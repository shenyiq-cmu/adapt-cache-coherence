// simple_traffic_dragon_test.c
// Simple benchmark to generate high bus traffic for Dragon protocol
#include <stdio.h>
#include <stdlib.h>

// Simple delay function
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
    
    printf("Core %d: Starting Dragon protocol traffic test\n", core_id);
    
    // Set up shared memory regions
    // We'll use 8 cache lines, 64 bytes each
    volatile char* data_region = shmem_ptr;                   // 0x8000: shared data
    volatile int* ready_flag = (volatile int*)(shmem_ptr + 512);  // 0x8200: ready flag
    volatile int* ack_flag = (volatile int*)(shmem_ptr + 516);    // 0x8204: ack flag
    volatile int* round_counter = (volatile int*)(shmem_ptr + 520); // 0x8208: round counter
    
    // Initialize shared memory if core 0
    if (core_id == 0) {
        printf("Core 0: Initializing shared memory\n");
        for (int i = 0; i < 512; i++) {
            data_region[i] = 0;
        }
        *ready_flag = 0;
        *ack_flag = 0;
        *round_counter = 0;
    }
    
    // Simple ping-pong test with 20 rounds
    int rounds = 20;
    
    if (core_id == 0) {
        printf("Core 0: Producer starting\n");
        
        for (int round = 1; round <= rounds; round++) {
            printf("Core 0: Starting round %d\n", round);
            
            // Update entire data region
            for (int i = 0; i < 512; i++) {
                data_region[i] = (char)(round + i);
                delay(2); // Small delay between writes
            }
            
            // Signal data is ready
            *ready_flag = round;
            printf("Core 0: Set ready flag for round %d\n", round);
            
            // Wait for consumer to acknowledge
            printf("Core 0: Waiting for acknowledgment\n");
            while (*ack_flag != round) {
                delay(100);
                
                // Randomly update some values while waiting
                // This creates more coherence traffic
                for (int j = 0; j < 5; j++) {
                    int addr = (round * j) % 512;
                    data_region[addr] += 10;
                }
            }
            
            printf("Core 0: Received acknowledgment for round %d\n", round);
            
            // Update round counter
            *round_counter = round;
        }
        
        printf("Core 0: Producer finished\n");
    } else {
        printf("Core 1: Consumer starting\n");
        
        int last_round = 0;
        
        while (last_round < rounds) {
            // Check if new data is ready
            int current_round = *ready_flag;
            
            if (current_round > last_round) {
                printf("Core 1: Processing data for round %d\n", current_round);
                
                // Read and process all data
                int sum = 0;
                for (int i = 0; i < 512; i++) {
                    sum += data_region[i];
                    delay(2); // Small delay between reads
                    
                    // Modify data periodically to create more coherence traffic
                    if (i % 16 == 0) {
                        data_region[i] = (char)(sum & 0xFF);
                    }
                }
                
                printf("Core 1: Data checksum for round %d: %d\n", current_round, sum);
                
                // Update last processed round
                last_round = current_round;
                
                // Acknowledge processing
                printf("Core 1: Acknowledging round %d\n", current_round);
                *ack_flag = current_round;
                
                // Wait for round counter to be updated before proceeding
                while (*round_counter < current_round) {
                    delay(100);
                    
                    // Randomly update values while waiting
                    for (int j = 0; j < 5; j++) {
                        int addr = (current_round * j + 256) % 512;
                        data_region[addr] += 5;
                    }
                }
            } else {
                // No new data yet, read and modify some data to create traffic
                for (int i = 0; i < 10; i++) {
                    int addr = (last_round * i + 128) % 512;
                    data_region[addr] = (char)(data_region[addr] + 1);
                }
                
                delay(50);
            }
        }
        
        printf("Core 1: Consumer finished\n");
    }
    
    printf("Core %d: Completed Dragon protocol test\n", core_id);
    return 0;
}