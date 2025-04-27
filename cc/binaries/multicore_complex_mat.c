// dragon_matrix_test_4core.c
// 4-core version with more shared state for cache coherence effects

#include <stdio.h>
#include <stdlib.h>

// Simple delay function
void delay(int cycles) {
    for (volatile int i = 0; i < cycles; i++);
}

// Matrix dimensions
#define SIZE 16
#define BLOCK_SIZE 2

// Shared matrix
typedef struct {
    int data[SIZE][SIZE];
} Matrix;

// Lock structure (simple spinlock)
typedef struct {
    volatile int lock;
} SpinLock;

void spin_lock(SpinLock* lock) {
    while (__sync_lock_test_and_set(&lock->lock, 1)) {
        delay(100);
    }
}

void spin_unlock(SpinLock* lock) {
    __sync_lock_release(&lock->lock);
}

// Globals
void matrix_init(Matrix* M, int pattern);
void matrix_multiply_blocked(Matrix* A, Matrix* B, Matrix* C, int start_row, int end_row);
void matrix_transpose(Matrix* A, Matrix* B, int start_row, int end_row);
void matrix_add(Matrix* A, Matrix* B, Matrix* C, int start_row, int end_row, int col_start, int col_end);
int matrix_checksum(Matrix* M);

// Main
int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <core_id>\n", argv[0]);
        return 1;
    }

    int core_id = atoi(argv[1]);
    volatile void* shmem_ptr = (volatile void*) (4096 * 8);

    printf("Core %d: Starting Dragon matrix test (4-core)\n", core_id);

    volatile Matrix* A = (volatile Matrix*) shmem_ptr;
    volatile Matrix* B = (volatile Matrix*) (A + 1);
    volatile Matrix* C = (volatile Matrix*) (B + 1);
    volatile Matrix* T = (volatile Matrix*) (C + 1);
    volatile Matrix* R = (volatile Matrix*) (T + 1);

    volatile int* phase = (volatile int*) (R + 1);
    volatile int* sync_flags = (volatile int*) (phase + 1); // 4 elements for 4 cores
    volatile int* global_counter = (volatile int*) (sync_flags + 4); // global counter
    volatile SpinLock* shared_lock = (volatile SpinLock*) (global_counter + 1); // simple lock

    // Init by core 0
    if (core_id == 0) {
        printf("Core 0: Initializing matrices\n");

        matrix_init((Matrix*)A, 1);
        matrix_init((Matrix*)B, 2);

        for (int i = 0; i < SIZE; i++) {
            for (int j = 0; j < SIZE; j++) {
                C->data[i][j] = 0;
                T->data[i][j] = 0;
                R->data[i][j] = 0;
            }
        }

        *phase = 0;
        for (int i = 0; i < 4; i++) sync_flags[i] = 0;
        *global_counter = 0;
        shared_lock->lock = 0;
    } else {
        delay(5000); // wait for init
    }

    // Synchronize
    sync_flags[core_id] = 1;
    while (!(sync_flags[0] && sync_flags[1] && sync_flags[2] && sync_flags[3])) delay(100);

    printf("Core %d: Matrices initialized, starting operations\n", core_id);

    // Phase 1: Matrix multiplication (split rows among cores)
    if (core_id == 0) *phase = 1;
    while (*phase != 1) delay(100);

    int row_per_core = SIZE / 4;
    matrix_multiply_blocked((Matrix*)A, (Matrix*)B, (Matrix*)C, core_id * row_per_core, (core_id + 1) * row_per_core);

    // Use global counter + lock for extra coherence
    for (int i = 0; i < 100; i++) {
        spin_lock((SpinLock*)shared_lock);
        (*global_counter)++;
        spin_unlock((SpinLock*)shared_lock);
        delay(10);
    }

    sync_flags[core_id] = 2;
    while (!(sync_flags[0] == 2 && sync_flags[1] == 2 && sync_flags[2] == 2 && sync_flags[3] == 2)) delay(100);

    if (core_id == 0) {
        printf("Core 0: Matrix C checksum: %d\n", matrix_checksum((Matrix*)C));
    }

    // Phase 2: Transpose
    if (core_id == 0) *phase = 2;
    while (*phase != 2) delay(100);

    matrix_transpose((Matrix*)C, (Matrix*)T, core_id * row_per_core, (core_id + 1) * row_per_core);

    // Periodically write to shared memory during transpose
    if (core_id % 2 == 0) {
        for (int i = 0; i < 20; i++) {
            *global_counter += 1;
            delay(5);
        }
    }

    sync_flags[core_id] = 3;
    while (!(sync_flags[0] == 3 && sync_flags[1] == 3 && sync_flags[2] == 3 && sync_flags[3] == 3)) delay(100);

    if (core_id == 0) {
        printf("Core 0: Matrix T checksum: %d\n", matrix_checksum((Matrix*)T));
    }

    // Phase 3: Matrix addition (split columns)
    if (core_id == 0) *phase = 3;
    while (*phase != 3) delay(100);

    int col_per_core = SIZE / 4;
    matrix_add((Matrix*)C, (Matrix*)T, (Matrix*)R, 0, SIZE, core_id * col_per_core, (core_id + 1) * col_per_core);

    // Random shared writes
    for (int i = 0; i < 50; i++) {
        *global_counter += core_id;
        delay(10);
    }

    sync_flags[core_id] = 4;
    while (!(sync_flags[0] == 4 && sync_flags[1] == 4 && sync_flags[2] == 4 && sync_flags[3] == 4)) delay(100);

    if (core_id == 0) {
        printf("Core 0: Matrix R checksum: %d\n", matrix_checksum((Matrix*)R));
        printf("Core 0: Final global counter value: %d\n", *global_counter);
        *phase = 4;
    }

    while (*phase != 4) delay(100);

    printf("Core %d: Test completed.\n", core_id);
    return 0;
}

