// dragon_multicore_matrix_test.c
// Matrix operations benchmark for Dragon cache coherence protocol
// Extended to support 4 or 8 cores
#include <stdio.h>
#include <stdlib.h>

// Simple delay function
void delay(int cycles) {
    for (volatile int i = 0; i < cycles; i++);
}

// Define matrix dimensions
#define SIZE 16          // Matrix size (SIZE x SIZE)
#define BLOCK_SIZE 2     // Block size for tiled operations
#define MAX_CORES 8      // Maximum number of cores supported

// Structure to hold a matrix in shared memory
typedef struct {
    int data[SIZE][SIZE];
} Matrix;

// Small structure for control variables to ensure they're at the start of shared memory
typedef struct {
    int phase;                    // Current operation phase
    int sync_flags[MAX_CORES];    // Synchronization flags for each core
    int num_cores;                // Total number of cores in use
} Control;

// Function prototypes
void matrix_multiply_blocked(Matrix* A, Matrix* B, Matrix* C, int start_row, int end_row);
void matrix_transpose(Matrix* A, Matrix* B, int start_row, int end_row);
void matrix_add(Matrix* A, Matrix* B, Matrix* C, int start_col, int end_col);
void matrix_init(Matrix* M, int pattern);
int matrix_checksum(Matrix* M);

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <core_id>\n", argv[0]);
        return 1;
    }

    int core_id = atoi(argv[1]);
    
    // Shared memory region at virtual address 0x8000
    volatile void* shmem_ptr = (volatile void*) (4096 * 8);
    
    printf("Core %d: Starting Dragon multicore matrix test\n", core_id);
    
    // Memory layout - control structure first to ensure it's at beginning of shared memory
    volatile Control* control = (volatile Control*) shmem_ptr;
    
    // Matrices - place after control structure
    volatile Matrix* A = (volatile Matrix*) (control + 1);    // Input matrix A
    volatile Matrix* B = (volatile Matrix*) (A + 1);          // Input matrix B
    volatile Matrix* C = (volatile Matrix*) (B + 1);          // Result matrix C
    
    // Check memory bounds (only need to use 3 matrices to stay within 4KB)
    // Each 16x16 matrix is ~1KB, so 3 matrices plus control is around 3.1KB
    volatile char* end_ptr = (volatile char*)C + sizeof(Matrix);
    if (end_ptr > (char*)shmem_ptr + 4096) {
        printf("ERROR: Memory layout exceeds 4KB shared memory limit!\n");
        return 1;
    }
    
    // Core 0 initializes everything
    if (core_id == 0) {
        printf("Core 0: Initializing matrices and control variables\n");
        
        // If using 4 cores, set num_cores to 4; if using 8 cores, set to 8
        control->num_cores = 4;  // Change to 8 for 8-core test
        
        // Initialize control variables
        control->phase = 0;
        for (int i = 0; i < control->num_cores; i++) {
            control->sync_flags[i] = 0;
        }
        
        // Initialize matrices with different patterns
        matrix_init((Matrix*)A, 1);  // Initialize with pattern 1 (i+j)
        matrix_init((Matrix*)B, 2);  // Initialize with pattern 2 (i*j)
        
        // Clear result matrix
        for (int i = 0; i < SIZE; i++) {
            for (int j = 0; j < SIZE; j++) {
                C->data[i][j] = 0;
            }
        }
        
        printf("Core 0: Initialization complete, num_cores = %d\n", control->num_cores);
    } else {
        // Other cores wait for initialization
        delay(5000); // Wait for initialization
    }
    
    // Check if this core should participate
    if (core_id >= control->num_cores) {
        printf("Core %d: Not participating in this test (num_cores = %d)\n", 
              core_id, control->num_cores);
        return 0;
    }
    
    // All participating cores synchronize before starting
    control->sync_flags[core_id] = 1;
    printf("Core %d: Set sync flag, waiting for others\n", core_id);
    
    // Wait for all cores to be ready
    int all_ready = 0;
    while (!all_ready) {
        all_ready = 1;
        for (int i = 0; i < control->num_cores; i++) {
            if (control->sync_flags[i] != 1) {
                all_ready = 0;
                break;
            }
        }
        if (!all_ready) delay(100);
    }
    
    printf("Core %d: All cores ready, starting matrix operations\n", core_id);
    
    // Phase 1: Matrix multiplication C = A * B
    // Divide work among all cores based on rows
    if (core_id == 0) {
        // Core 0 signals start of phase 1
        control->phase = 1;
        
        // Reset sync flags
        for (int i = 0; i < control->num_cores; i++) {
            control->sync_flags[i] = 0;
        }
    }
    
    // Wait for phase 1 signal
    while (control->phase != 1) delay(100);
    
    // Calculate this core's portion of the work for multiplication
    int rows_per_core = SIZE / control->num_cores;
    int start_row = core_id * rows_per_core;
    int end_row = (core_id == control->num_cores - 1) ? SIZE : start_row + rows_per_core;
    
    printf("Core %d: Starting matrix multiplication (rows %d to %d)\n", 
          core_id, start_row, end_row-1);
    
    // Perform matrix multiplication for assigned rows
    matrix_multiply_blocked((Matrix*)A, (Matrix*)B, (Matrix*)C, start_row, end_row);
    
    // Signal completion
    control->sync_flags[core_id] = 1;
    printf("Core %d: Matrix multiplication complete\n", core_id);
    
    // Wait for all cores to complete multiplication
    all_ready = 0;
    while (!all_ready) {
        all_ready = 1;
        for (int i = 0; i < control->num_cores; i++) {
            if (control->sync_flags[i] != 1) {
                all_ready = 0;
                break;
            }
        }
        if (!all_ready) delay(100);
    }
    
    // Only core 0 calculates checksum
    if (core_id == 0) {
        printf("Core 0: Matrix C checksum: %d\n", matrix_checksum((Matrix*)C));
    }
    
    // Phase 2: Matrix transpose B = C^T
    // Divide work among cores based on rows
    if (core_id == 0) {
        // Signal start of phase 2
        control->phase = 2;
        
        // Reset sync flags
        for (int i = 0; i < control->num_cores; i++) {
            control->sync_flags[i] = 0;
        }
    }
    
    // Wait for phase 2 signal
    while (control->phase != 2) delay(100);
    
    printf("Core %d: Starting matrix transpose (rows %d to %d)\n", 
          core_id, start_row, end_row-1);
    
    // Perform transpose for assigned rows
    matrix_transpose((Matrix*)C, (Matrix*)B, start_row, end_row);
    
    // Signal completion
    control->sync_flags[core_id] = 2;
    printf("Core %d: Matrix transpose complete\n", core_id);
    
    // Wait for all cores to complete transpose
    all_ready = 0;
    while (!all_ready) {
        all_ready = 1;
        for (int i = 0; i < control->num_cores; i++) {
            if (control->sync_flags[i] != 2) {
                all_ready = 0;
                break;
            }
        }
        if (!all_ready) delay(100);
    }
    
    // Only core 0 calculates checksum
    if (core_id == 0) {
        printf("Core 0: Matrix B checksum (transpose result): %d\n", matrix_checksum((Matrix*)B));
    }
    
    // Phase 3: Matrix addition A = C + B
    // Divide work among cores based on columns instead of rows
    if (core_id == 0) {
        // Signal start of phase 3
        control->phase = 3;
        
        // Reset sync flags
        for (int i = 0; i < control->num_cores; i++) {
            control->sync_flags[i] = 0;
        }
    }
    
    // Wait for phase 3 signal
    while (control->phase != 3) delay(100);
    
    // Calculate this core's portion for addition (by columns)
    int cols_per_core = SIZE / control->num_cores;
    int start_col = core_id * cols_per_core;
    int end_col = (core_id == control->num_cores - 1) ? SIZE : start_col + cols_per_core;
    
    printf("Core %d: Starting matrix addition (columns %d to %d)\n", 
          core_id, start_col, end_col-1);
    
    // Process assigned columns
    for (int i = 0; i < SIZE; i++) {
        for (int j = start_col; j < end_col; j++) {
            A->data[i][j] = C->data[i][j] + B->data[i][j];
            delay(1);  // Small delay for better coherence effects
        }
    }
    
    // Signal completion
    control->sync_flags[core_id] = 3;
    printf("Core %d: Matrix addition complete\n", core_id);
    
    // Wait for all cores to complete addition
    all_ready = 0;
    while (!all_ready) {
        all_ready = 1;
        for (int i = 0; i < control->num_cores; i++) {
            if (control->sync_flags[i] != 3) {
                all_ready = 0;
                break;
            }
        }
        if (!all_ready) delay(100);
    }
    
    // Final checksum and completion
    if (core_id == 0) {
        printf("Core 0: All matrix operations complete\n");
        printf("Core 0: Final result matrix A checksum: %d\n", matrix_checksum((Matrix*)A));
        
        // Signal completion
        control->phase = 4;
    } else {
        // Wait for final phase
        while (control->phase != 4) delay(100);
    }
    
    printf("Core %d: Matrix test completed\n", core_id);
    return 0;
}

