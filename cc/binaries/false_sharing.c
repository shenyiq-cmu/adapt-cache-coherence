#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    int core_id = atoi(argv[1]);
    volatile char* shmem_ptr = (volatile char*) (4096 * 8);
    
    // Each core updates its own array element, but they share cache lines
    // This should highlight the difference between invalidate vs update protocols
    
    // Clear shared area
    if (core_id == 0) {
        for (int i = 0; i < 1024; i++) {
            shmem_ptr[i] = 0;
        }
    }
    
    // Wait for initialization
    for (volatile int i = 0; i < 1000; i++);
    
    printf("Core %d: Starting false sharing test\n", core_id);
    
    // Calculate which element this core will access
    // Place cores on same cache line to force false sharing
    int my_offset = core_id * 16;
    int sum = 0;
    
    // Each core updates its element many times
    for (int i = 0; i < 1000; i++) {
        // Read
        sum += shmem_ptr[my_offset];
        
        // Write
        shmem_ptr[my_offset] = shmem_ptr[my_offset] + 1;
        
        // Every 100 iterations, read an element from other cores
        if (i % 100 == 0) {
            for (int j = 0; j < 4; j++) {
                if (j != core_id) {
                    sum += shmem_ptr[j * 16];
                }
            }
        }
    }
    
    printf("Core %d: Finished, final value = %d, sum = %d\n", 
           core_id, shmem_ptr[my_offset], sum);
    
    return 0;
}