// Initialize matrix
void matrix_init(Matrix* M, int pattern) {
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            switch (pattern) {
                case 1:  M->data[i][j] = i + j; break;
                case 2:  M->data[i][j] = (i * j) % 10; break;
                default: M->data[i][j] = 1; break;
            }
        }
    }
}

// Blocked multiply
void matrix_multiply_blocked(Matrix* A, Matrix* B, Matrix* C, int start_row, int end_row) {
    for (int i = start_row; i < end_row; i += BLOCK_SIZE) {
        for (int j = 0; j < SIZE; j += BLOCK_SIZE) {
            for (int k = 0; k < SIZE; k += BLOCK_SIZE) {
                for (int ii = i; ii < i + BLOCK_SIZE && ii < end_row; ii++) {
                    for (int jj = j; jj < j + BLOCK_SIZE && jj < SIZE; jj++) {
                        for (int kk = k; kk < k + BLOCK_SIZE && kk < SIZE; kk++) {
                            C->data[ii][jj] += A->data[ii][kk] * B->data[kk][jj];
                        }
                        delay(1);
                    }
                }
            }
        }
    }
}

// Transpose
void matrix_transpose(Matrix* A, Matrix* B, int start_row, int end_row) {
    for (int i = start_row; i < end_row; i++) {
        for (int j = 0; j < SIZE; j++) {
            B->data[j][i] = A->data[i][j];
            delay(1);
        }
    }
}

// Addition with column split
void matrix_add(Matrix* A, Matrix* B, Matrix* C, int start_row, int end_row, int col_start, int col_end) {
    for (int i = start_row; i < end_row; i++) {
        for (int j = col_start; j < col_end; j++) {
            C->data[i][j] = A->data[i][j] + B->data[i][j];
            delay(1);
        }
    }
}

// Checksum
int matrix_checksum(Matrix* M) {
    int sum = 0;
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            sum += M->data[i][j];
        }
    }
    return sum;
}