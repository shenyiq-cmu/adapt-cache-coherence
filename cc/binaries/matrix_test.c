// dragon_fixed_matrix_test.c
// Memory-efficient matrix operations benchmark for Dragon cache coherence protocol
#include <stdio.h>
#include <stdlib.h>

// Simple delay function
void delay(int cycles) {
    for (volatile int i = 0; i < cycles; i++);
}

// Define a very small matrix size to ensure we stay within 4KB
#define SIZE 6           // Just 6x6 matrices
#define MAX_ITER 10      // Limit number of iterations

// Structure for control variables - keep separate from matrices
typedef struct {
    int sync_flags[2];   // Synchronization flags
    int phase;           // Current operation phase
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
    
    printf("Core %d: Starting Dragon fixed matrix test\n", core_id);
    
    // Memory layout - control variables first, then matrices
    // Put all control variables at the beginning of shared memory
    volatile Control* control = (volatile Control*) shmem_ptr;
    
    // Place matrices after control structure
    volatile int (*A)[SIZE] = (volatile int (*)[SIZE])((char*)control + sizeof(Control));
    volatile int (*B)[SIZE] = (volatile int (*)[SIZE])((char*)A + sizeof(int[SIZE][SIZE]));
    
    // Check that we're within memory limits
    volatile char* end_ptr = (volatile char*)B + sizeof(int[SIZE][SIZE]);
    volatile int* overflow_check = (volatile int*)end_ptr;
    
    // Initialize if core 0
    if (core_id == 0) {
        printf("Core 0: Initializing matrices\n");
        
        // Check memory bounds
        printf("Core 0: Control at %p, A at %p, B at %p, End at %p\n", 
               control, A, B, end_ptr);
        
        if (end_ptr > (char*)shmem_ptr + 4096) {
            printf("ERROR: Memory layout exceeds 4KB shared memory limit!\n");
            // Initialize just enough to let core 1 know
            control->phase = -1;
            return 1;
        }
        
        // Initialize control variables
        control->phase = 0;
        control->sync_flags[0] = 0;
        control->sync_flags[1] = 0;
        
        // Initialize matrices
        matrix_init(A, 1);  // Initialize A with pattern 1
        matrix_init(B, 0);  // Initialize B with zeros
        
        printf("Core 0: Matrices initialized\n");
    }
    
    // Check for overflow error
    if (core_id == 1) {
        // Wait for Core 0 to initialize
        int timeout = 0;
        while (control->phase == 0 && timeout < 1000) {
            delay(100);
            timeout++;
        }
        
        if (control->phase == -1) {
            printf("Core 1: Detected memory overflow, exiting\n");
            return 1;
        }
    }
    
    // Synchronize before starting
    if (core_id == 0) {
        control->sync_flags[0] = 1;
        int timeout = 0;
        while (control->sync_flags[1] == 0 && timeout < 1000) {
            delay(100);
            timeout++;
        }
        
        if (timeout >= 1000) {
            printf("Core 0: Timeout waiting for Core 1 sync, continuing anyway\n");
        }
    } else {
        control->sync_flags[1] = 1;
        int timeout = 0;
        while (control->sync_flags[0] == 0 && timeout < 1000) {
            delay(100);
            timeout++;
        }
        
        if (timeout >= 1000) {
            printf("Core 1: Timeout waiting for Core 0 sync, continuing anyway\n");
        }
    }
    
    printf("Core %d: Starting matrix operations\n", core_id);
    
    // Reset flags
    if (core_id == 0) {
        control->sync_flags[0] = 0;
        control->sync_flags[1] = 0;
    }
    
    // Phase 1: Simple matrix multiplication with row partitioning
    if (core_id == 0) {
        // Signal start of phase 1
        control->phase = 1;
        printf("Core 0: Starting matrix multiplication (top half)\n");
        
        // Process top half (rows 0 to SIZE/2-1)
        for (int i = 0; i < SIZE/2; i++) {
            for (int j = 0; j < SIZE; j++) {
                int sum = 0;
                for (int k = 0; k < SIZE; k++) {
                    sum += A[i][k] * A[k][j];  // Use A as both input matrices
                }
                B[i][j] = sum;  // Store result in B
                delay(1);
            }
        }
        
        control->sync_flags[0] = 1;
        printf("Core 0: Matrix multiplication complete\n");
    } else {
        // Wait for phase 1 to be signaled
        int timeout = 0;
        while (control->phase != 1 && timeout < 1000) {
            delay(100);
            timeout++;
        }
        
        if (timeout >= 1000) {
            printf("Core 1: Timeout waiting for phase 1, exiting\n");
            return 1;
        }
        
        printf("Core 1: Starting matrix multiplication (bottom half)\n");
        
        // Process bottom half (rows SIZE/2 to SIZE-1)
        for (int i = SIZE/2; i < SIZE; i++) {
            for (int j = 0; j < SIZE; j++) {
                int sum = 0;
                for (int k = 0; k < SIZE; k++) {
                    sum += A[i][k] * A[k][j];  // Use A as both input matrices
                }
                B[i][j] = sum;  // Store result in B
                delay(1);
            }
        }
        
        control->sync_flags[1] = 1;
        printf("Core 1: Matrix multiplication complete\n");
    }
    
    // Synchronize after multiplication
    if (core_id == 0) {
        int timeout = 0;
        while (control->sync_flags[1] == 0 && timeout < 1000) {
            delay(100);
            timeout++;
        }
        
        if (timeout >= 1000) {
            printf("Core 0: Timeout waiting for Core 1 after multiplication\n");
        } else {
            printf("Core 0: Matrix B checksum after multiplication: %d\n", 
                   matrix_checksum(B));
        }
    } else {
        int timeout = 0;
        while (control->sync_flags[0] == 0 && timeout < 1000) {
            delay(100);
            timeout++;
        }
        
        if (timeout >= 1000) {
            printf("Core 1: Timeout waiting for Core 0 after multiplication\n");
        }
    }
    
    // Phase 2: Matrix transposition with column partitioning
    if (core_id == 0) {
        // Reset flags
        control->sync_flags[0] = 0;
        control->sync_flags[1] = 0;
        
        // Signal start of phase 2
        control->phase = 2;
        printf("Core 0: Starting matrix transposition (left half)\n");
        
        // Process left half (columns 0 to SIZE/2-1)
        for (int i = 0; i < SIZE; i++) {
            for (int j = 0; j < SIZE/2; j++) {
                int temp = B[i][j];
                B[i][j] = B[j][i];  // Simple swap to create coherence traffic
                B[j][i] = temp;
                delay(1);
            }
        }
        
        control->sync_flags[0] = 2;
        printf("Core 0: Matrix transposition complete\n");
    } else {
        // Wait for phase 2 to be signaled
        int timeout = 0;
        while (control->phase != 2 && timeout < 1000) {
            delay(100);
            timeout++;
        }
        
        if (timeout >= 1000) {
            printf("Core 1: Timeout waiting for phase 2, exiting\n");
            return 1;
        }
        
        printf("Core 1: Starting matrix transposition (right half)\n");
        
        // Process right half (columns SIZE/2 to SIZE-1)
        for (int i = 0; i < SIZE; i++) {
            for (int j = SIZE/2; j < SIZE; j++) {
                // Only transpose if i != j to avoid conflicts with Core 0
                if (i != j) {
                    int temp = B[i][j];
                    B[i][j] = B[j][i];  // Simple swap to create coherence traffic
                    B[j][i] = temp;
                }
                delay(1);
            }
        }
        
        control->sync_flags[1] = 2;
        printf("Core 1: Matrix transposition complete\n");
    }
    
    // Final synchronization
    if (core_id == 0) {
        int timeout = 0;
        while (control->sync_flags[1] != 2 && timeout < 1000) {
            delay(100);
            timeout++;
        }
        
        if (timeout >= 1000) {
            printf("Core 0: Timeout waiting for Core 1 after transposition\n");
        } else {
            printf("Core 0: Matrix B final checksum: %d\n", 
                   matrix_checksum(B));
            printf("Core 0: All matrix operations complete\n");
        }
        
        // Signal completion
        control->phase = 3;
    } else {
        int timeout = 0;
        while (control->phase != 3 && timeout < 1000) {
            delay(100);
            timeout++;
        }
        
        if (timeout >= 1000) {
            printf("Core 1: Timeout waiting for final phase\n");
        } else {
            printf("Core 1: All matrix operations complete\n");
        }
    }
    
    printf("Core %d: Matrix test completed\n", core_id);
    return 0;
}