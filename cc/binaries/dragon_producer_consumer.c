// continuous_miss_dragon_test.c
// Benchmark designed to maintain high cache miss rates throughout the test
#include <stdio.h>
#include <stdlib.h>

// Simple delay function
void delay(int cycles) {
    for (volatile int i = 0; i < cycles; i++);
}

// Function to generate a pseudo-random index
// This ensures we don't fall into predictable access patterns
int get_random_index(int seed, int range) {
    // Simple linear congruential generator
    return (seed * 1103515245 + 12345) % range;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <core_id>\n", argv[0]);
        return 1;
    }

    int core_id = atoi(argv[1]);
    // Shared memory region at virtual address 0x8000
    volatile char* shmem_ptr = (volatile char*) (4096 * 8);
    
    printf("Core %d: Starting Dragon protocol continuous miss test\n", core_id);
    
    // Define memory layout
    // We'll use a large array to ensure we always exceed cache size
    #define ARRAY_SIZE 4096  // Larger than cache capacity
    #define SYNC_OFFSET 4100 // Beyond data array
    
    volatile char* data_array = shmem_ptr;                          // 0x8000: data array
    volatile int* ready_flag = (volatile int*)(shmem_ptr + SYNC_OFFSET);      // Ready flag
    volatile int* ack_flag = (volatile int*)(shmem_ptr + SYNC_OFFSET + 4);    // Ack flag
    volatile int* round_counter = (volatile int*)(shmem_ptr + SYNC_OFFSET + 8); // Round counter
    
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
    int rounds = 50;
    
    if (core_id == 0) {
        printf("Core 0: Producer starting\n");
        
        for (int round = 1; round <= rounds; round++) {
            printf("Core 0: Starting round %d\n", round);
            
            // Use large strides to guarantee cache misses
            // Using prime number stride to avoid cache set pattern matching
            int stride = 163; // Prime number stride
            int seed = round * 37;
            
            // Write to memory locations with large strides
            for (int i = 0; i < ARRAY_SIZE; i += stride) {
                // Generate a pseudo-random index based on current position
                int idx = get_random_index(seed + i, ARRAY_SIZE);
                
                // Update value
                data_array[idx] = (char)(round + i);
                
                // Small delay between writes
                delay(2);
                
                // Every few iterations, write to a previously accessed location
                // to create coherence conflicts
                if (i % 7 == 0 && i > 0) {
                    int prev_idx = get_random_index(seed + i - stride, ARRAY_SIZE);
                    data_array[prev_idx] = (char)(round + i + 1);
                }
            }
            
            // Signal data is ready
            *ready_flag = round;
            printf("Core 0: Set ready flag for round %d\n", round);
            
            // Wait for consumer to acknowledge
            printf("Core 0: Waiting for acknowledgment\n");
            while (*ack_flag != round) {
                delay(50);
                
                // Continue writing to random locations while waiting
                for (int j = 0; j < 20; j++) {
                    int idx = get_random_index(seed + round + j * 123, ARRAY_SIZE);
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
                
                // Use different access pattern than core 0 (different stride)
                // to ensure we're not just benefiting from core 0's prefetching
                int stride = 191; // Different prime number stride
                int seed = current_round * 41;
                int sum = 0;
                
                // Read and process data in a pattern designed to create misses
                for (int i = 0; i < ARRAY_SIZE; i += stride) {
                    // Generate a pseudo-random index
                    int idx = get_random_index(seed + i, ARRAY_SIZE);
                    
                    // Read data
                    sum += data_array[idx];
                    delay(2);
                    
                    // Every few iterations, modify a location
                    if (i % 5 == 0) {
                        // Choose a different location to modify
                        int mod_idx = get_random_index(seed + i + 123, ARRAY_SIZE);
                        data_array[mod_idx] = (char)(sum & 0xFF);
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
                    delay(50);
                    
                    // Continue accessing random locations while waiting
                    for (int j = 0; j < 20; j++) {
                        int idx = get_random_index(seed + j * 67, ARRAY_SIZE);
                        data_array[idx] = (char)(data_array[idx] + 1);
                    }
                }
            } else {
                // No new data yet, access random memory locations to create traffic
                int seed = last_round * 59;
                for (int i = 0; i < 30; i++) {
                    int idx = get_random_index(seed + i * 31, ARRAY_SIZE);
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