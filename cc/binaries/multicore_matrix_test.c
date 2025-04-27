// dragon_fixed_multicore_test.c
// Memory-efficient matrix operations benchmark for Dragon cache coherence protocol
// Supports 4/8 cores with deadlock prevention
#include <stdio.h>
#include <stdlib.h>

// Simple delay function
void delay(int cycles) {
    for (volatile int i = 0; i < cycles; i++);
}

// Define matrix dimensions - keep small to fit in 4KB
#define SIZE 6           // 6x6 matrices
#define MAX_CORES 8      // Maximum number of cores supported

// Structure for control variables - keep separate from matrices
typedef struct {
    volatile int phase;                  // Current operation phase
    volatile int sync_flags[MAX_CORES];  // Sync flags for each core
    volatile int num_cores;              // Number of cores in use
    volatile int checksum;               // For sharing checksum results
} Control;

// Function to initialize a matrix
void matrix_init(volatile int data[SIZE][SIZE], int pattern) {
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            switch (pattern) {
                case 1:  data[i][j] = i + j; break;         // Pattern 1: i+j
                case 2:  data[i][j] = (i * j) % 10; break;  // Pattern 2: i*j mod 10
                default: data[i][j] = 0; break;             // Default: all zeros
            }
        }
    }
}

// Function to compute matrix checksum
int matrix_checksum(volatile int data[SIZE][SIZE]) {
    int sum = 0;
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            sum += data[i][j];
        }
    }
    return sum;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <core_id>\n", argv[0]);
        return 1;
    }

    int core_id = atoi(argv[1]);
    
    // Shared memory region at virtual address 0x8000
    volatile void* shmem_ptr = (volatile void*) (4096 * 8);
    
    printf("Core %d: Starting Dragon fixed multicore test\n", core_id);
    
    // Memory layout - control variables first, then matrices
    volatile Control* control = (volatile Control*) shmem_ptr;
    
    // Place matrices after control structure
    volatile int (*A)[SIZE] = (volatile int (*)[SIZE])((char*)control + sizeof(Control));
    volatile int (*B)[SIZE] = (volatile int (*)[SIZE])((char*)A + sizeof(int[SIZE][SIZE]));
    
    // Check memory bounds
    volatile char* end_ptr = (volatile char*)B + sizeof(int[SIZE][SIZE]);
    
    // Core 0 initializes everything
    if (core_id == 0) {
        printf("Core 0: Initializing matrices and control variables\n");
        
        // Check memory bounds
        printf("Core 0: Control at %p, A at %p, B at %p, End at %p\n", 
               control, A, B, end_ptr);
        
        if (end_ptr > (char*)shmem_ptr + 4096) {
            printf("ERROR: Memory layout exceeds 4KB shared memory limit!\n");
            // Initialize just enough to let other cores know
            control->phase = -1;
            return 1;
        }
        
        // Configure number of cores (4 or 8)
        control->num_cores = 4;  // Change to 8 for 8-core test
        
        // Initialize control variables
        control->phase = 0;
        control->checksum = 0;
        
        // Important: initialize all sync flags
        for (int i = 0; i < MAX_CORES; i++) {
            control->sync_flags[i] = 0;
        }
        
        // Initialize matrices
        matrix_init(A, 1);  // Initialize A with pattern 1
        matrix_init(B, 0);  // Initialize B with zeros
        
        // Memory barrier to ensure all writes are visible
        __sync_synchronize();
        
        printf("Core 0: Matrices initialized\n");
    }
    
    // Wait a bit to ensure initialization is complete
    delay(5000);
    
    // Check if this core should participate
    if (core_id >= control->num_cores) {
        printf("Core %d: Not needed for this test (using %d cores)\n", 
               core_id, control->num_cores);
        return 0;
    }
    
    // Check for initialization error
    if (control->phase == -1) {
        printf("Core %d: Detected memory overflow error, exiting\n", core_id);
        return 1;
    }
    
    // First synchronization: all cores signal they're ready
    printf("Core %d: Setting initial sync flag\n", core_id);
    control->sync_flags[core_id] = 1;
    
    // Wait for all cores to be ready
    int timeout = 0;
    int all_ready = 0;
    
    while (!all_ready && timeout < 100) {
        all_ready = 1;
        
        for (int i = 0; i < control->num_cores; i++) {
            if (control->sync_flags[i] != 1) {
                all_ready = 0;
                break;
            }
        }
        
        if (!all_ready) {
            delay(1000);
            timeout++;
        }
    }
    
    if (timeout >= 100) {
        printf("Core %d: Timeout waiting for all cores to initialize, continuing anyway\n", core_id);
    }
    
    printf("Core %d: Starting matrix operations\n", core_id);
    
    // Calculate work division
    int rows_per_core = SIZE / control->num_cores;
    int start_row = core_id * rows_per_core;
    int end_row = (core_id == control->num_cores - 1) ? SIZE : start_row + rows_per_core;
    
    // Reset sync flags for next phase (only core 0 does this)
    if (core_id == 0) {
        // Signal start of phase 1 AFTER resetting flags
        for (int i = 0; i < control->num_cores; i++) {
            control->sync_flags[i] = 0;
        }
        
        // Memory barrier
        __sync_synchronize();
        
        // Now set the phase to signal other cores
        control->phase = 1;
    }
    
    // Wait for phase 1 signal
    timeout = 0;
    while (control->phase != 1 && timeout < 100) {
        delay(1000);
        timeout++;
    }
    
    if (timeout >= 100) {
        printf("Core %d: Timeout waiting for phase 1 signal\n", core_id);
    }
    
    // Phase 1: Matrix multiplication
    printf("Core %d: Starting matrix multiplication (rows %d to %d)\n", 
           core_id, start_row, end_row-1);
    
    // Process assigned rows
    for (int i = start_row; i < end_row; i++) {
        for (int j = 0; j < SIZE; j++) {
            int sum = 0;
            for (int k = 0; k < SIZE; k++) {
                sum += A[i][k] * A[k][j];  // Use A as both input matrices
            }
            B[i][j] = sum;  // Store result in B
            delay(1);
        }
    }
    
    // Signal completion of phase 1
    printf("Core %d: Matrix multiplication complete\n", core_id);
    control->sync_flags[core_id] = 1;
    
    // Core 0 calculates checksum and coordinates phase 2
    if (core_id == 0) {
        // Wait for all cores to complete matrix multiplication
        timeout = 0;
        all_ready = 0;
        
        while (!all_ready && timeout < 100) {
            all_ready = 1;
            
            for (int i = 0; i < control->num_cores; i++) {
                if (control->sync_flags[i] != 1) {
                    all_ready = 0;
                    break;
                }
            }
            
            if (!all_ready) {
                delay(1000);
                timeout++;
            }
        }
        
        if (timeout >= 100) {
            printf("Core 0: Timeout waiting for multiplication completion\n");
        }
        
        // Calculate and store checksum
        control->checksum = matrix_checksum(B);
        printf("Core 0: Matrix B checksum after multiplication: %d\n", control->checksum);
        
        // Reset sync flags for next phase
        for (int i = 0; i < control->num_cores; i++) {
            control->sync_flags[i] = 0;
        }
        
        // Memory barrier
        __sync_synchronize();
        
        // Signal start of phase 2
        control->phase = 2;
    }
    
    // All cores wait for phase 2 signal
    timeout = 0;
    while (control->phase != 2 && timeout < 100) {
        delay(1000);
        timeout++;
    }
    
    if (timeout >= 100) {
        printf("Core %d: Timeout waiting for phase 2 signal\n", core_id);
    }
    
    // Phase 2: Matrix transposition by columns
    // Calculate column division
    int cols_per_core = SIZE / control->num_cores;
    int start_col = core_id * cols_per_core;
    int end_col = (core_id == control->num_cores - 1) ? SIZE : start_col + cols_per_core;
    
    printf("Core %d: Starting matrix transposition (columns %d to %d)\n", 
           core_id, start_col, end_col-1);
    
    // Process assigned columns
    for (int j = start_col; j < end_col; j++) {
        for (int i = 0; i < SIZE; i++) {
            // Only swap if i != j to avoid conflicts
            if (i != j) {
                int temp = B[i][j];
                B[i][j] = B[j][i];
                B[j][i] = temp;
            }
            delay(1);
        }
    }
    
    // Signal completion of phase 2
    printf("Core %d: Matrix transposition complete\n", core_id);
    control->sync_flags[core_id] = 2;
    
    // Core 0 calculates checksum and signals completion
    if (core_id == 0) {
        // Wait for all cores to complete transposition
        timeout = 0;
        all_ready = 0;
        
        while (!all_ready && timeout < 100) {
            all_ready = 1;
            
            for (int i = 0; i < control->num_cores; i++) {
                if (control->sync_flags[i] != 2) {
                    all_ready = 0;
                    break;
                }
            }
            
            if (!all_ready) {
                delay(1000);
                timeout++;
            }
        }
        
        if (timeout >= 100) {
            printf("Core 0: Timeout waiting for transposition completion\n");
        }
        
        // Calculate and store final checksum
        control->checksum = matrix_checksum(B);
        printf("Core 0: Matrix B final checksum: %d\n", control->checksum);
        printf("Core 0: All matrix operations complete\n");
        
        // Signal completion
        control->phase = 3;
    }
    
    // All cores wait for completion signal
    timeout = 0;
    while (control->phase != 3 && timeout < 100) {
        delay(1000);
        timeout++;
    }
    
    if (timeout >= 100) {
        printf("Core %d: Timeout waiting for completion signal\n", core_id);
    } else {
        printf("Core %d: All matrix operations completed successfully\n", core_id);
    }
    
    return 0;
}