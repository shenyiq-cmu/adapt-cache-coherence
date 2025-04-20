#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    int core_id = atoi(argv[1]);
    volatile char* shmem_ptr = (volatile char*) (4096 * 8);
    
    // Core 0 is producer, others are consumers
    if (core_id == 0) {
        printf("Core %d: Producer starting\n", core_id);
        
        // Producer writes data to shared memory
        for (int round = 0; round < 10; round++) {
            // Write a batch of data
            for (int i = 0; i < 64; i++) {
                shmem_ptr[i] = (round * 10) + i;
            }
            
            // Signal that new data is available by updating a flag
            shmem_ptr[1024] = round + 1;
            
            // Wait a bit to let consumers work
            for (volatile int i = 0; i < 5000; i++);
        }
        
        printf("Core %d: Producer finished\n", core_id);
    } else {
        printf("Core %d: Consumer starting\n", core_id);
        
        int last_round = 0;
        int sum = 0;
        
        // Consumers read and process data
        while (last_round < 10) {
            // Check if new data is available
            int current_round = shmem_ptr[1024];
            
            if (current_round > last_round) {
                // Process new data - consume based on core_id to distribute work
                for (int i = (core_id - 1) * 16; i < (core_id) * 16; i++) {
                    sum += shmem_ptr[i];
                }
                
                // Update last seen round
                last_round = current_round;
                
                // Print progress
                printf("Core %d: Processed round %d, sum = %d\n", 
                       core_id, last_round, sum);
            }
        }
        
        printf("Core %d: Consumer finished, final sum = %d\n", core_id, sum);
    }
    
    return 0;
}