
// Very simple matrix test for Dragon cache coherence protocol
#include <stdio.h>
#include <stdlib.h>

// Simple delay function
void delay(int cycles) {
    for (volatile int i = 0; i < cycles; i++);
}

// Define a very small matrix size to avoid any memory issues
#define SIZE 8

// Structure to hold a matrix in shared memory
typedef struct {
    int data[SIZE][SIZE];
} Matrix;

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <core_id>\n", argv[0]);
        return 1;
    }

    int core_id = atoi(argv[1]);
    
    // Shared memory region at virtual address 0x8000
    volatile void* shmem_ptr = (volatile void*) (4096 * 8);
    
    printf("Core %d: Starting Dragon minimal test\n", core_id);
    
    // Just use two matrices for simplicity
    volatile Matrix* A = (volatile Matrix*) shmem_ptr;                   // Input/output matrix
    volatile int* sync_done = (volatile int*) (A + 1);                   // Sync flag
    
    // Initialize if core 0
    if (core_id == 0) {
        // Initialize matrix with a simple pattern
        for (int i = 0; i < SIZE; i++) {
            for (int j = 0; j < SIZE; j++) {
                A->data[i][j] = i + j;
            }
        }
        
        // Initialize sync flag
        *sync_done = 0;
        
        printf("Core 0: Matrix initialized\n");
    }
    
    // Simple synchronization
    if (core_id == 0) {
        // Wait a bit to ensure Core 1 has started
        delay(1000);
        
        // Signal that initialization is done
        *sync_done = 1;
    } else {
        // Wait for initialization
        while (*sync_done == 0) {
            delay(10);
        }
    }
    
    printf("Core %d: Starting main operation\n", core_id);
    
    // Very simple test: each core updates half the matrix
    int start_row = (core_id == 0) ? 0 : SIZE/2;
    int end_row = (core_id == 0) ? SIZE/2 : SIZE;
    
    // Just do a simple operation to test basic coherence
    for (int i = start_row; i < end_row; i++) {
        for (int j = 0; j < SIZE; j++) {
            // Read value
            int val = A->data[i][j];
            
            // Perform a simple calculation
            val = val * 2 + 1;
            
            // Write back
            A->data[i][j] = val;
            
            // Small delay
            delay(5);
        }
    }
    
    // Signal completion (Core 0 writes first half of flag, Core 1 writes second half)
    if (core_id == 0) {
        *sync_done = 2;
        // Wait for Core 1 to finish
        while (*sync_done != 3) {
            delay(10);
        }
    } else {
        // Wait for Core 0 to signal it's done
        while (*sync_done != 2) {
            delay(10);
        }
        // Signal Core 1 is also done
        *sync_done = 3;
    }
    
    // Both cores compute checksum to verify coherence
    int checksum = 0;
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            checksum += A->data[i][j];
        }
    }
    
    printf("Core %d: Matrix checksum: %d\n", core_id, checksum);
    printf("Core %d: Test completed\n", core_id);
    
    return 0;
}