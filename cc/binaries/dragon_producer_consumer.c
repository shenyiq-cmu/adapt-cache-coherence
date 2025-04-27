// dragon_producer_consumer.c
// A test designed to exercise the Dragon protocol's coherence states and transitions
#include <stdio.h>
#include <stdlib.h>

void delay(int cycles) {
    for (volatile int i = 0; i < cycles; i++);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <core_id>\n", argv[0]);
        return 1;
    }

    int core_id = atoi(argv[1]);
    // Shared memory region at virtual address 0x8000
    volatile char* shmem_ptr = (volatile char*) (4096 * 8);
    
    printf("Core %d: Starting Dragon protocol test\n", core_id);
    
    // Memory layout:
    // data_area: 10 bytes for data transfer (0x8000 - 0x800A)
    // ready_flag: 1 byte at 0x8064 to signal data is ready
    // ack_flag: 1 byte at 0x8065 to acknowledge processing
    volatile char* data_area = shmem_ptr;             // Address 0x8000
    volatile char* ready_flag = &shmem_ptr[100];      // Address 0x8064
    volatile char* ack_flag = &shmem_ptr[101];        // Address 0x8065
    
    // Core 0 is producer, Core 1 is consumer
    if (core_id == 0) {
        printf("Core 0: Producer starting\n");
        
        // Initialize flags
        *ready_flag = 0;
        *ack_flag = 0;
        
        // Run 5 rounds of production
        for (int round = 1; round <= 5; round++) {
            // Produce new data
            printf("Core 0: Producing data for round %d\n", round);
            
            // Write 10 values - this should transition cache lines to M state
            for (int i = 0; i < 10; i++) {
                data_area[i] = round * 10 + i;
                delay(10); // Small delay between writes
            }
            
            // Signal that new data is ready - this should cause a transition 
            // from M to either Sm or Sc when consumer reads
            printf("Core 0: Setting ready flag for round %d\n", round);
            *ready_flag = round;
            
            // Wait for consumer to acknowledge
            printf("Core 0: Waiting for consumer acknowledgment\n");
            while (*ack_flag != round) {
                delay(100);
            }
            
            printf("Core 0: Received acknowledgment for round %d\n", round);
            
            // Delay between rounds to allow for cache state transitions
            delay(1000);
        }
        
        printf("Core 0: Producer finished\n");
    } else {
        printf("Core 1: Consumer starting\n");
        
        int last_round = 0;
        int sum = 0;
        
        // Process 5 rounds of data
        while (last_round < 5) {
            // Check if new data is ready - this read should trigger BusRd
            int current_round = *ready_flag;
            
            if (current_round > last_round) {
                // New data is available - this should trigger cache state transitions
                printf("Core 1: Processing data for round %d\n", current_round);
                
                // Read and process the data - these reads should get data 
                // from the other cache via BusRd, causing flush if in M state
                for (int i = 0; i < 10; i++) {
                    sum += data_area[i];
                    printf("Core 1: Read data[%d] = %d\n", i, data_area[i]);
                    delay(10); // Small delay between reads
                }
                
                // Modify one value to test Sm/M transitions
                if (current_round == 3) {
                    printf("Core 1: Modifying data[5] to test coherence\n");
                    data_area[5] = 99; // This should cause a state transition
                }
                
                // Update the last processed round
                last_round = current_round;
                
                // Acknowledge processing - this write should trigger BusUpd
                printf("Core 1: Acknowledging round %d\n", current_round);
                *ack_flag = current_round;
            }
            
            delay(50);
        }
        
        printf("Core 1: Consumer finished, total sum = %d\n", sum);
    }
    
    printf("Core %d: Completed Dragon protocol test\n", core_id);
    return 0;
}