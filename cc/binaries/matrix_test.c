// dragon_small_matrix_test.c
// Small matrix operations benchmark for Dragon cache coherence protocol
// Designed to fit within 4KB of shared memory
#include <stdio.h>
#include <stdlib.h>

// Simple delay function
void delay(int cycles) {
    for (volatile int i = 0; i < cycles; i++);
}

// Define matrix dimensions - small enough to fit in 4KB shared memory
#define SIZE 10          // Matrix size (SIZE x SIZE)

// Structure to hold a matrix in shared memory
typedef struct {
    int data[SIZE][SIZE];
} Matrix;

// Function prototypes
void matrix_multiply(Matrix* A, Matrix* B, Matrix* C, int start_row, int end_row);
void matrix_transpose(Matrix* A, Matrix* B, int start_row, int end_row);
void matrix_add(Matrix* A, Matrix* B, Matrix* C, int start_row, int end_row);
int matrix_checksum(Matrix* M);

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <core_id>\n", argv[0]);
        return 1;
    }

    int core_id = atoi(argv[1]);
    
    // Shared memory region at virtual address 0x8000
    volatile void* shmem_ptr = (volatile void*) (4096 * 8);
    
    printf("Core %d: Starting Dragon small matrix test\n", core_id);
    
    // Memory layout - use only 3 matrices to fit in 4KB
    // Each 10x10 matrix is about 400 bytes
    volatile Matrix* A = (volatile Matrix*) shmem_ptr;                    // Input matrix A
    volatile Matrix* B = (volatile Matrix*) (A + 1);                      // Input matrix B
    volatile Matrix* C = (volatile Matrix*) (B + 1);                      // Result matrix C
    
    // Control variables
    volatile int* phase = (volatile int*) (C + 1);                        // Current operation phase
    volatile int* sync_flags = (volatile int*) (phase + 1);               // Synchronization flags (2 elements)
    
    // Initialize if core 0
    if (core_id == 0) {
        printf("Core 0: Initializing matrices\n");
        
        // Initialize matrices with patterns
        for (int i = 0; i < SIZE; i++) {
            for (int j = 0; j < SIZE; j++) {
                A->data[i][j] = i + j;                // Pattern for A
                B->data[i][j] = (i * j) % 10;         // Pattern for B
                C->data[i][j] = 0;                    // Clear result matrix
            }
        }
        
        // Initialize control variables
        *phase = 0;
        sync_flags[0] = 0;
        sync_flags[1] = 0;
        
        printf("Core 0: Matrices initialized\n");
    }
    
    // Make sure both cores are ready before starting
    if (core_id == 0) {
        sync_flags[0] = 1;
        while (sync_flags[1] == 0) delay(100);
    } else {
        sync_flags[1] = 1;
        while (sync_flags[0] == 0) delay(100);
    }
    
    printf("Core %d: Starting matrix operations\n", core_id);
    
    // Phase 1: Matrix multiplication C = A * B
    // Core 0 computes top half, Core 1 computes bottom half
    if (core_id == 0) {
        *phase = 1;
        printf("Core 0: Starting matrix multiplication (top half)\n");
        matrix_multiply((Matrix*)A, (Matrix*)B, (Matrix*)C, 0, SIZE/2);
        sync_flags[0] = 2;
        printf("Core 0: Matrix multiplication complete\n");
    } else {
        while (*phase != 1) delay(100);
        printf("Core 1: Starting matrix multiplication (bottom half)\n");
        matrix_multiply((Matrix*)A, (Matrix*)B, (Matrix*)C, SIZE/2, SIZE);
        sync_flags[1] = 2;
        printf("Core 1: Matrix multiplication complete\n");
    }
    
    // Synchronize after multiplication
    if (core_id == 0) {
        while (sync_flags[1] != 2) delay(100);
        printf("Core 0: Matrix C checksum after multiplication: %d\n", matrix_checksum((Matrix*)C));
    } else {
        while (sync_flags[0] != 2) delay(100);
    }
    
    // Phase 2: Matrix reuse - overwrite A with C transpose
    // Core 0 computes first half, Core 1 computes second half
    if (core_id == 0) {
        *phase = 2;
        printf("Core 0: Starting matrix transpose (first half)\n");
        matrix_transpose((Matrix*)C, (Matrix*)A, 0, SIZE/2);
        sync_flags[0] = 3;
        printf("Core 0: Matrix transpose complete\n");
    } else {
        while (*phase != 2) delay(100);
        printf("Core 1: Starting matrix transpose (second half)\n");
        matrix_transpose((Matrix*)C, (Matrix*)A, SIZE/2, SIZE);
        sync_flags[1] = 3;
        printf("Core 1: Matrix transpose complete\n");
    }
    
    // Synchronize after transpose
    if (core_id == 0) {
        while (sync_flags[1] != 3) delay(100);
        printf("Core 0: Matrix A checksum after transpose: %d\n", matrix_checksum((Matrix*)A));
    } else {
        while (sync_flags[0] != 3) delay(100);
    }
    
    // Phase 3: Matrix addition B = C + A
    // This time Core 0 computes left half, Core 1 computes right half
    if (core_id == 0) {
        *phase = 3;
        printf("Core 0: Starting matrix addition (columns 0-%d)\n", SIZE/2-1);
        
        // Process left half of matrix (columns 0 to SIZE/2-1)
        for (int i = 0; i < SIZE; i++) {
            for (int j = 0; j < SIZE/2; j++) {
                B->data[i][j] = C->data[i][j] + A->data[i][j];
                delay(1);
            }
        }
        
        sync_flags[0] = 4;
        printf("Core 0: Matrix addition complete\n");
    } else {
        while (*phase != 3) delay(100);
        printf("Core 1: Starting matrix addition (columns %d-%d)\n", SIZE/2, SIZE-1);
        
        // Process right half of matrix (columns SIZE/2 to SIZE-1)
        for (int i = 0; i < SIZE; i++) {
            for (int j = SIZE/2; j < SIZE; j++) {
                B->data[i][j] = C->data[i][j] + A->data[i][j];
                delay(1);
            }
        }
        
        sync_flags[1] = 4;
        printf("Core 1: Matrix addition complete\n");
    }
    
    // Final synchronization
    if (core_id == 0) {
        while (sync_flags[1] != 4) delay(100);
        
        printf("Core 0: All matrix operations complete\n");
        printf("Core 0: Final result matrix B checksum: %d\n", matrix_checksum((Matrix*)B));
        
        // Signal completion
        *phase = 4;
    } else {
        while (*phase != 4) delay(100);
        printf("Core 1: All matrix operations complete\n");
    }
    
    printf("Core %d: Matrix test completed\n", core_id);
    return 0;
}

// Compute matrix multiplication
void matrix_multiply(Matrix* A, Matrix* B, Matrix* C, int start_row, int end_row) {
    for (int i = start_row; i < end_row; i++) {
        for (int j = 0; j < SIZE; j++) {
            int sum = 0;
            for (int k = 0; k < SIZE; k++) {
                sum += A->data[i][k] * B->data[k][j];
            }
            C->data[i][j] = sum;
            delay(1); // Small delay for better coherence effects
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