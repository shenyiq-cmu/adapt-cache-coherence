// dragon_matrix_test.c
 // Matrix operations benchmark for Dragon cache coherence protocol
 #include <stdio.h>
 #include <stdlib.h>
 
 // Simple delay function
 void delay(int cycles) {
     for (volatile int i = 0; i < cycles; i++);
 }
 
 // Define matrix dimensions
 #define SIZE 16          // Matrix size (SIZE x SIZE)
 #define BLOCK_SIZE 2     // Block size for tiled operations
 
 // Structure to hold a matrix in shared memory
 typedef struct {
     int data[SIZE][SIZE];
 } Matrix;
 
 // Function prototypes
 void matrix_multiply_blocked(Matrix* A, Matrix* B, Matrix* C, int start_row, int end_row);
 void matrix_transpose(Matrix* A, Matrix* B, int start_row, int end_row);
 void matrix_add(Matrix* A, Matrix* B, Matrix* C, int start_row, int end_row);
 void matrix_init(Matrix* M, int pattern);
 void matrix_print(Matrix* M, const char* name);
 int matrix_checksum(Matrix* M);
 
 int main(int argc, char** argv) {
     if (argc < 2) {
         printf("Usage: %s <core_id>\n", argv[0]);
         return 1;
     }
 
     int core_id = atoi(argv[1]);
     
     // Shared memory region at virtual address 0x8000
     volatile void* shmem_ptr = (volatile void*) (4096 * 8);
     
     printf("Core %d: Starting Dragon matrix test\n", core_id);
     
     // Memory layout
     // We need space for 5 matrices (A, B, C, T, R) plus control variables
     // Each matrix is SIZE*SIZE*sizeof(int) bytes
     
     // Matrices
     volatile Matrix* A = (volatile Matrix*) shmem_ptr;                    // Input matrix A
     volatile Matrix* B = (volatile Matrix*) (A + 1);                      // Input matrix B
     volatile Matrix* C = (volatile Matrix*) (B + 1);                      // Result matrix C
     volatile Matrix* T = (volatile Matrix*) (C + 1);                      // Transpose matrix T
     volatile Matrix* R = (volatile Matrix*) (T + 1);                      // Final result matrix R
     
     // Control variables
     volatile int* phase = (volatile int*) (R + 1);                         // Current operation phase
     volatile int* sync_flags = (volatile int*) (phase + 1);                // Synchronization flags (2 elements)
     
     // Initialize if core 0
     if (core_id == 0) {
         printf("Core 0: Initializing matrices\n");
         
         // Initialize matrices with different patterns
         matrix_init((Matrix*)A, 1);  // Initialize with pattern 1 (i+j)
         matrix_init((Matrix*)B, 2);  // Initialize with pattern 2 (i*j)
         
         // Clear result matrices
         for (int i = 0; i < SIZE; i++) {
             for (int j = 0; j < SIZE; j++) {
                 C->data[i][j] = 0;
                 T->data[i][j] = 0;
                 R->data[i][j] = 0;
             }
         }
         
         // Initialize control variables
         *phase = 0;
         sync_flags[0] = 0;
         sync_flags[1] = 0;
         
         // Print input matrices
         printf("Core 0: Matrix A initialized\n");
         printf("Core 0: Matrix B initialized\n");
     } else{
        delay(5000); // wait for init
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
         matrix_multiply_blocked((Matrix*)A, (Matrix*)B, (Matrix*)C, 0, SIZE/2);
         sync_flags[0] = 2;
         printf("Core 0: Matrix multiplication complete\n");
     } else {
         while (*phase != 1) delay(100);
         printf("Core 1: Starting matrix multiplication (bottom half)\n");
         matrix_multiply_blocked((Matrix*)A, (Matrix*)B, (Matrix*)C, SIZE/2, SIZE);
         sync_flags[1] = 2;
         printf("Core 1: Matrix multiplication complete\n");
     }
     
     // Synchronize after multiplication
     if (core_id == 0) {
         while (sync_flags[1] != 2) delay(100);
         printf("Core 0: Matrix C checksum: %d\n", matrix_checksum((Matrix*)C));
     } else {
         while (sync_flags[0] != 2) delay(100);
     }
     
     // Phase 2: Matrix transpose T = C^T
     // Core 0 computes first half, Core 1 computes second half
     if (core_id == 0) {
         *phase = 2;
         printf("Core 0: Starting matrix transpose (first half)\n");
         matrix_transpose((Matrix*)C, (Matrix*)T, 0, SIZE/2);
         sync_flags[0] = 3;
         printf("Core 0: Matrix transpose complete\n");
     } else {
         while (*phase != 2) delay(100);
         printf("Core 1: Starting matrix transpose (second half)\n");
         matrix_transpose((Matrix*)C, (Matrix*)T, SIZE/2, SIZE);
         sync_flags[1] = 3;
         printf("Core 1: Matrix transpose complete\n");
     }
     
     // Synchronize after transpose
     if (core_id == 0) {
         while (sync_flags[1] != 3) delay(100);
         printf("Core 0: Matrix T checksum: %d\n", matrix_checksum((Matrix*)T));
     } else {
         while (sync_flags[0] != 3) delay(100);
     }
     
     // Phase 3: Matrix addition R = C + T
     // This time Core 0 computes left half, Core 1 computes right half
     // This creates a different access pattern to test coherence
     if (core_id == 0) {
         *phase = 3;
         printf("Core 0: Starting matrix addition (columns 0-%d)\n", SIZE/2-1);
         
         // Process left half of matrix (columns 0 to SIZE/2-1)
         for (int i = 0; i < SIZE; i++) {
             for (int j = 0; j < SIZE/2; j++) {
                 R->data[i][j] = C->data[i][j] + T->data[i][j];
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
                 R->data[i][j] = C->data[i][j] + T->data[i][j];
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
         printf("Core 0: Final result matrix R checksum: %d\n", matrix_checksum((Matrix*)R));
         
         // Signal completion
         *phase = 4;
     } else {
         while (*phase != 4) delay(100);
         printf("Core 1: All matrix operations complete\n");
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
 
 // Print a matrix (for debugging)
 void matrix_print(Matrix* M, const char* name) {
     printf("Matrix %s:\n", name);
     for (int i = 0; i < SIZE; i++) {
         for (int j = 0; j < SIZE; j++) {
             printf("%3d ", M->data[i][j]);
         }
         printf("\n");
     }
     printf("\n");
 }