// Initialize matrix with a pattern
void matrix_init(Matrix* M, int pattern) {
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            switch (pattern) {
                case 1:  M->data[i][j] = i + j; break;         // Pattern 1: i+j
                case 2:  M->data[i][j] = (i * j) % 10; break;  // Pattern 2: i*j mod 10
                default: M->data[i][j] = 1; break;             // Default: all 1's
            }
        }
    }
}

// Compute matrix multiplication using blocking for better cache behavior
void matrix_multiply_blocked(Matrix* A, Matrix* B, Matrix* C, int start_row, int end_row) {
    for (int i = start_row; i < end_row; i += BLOCK_SIZE) {
        for (int j = 0; j < SIZE; j += BLOCK_SIZE) {
            for (int k = 0; k < SIZE; k += BLOCK_SIZE) {
                // Compute the block multiplication
                for (int ii = i; ii < i + BLOCK_SIZE && ii < end_row; ii++) {
                    for (int jj = j; jj < j + BLOCK_SIZE && jj < SIZE; jj++) {
                        for (int kk = k; kk < k + BLOCK_SIZE && kk < SIZE; kk++) {
                            C->data[ii][jj] += A->data[ii][kk] * B->data[kk][jj];
                        }
                        delay(1); // Small delay for better coherence effects
                    }
                }
            }
        }
    }
}

// Compute matrix transpose
void matrix_transpose(Matrix* A, Matrix* B, int start_row, int end_row) {
    for (int i = start_row; i < end_row; i++) {
        for (int j = 0; j < SIZE; j++) {
            B->data[j][i] = A->data[i][j];
            delay(1); // Small delay for better coherence effects
        }
    }
}

// Compute checksum of matrix (for validation)
int matrix_checksum(Matrix* M) {
    int sum = 0;
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            sum += M->data[i][j];
        }
    }
    return sum;
}