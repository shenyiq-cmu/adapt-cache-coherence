// dragon_producer_consumer.c
// Modified benchmark for Dragon cache coherence protocol
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
    
    printf("Core %d: Starting Dragon protocol test\n", core_id);
    
    // Memory layout - using two separate data regions to test different sharing patterns
    #define BUFFER_SIZE 64 
    #define METADATA_SIZE 16
    
    // First buffer for regular producer-consumer pattern
    volatile char* primary_buffer = shmem_ptr;                     // 0x8000
    // Second buffer for simultaneous read-write activities
    volatile char* secondary_buffer = shmem_ptr + BUFFER_SIZE;     // 0x8040
    // Metadata region for flags and counters
    volatile int* ready_flag = (volatile int*)(shmem_ptr + 2*BUFFER_SIZE); // 0x8080
    volatile int* ack_flag = (volatile int*)(shmem_ptr + 2*BUFFER_SIZE + 4); // 0x8084
    volatile int* shared_counter = (volatile int*)(shmem_ptr + 2*BUFFER_SIZE + 8); // 0x8088
    volatile int* mode_flag = (volatile int*)(shmem_ptr + 2*BUFFER_SIZE + 12); // 0x808C
    
    // Initialize shared memory if core 0
    if (core_id == 0) {
        printf("Core 0: Initializing shared memory\n");
        
        // Initialize both buffers
        for (int i = 0; i < BUFFER_SIZE; i++) {
            primary_buffer[i] = 0;
            secondary_buffer[i] = 0;
        }
        
        // Initialize metadata
        *ready_flag = 0;
        *ack_flag = 0;
        *shared_counter = 0;
        *mode_flag = 0; // 0=normal, 1=concurrent mode
    }
    
    // Test parameters
    int rounds = 10;
    
    if (core_id == 0) {
        printf("Core 0: Producer starting\n");
        
        for (int round = 1; round <= rounds; round++) {
            printf("Core 0: Starting round %d\n", round);
            
            // Alternate between normal producer-consumer and concurrent access modes
            *mode_flag = (round % 2); // Toggle between modes
            
            if (*mode_flag == 0) {
                printf("Core 0: Normal producer-consumer mode\n");
                
                // Fill the primary buffer with data
                for (int i = 0; i < BUFFER_SIZE; i++) {
                    primary_buffer[i] = (char)(round * 10 + i);
                    delay(10);
                }
                
                // Signal that data is ready
                *ready_flag = round;
                printf("Core 0: Primary buffer filled, ready flag set\n");
                
                // Wait for consumer to process
                printf("Core 0: Waiting for consumer acknowledgment\n");
                while (*ack_flag != round) {
                    delay(100);
                }
                
                printf("Core 0: Received acknowledgment for round %d\n", round);
            } 
            else {
                printf("Core 0: Concurrent access mode\n");
                
                // Signal start of concurrent mode
                *ready_flag = round;
                
                // Wait for consumer to be ready for concurrent mode
                while (*ack_flag != round) {
                    delay(50);
                }
                
                // Both cores now simultaneously access the secondary buffer
                printf("Core 0: Starting concurrent access to secondary buffer\n");
                
                // Core 0 writes to even indices while reading odd indices
                for (int iter = 0; iter < 5; iter++) {
                    // Write to even indices
                    for (int i = 0; i < BUFFER_SIZE; i += 2) {
                        secondary_buffer[i] = (char)(round * 10 + i + iter);
                        delay(5);
                    }
                    
                    // Read from odd indices (written by Core 1)
                    int sum = 0;
                    for (int i = 1; i < BUFFER_SIZE; i += 2) {
                        sum += secondary_buffer[i];
                        delay(5);
                    }
                    
                    printf("Core 0: Iteration %d, sum of odd indices: %d\n", iter, sum);
                    
                    // Update shared counter to synchronize with Core 1
                    (*shared_counter)++;
                    
                    // Wait for Core 1 to complete its iteration
                    int expected = (*shared_counter) + 1;
                    while (*shared_counter < expected) {
                        delay(20);
                    }
                }
                
                printf("Core 0: Completed concurrent access\n");
                
                // Signal completion of concurrent mode
                *ready_flag = round + 100;
                
                // Wait for consumer to acknowledge
                while (*ack_flag != round + 100) {
                    delay(50);
                }
            }
            
            // Short delay between rounds
            delay(500);
        }
        
        printf("Core 0: Producer finished\n");
    } 
    else {
        printf("Core 1: Consumer starting\n");
        
        int last_round = 0;
        
        while (last_round < rounds) {
            // Check if new data or new mode is ready
            int current_round = *ready_flag;
            
            if (current_round > last_round) {
                if (current_round > 100) {
                    // This is an end-of-concurrent-mode signal
                    printf("Core 1: Acknowledging end of concurrent mode\n");
                    *ack_flag = current_round;
                    last_round = current_round - 100;
                    continue;
                }
                
                // Check which mode we're in
                if (*mode_flag == 0) {
                    // Normal producer-consumer mode
                    printf("Core 1: Processing primary buffer for round %d\n", current_round);
                    
                    // Read and verify data from the primary buffer
                    int sum = 0;
                    for (int i = 0; i < BUFFER_SIZE; i++) {
                        char expected = (char)(current_round * 10 + i);
                        char actual = primary_buffer[i];
                        
                        if (expected != actual) {
                            printf("Core 1: Data verification error at index %d! Expected %d, got %d\n", 
                                   i, expected, actual);
                        }
                        
                        sum += actual;
                        delay(10);
                    }
                    
                    printf("Core 1: Buffer checksum: %d\n", sum);
                    
                    // Modify some values to test coherence
                    for (int i = 0; i < BUFFER_SIZE; i += 8) {
                        primary_buffer[i] = (char)(current_round * 20 + i);
                        delay(5);
                    }
                    
                    // Acknowledge processing
                    printf("Core 1: Acknowledging round %d\n", current_round);
                    *ack_flag = current_round;
                    last_round = current_round;
                }
                else {
                    // Concurrent access mode
                    printf("Core 1: Entering concurrent mode for round %d\n", current_round);
                    
                    // Acknowledge ready for concurrent mode
                    *ack_flag = current_round;
                    
                    // Core 1 writes to odd indices while reading even indices
                    for (int iter = 0; iter < 5; iter++) {
                        // Write to odd indices
                        for (int i = 1; i < BUFFER_SIZE; i += 2) {
                            secondary_buffer[i] = (char)(current_round * 10 + i + iter);
                            delay(5);
                        }
                        
                        // Read from even indices (written by Core 0)
                        int sum = 0;
                        for (int i = 0; i < BUFFER_SIZE; i += 2) {
                            sum += secondary_buffer[i];
                            delay(5);
                        }
                        
                        printf("Core 1: Iteration %d, sum of even indices: %d\n", iter, sum);
                        
                        // Update shared counter to synchronize with Core 0
                        (*shared_counter)++;
                        
                        // Wait for Core 0 to complete its iteration
                        int expected = (*shared_counter) + 1;
                        while (*shared_counter < expected) {
                            delay(20);
                        }
                    }
                    
                    printf("Core 1: Completed concurrent mode processing\n");
                    
                    // Don't update last_round yet - wait for the end signal
                }
            } else {
                delay(50);
            }
        }
        
        printf("Core 1: Consumer finished\n");
    }
    
    printf("Core %d: Completed Dragon protocol test\n", core_id);
    return 0;
}