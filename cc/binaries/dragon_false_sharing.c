// dragon_false_sharing.c
#include <stdio.h>
#include <stdlib.h>

void delay(int cycles) {
    for (volatile int i = 0; i < cycles; i++);
}

int main(int argc, char** argv) {
    int core_id = atoi(argv[1]);
    volatile char* shmem_ptr = (volatile char*) (4096 * 8);
    
    printf("Core %d: Starting false sharing test\n", core_id);
    
    // Initialize memory if core 0
    if (core_id == 0) {
        for (int i = 0; i < 64; i++) {
            shmem_ptr[i] = 0;
        }
    }
    
    // Wait for initialization
    delay(1000);
    
    // Each core updates its own variables that share a cache line
    // Dragon should handle this better than MESI due to updates
    
    // Variables are placed at core_id offset within the same cache line
    int my_offset = core_id * 8; // Still within the same 32-byte cache line
    int sum = 0;
    
    printf("Core %d: Starting updates at offset %d\n", core_id, my_offset);
    
    // Perform many updates to cause false sharing
    for (int i = 0; i < 500; i++) {
        // Read current value
        sum += shmem_ptr[my_offset];
        
        // Update value
        shmem_ptr[my_offset]++;
        
        // Every 100 iterations, read values from other core
        if (i % 100 == 0) {
            int other_offset = ((core_id + 1) % 2) * 8;
            int other_val = shmem_ptr[other_offset];
            
            printf("Core %d: Iteration %d, my value = %d, other value = %d\n", 
                   core_id, i, shmem_ptr[my_offset], other_val);
        }
    }
    
    printf("Core %d: Finished false sharing test, final value = %d, sum = %d\n", 
           core_id, shmem_ptr[my_offset], sum);
    
    return 0;
}