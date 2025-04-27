// fixed_dragon_test.c
// Simplified benchmark that stays within memory bounds
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
    // Use a smaller, safe size to avoid page faults
    volatile char* shmem_ptr = (volatile char*) (4096 * 8);
    
    printf("Core %d: Starting Dragon protocol test\n", core_id);
    
    // Define memory layout with conservative sizes
    // Stay well within the 4KB page that's mapped
    #define ARRAY_SIZE 1024  // 1KB array size, safely within 4KB page
    
    volatile char* data_array = shmem_ptr;                    // 0x8000: data array
    volatile int* ready_flag = (volatile int*)(shmem_ptr + ARRAY_SIZE);    // Ready flag
    volatile int* ack_flag = (volatile int*)(shmem_ptr + ARRAY_SIZE + 4);  // Ack flag
    volatile int* round_counter = (volatile int*)(shmem_ptr + ARRAY_SIZE + 8); // Round counter
    
    // Initialize shared memory if core 0
    if (core_id == 0) {
        printf("Core 0: Initializing shared memory\n");
        for (int i = 0; i < ARRAY_SIZE; i++) {
            data_array[i] = (char)i;
        }
        *ready_flag = 0;
        *ack_flag = 0;
        *round_counter = 0;
    }
    
    // Number of rounds to run
    int rounds = 20;
    
    if (core_id == 0) {
        printf("Core 0: Producer starting\n");
        
        for (int round = 1; round <= rounds; round++) {
            printf("Core 0: Starting round %d\n", round);
            
            // Access the array with different patterns in each round
            // This should force high cache miss rates
            int step = (round % 5) + 1;  // Vary step size between rounds
            
            // Write to memory in a pattern designed to cause cache misses
            for (int i = 0; i < ARRAY_SIZE; i += step) {
                data_array[i] = (char)(round + i);
                
                // Every few iterations, modify a previously accessed location
                if (i > 0 && i % 16 == 0) {
                    int prev = (i - step) % ARRAY_SIZE;
                    data_array[prev] = (char)(round + prev + 1);
                }
                
                delay(5);
            }
            
            // Signal data is ready
            *ready_flag = round;
            printf("Core 0: Set ready flag for round %d\n", round);
            
            // Wait for consumer to acknowledge
            printf("Core 0: Waiting for acknowledgment\n");
            while (*ack_flag != round) {
                delay(50);
                
                // Continue writing to different locations while waiting
                for (int j = 0; j < 10; j++) {
                    int idx = (round * j + 7) % ARRAY_SIZE;
                    data_array[idx] = (char)(round + j + 50);
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
                
                // Use a different access pattern than core 0
                int step = (current_round % 4) + 2;  // Different step size
                int sum = 0;
                
                // Read and process data with a different pattern
                for (int i = 0; i < ARRAY_SIZE; i += step) {
                    sum += data_array[i];
                    
                    // Every few iterations, modify a location
                    if (i % 13 == 0) {
                        int mod_idx = (i + 11) % ARRAY_SIZE;
                        data_array[mod_idx] = (char)(current_round + mod_idx);
                    }
                    
                    delay(5);
                }
                
                printf("Core 1: Data checksum for round %d: %d\n", current_round, sum);
                
                // Update last processed round
                last_round = current_round;
                
                // Acknowledge processing
                printf("Core 1: Acknowledging round %d\n", current_round);
                *ack_flag = current_round;
                
                // Wait for round counter to be updated before proceeding
                while (*round_counter < current_round) {
                    delay(50);
                    
                    // Access different memory locations while waiting
                    for (int j = 0; j < 10; j++) {
                        int idx = (current_round * j + 13) % ARRAY_SIZE;
                        data_array[idx] = (char)(data_array[idx] + 1);
                    }
                }
            } else {
                // No new data yet, modify memory at different locations
                for (int i = 0; i < 5; i++) {
                    int idx = (last_round * i + 23) % ARRAY_SIZE;
                    data_array[idx] = (char)(data_array[idx] + 1);
                }
                
                delay(30);
            }
        }
        
        printf("Core 1: Consumer finished\n");
    }
    
    printf("Core %d: Completed Dragon protocol test\n", core_id);
    return 0;
}