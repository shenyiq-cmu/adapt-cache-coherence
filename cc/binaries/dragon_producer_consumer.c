// dragon_producer_consumer.c
#include <stdio.h>
#include <stdlib.h>

void delay(int cycles) {
    for (volatile int i = 0; i < cycles; i++);
}

int main(int argc, char** argv) {
    int core_id = atoi(argv[1]);
    volatile char* shmem_ptr = (volatile char*) (4096 * 8);
    
    printf("Core %d: Starting producer-consumer test\n", core_id);
    
    // Use separate memory locations for data and flags
    volatile char* data_area = shmem_ptr;             // Address 0x8000
    volatile char* ready_flag = &shmem_ptr[100];      // Address 0x8064
    volatile char* ack_flag = &shmem_ptr[101];        // Address 0x8065
    
    // Core 0 is producer, Core 1 is consumer
    if (core_id == 0) {
        printf("Core 0: Producer starting\n");
        
        // Run 5 rounds of production
        for (int round = 1; round <= 5; round++) {
            // Produce new data
            printf("Core 0: Producing data for round %d\n", round);
            
            // Write 10 values
            for (int i = 0; i < 10; i++) {
                data_area[i] = round * 10 + i;
            }
            
            // Signal that new data is ready
            printf("Core 0: Setting ready flag for round %d\n", round);
            *ready_flag = round;
            
            // Wait for consumer to acknowledge
            printf("Core 0: Waiting for consumer acknowledgment\n");
            while (*ack_flag != round) {
                delay(100);
            }
            
            printf("Core 0: Received acknowledgment for round %d\n", round);
        }
        
        printf("Core 0: Producer finished\n");
    } else {
        printf("Core 1: Consumer starting\n");
        
        int last_round = 0;
        int sum = 0;
        
        // Process 5 rounds of data
        while (last_round < 5) {
            // Check if new data is ready
            int current_round = *ready_flag;
            
            if (current_round > last_round) {
                // New data is available
                printf("Core 1: Processing data for round %d\n", current_round);
                
                // Read and process the data
                for (int i = 0; i < 10; i++) {
                    sum += data_area[i];
                    printf("Core 1: Read data[%d] = %d\n", i, data_area[i]);
                }
                
                // Update the last processed round
                last_round = current_round;
                
                // Acknowledge processing
                printf("Core 1: Acknowledging round %d\n", current_round);
                *ack_flag = current_round;
            }
            
            delay(50);
        }
        
        printf("Core 1: Consumer finished, total sum = %d\n", sum);
    }
    
    printf("Core %d: Completed producer-consumer test\n", core_id);
    return 0;
}