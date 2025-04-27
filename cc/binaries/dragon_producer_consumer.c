// complex_dragon_test.c
// Advanced test for Dragon protocol with multiple sharing patterns
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// Adjustable delay function
void delay(int cycles) {
    for (volatile int i = 0; i < cycles; i++);
}

// Different data access patterns to test various coherence transitions
typedef enum {
    PATTERN_ALTERNATE,      // Cores alternate writing to the same location
    PATTERN_PRODUCER_CONSUMER, // Traditional producer-consumer
    PATTERN_MIGRATORY,      // Data migrates from one core to another
    PATTERN_READ_MOSTLY,    // Mostly reads with occasional writes
    PATTERN_FALSE_SHARING   // Writes to different data in same cache line
} AccessPattern;

// Struct to encourage false sharing
typedef struct {
    char data[64];          // Sized to match a cache line
} CacheLine;

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <core_id>\n", argv[0]);
        return 1;
    }

    int core_id = atoi(argv[1]);
    // Shared memory region at virtual address 0x8000
    volatile char* shmem_base = (volatile char*) (4096 * 8);
    
    printf("Core %d: Starting complex Dragon protocol test\n", core_id);
    
    // Memory layout with multiple regions to test different patterns
    // Each region is 64 bytes (cache line sized) to test alignment effects
    volatile char* region1 = shmem_base;                  // 0x8000-0x803F: Producer-consumer data
    volatile CacheLine* region2 = (CacheLine*)(shmem_base + 64);  // 0x8040-0x807F: Alternating writes
    volatile CacheLine* region3 = (CacheLine*)(shmem_base + 128); // 0x8080-0x80BF: Migratory data
    volatile CacheLine* region4 = (CacheLine*)(shmem_base + 192); // 0x80C0-0x80FF: Read-mostly data
    volatile CacheLine* region5 = (CacheLine*)(shmem_base + 256); // 0x8100-0x813F: False sharing
    
    // Control & synchronization variables
    volatile int* current_phase = (volatile int*)(shmem_base + 320);    // 0x8140
    volatile int* sync_counter = (volatile int*)(shmem_base + 324);     // 0x8144
    volatile int* active_pattern = (volatile int*)(shmem_base + 328);   // 0x8148
    volatile bool* core0_ready = (volatile bool*)(shmem_base + 332);    // 0x814C
    volatile bool* core1_ready = (volatile bool*)(shmem_base + 333);    // 0x814D
    volatile int* results = (volatile int*)(shmem_base + 336);          // 0x8150 (array of results)
    
    // Initialize shared control variables if core 0
    if (core_id == 0) {
        *current_phase = 0;
        *sync_counter = 0;
        *active_pattern = PATTERN_PRODUCER_CONSUMER;
        *core0_ready = false;
        *core1_ready = false;
        
        // Initialize all regions
        for (int i = 0; i < 64; i++) {
            region1[i] = 0;
            region2->data[i] = 0;
            region3->data[i] = 0;
            region4->data[i] = 0;
            region5->data[i] = 0;
        }
        
        // Initialize results array
        for (int i = 0; i < 10; i++) {
            results[i] = 0;
        }
    }
    
    // Make sure both cores are here before starting
    if (core_id == 0) {
        *core0_ready = true;
        while (!(*core1_ready)) {
            delay(100);
        }
    } else {
        *core1_ready = true;
        while (!(*core0_ready)) {
            delay(100);
        }
    }
    
    printf("Core %d: Initialization complete, starting tests\n", core_id);
    
    // Run through different access patterns
    for (int pattern = PATTERN_PRODUCER_CONSUMER; pattern <= PATTERN_FALSE_SHARING; pattern++) {
        if (core_id == 0) {
            // Set the active pattern and reset phase
            *active_pattern = pattern;
            *current_phase = 0;
            *sync_counter = 0;
            
            printf("Core 0: Starting pattern %d\n", pattern);
        } else {
            // Wait for core 0 to set the pattern
            while (*active_pattern != pattern) {
                delay(50);
            }
            printf("Core 1: Starting pattern %d\n", pattern);
        }
        
        // Both cores execute the selected pattern
        switch (*active_pattern) {
            case PATTERN_PRODUCER_CONSUMER:
                // Traditional producer-consumer pattern
                if (core_id == 0) {  // Producer
                    // Run 3 rounds of production
                    for (int round = 1; round <= 3; round++) {
                        // Produce new data
                        printf("Core 0: Producing data for round %d\n", round);
                        
                        // Write data sequentially - transitions to E→M
                        for (int i = 0; i < 16; i++) {
                            region1[i] = round * 10 + i;
                            delay(5);
                        }
                        
                        // Signal data is ready - consumer will read causing M→Sm
                        *current_phase = round;
                        
                        // Wait for consumer to process
                        while (*sync_counter < round) {
                            delay(100);
                        }
                        
                        // Read back a value modified by consumer - tests Sc→Sm
                        int val = region1[8];
                        printf("Core 0: Read back modified value: %d\n", val);
                        
                        // Modify the same value again - tests concurrent modifications
                        region1[8] = val + 100;
                        printf("Core 0: Modified shared value again: %d\n", val + 100);
                        
                        delay(500);
                    }
                } else {  // Consumer
                    int last_phase = 0;
                    
                    while (last_phase < 3) {
                        int current = *current_phase;
                        
                        if (current > last_phase) {
                            printf("Core 1: Consuming data for phase %d\n", current);
                            
                            int sum = 0;
                            // Read data - may trigger BusRd and state changes
                            for (int i = 0; i < 16; i++) {
                                sum += region1[i];
                                delay(5);
                            }
                            
                            // Modify one value - tests Sc→Sm transition
                            region1[8] = current * 100;
                            printf("Core 1: Modified data[8] to %d\n", current * 100);
                            
                            // Read the value again to verify local cache state
                            int val = region1[8];
                            printf("Core 1: Read back my modified value: %d\n", val);
                            
                            // Store result
                            results[current-1] = sum;
                            
                            // Signal completion
                            *sync_counter = current;
                            last_phase = current;
                        }
                        
                        delay(50);
                    }
                }
                break;
                
            case PATTERN_ALTERNATE:
                // Cores alternate writing to the same location
                // This tests rapid transitions between M and Sm states
                if (core_id == 0) {
                    for (int i = 0; i < 10; i++) {
                        // Write to even indices
                        for (int j = 0; j < 32; j += 2) {
                            region2->data[j] = i + j;
                            delay(5);
                        }
                        
                        // Signal completion of this round
                        *current_phase = i + 1;
                        
                        // Wait for core 1 to complete its round
                        while (*sync_counter < i + 1) {
                            delay(50);
                        }
                        
                        // Read values written by core 1 to odd indices
                        int sum = 0;
                        for (int j = 1; j < 32; j += 2) {
                            sum += region2->data[j];
                            delay(5);
                        }
                        
                        printf("Core 0: Sum of odd indices = %d\n", sum);
                        results[i] = sum;
                    }
                } else {
                    for (int i = 0; i < 10; i++) {
                        // Wait for core 0 to complete its round
                        while (*current_phase <= i) {
                            delay(50);
                        }
                        
                        // Write to odd indices
                        for (int j = 1; j < 32; j += 2) {
                            region2->data[j] = i * 10 + j;
                            delay(5);
                        }
                        
                        // Read values written by core 0 to even indices
                        int sum = 0;
                        for (int j = 0; j < 32; j += 2) {
                            sum += region2->data[j];
                            delay(5);
                        }
                        
                        printf("Core 1: Sum of even indices = %d\n", sum);
                        
                        // Signal completion
                        *sync_counter = i + 1;
                    }
                }
                break;
                
            case PATTERN_MIGRATORY:
                // Migratory access pattern:
                // Data ownership repeatedly transfers between cores
                // Tests frequent M→I→M transitions
                if (core_id == 0) {
                    // Initialize the migratory data
                    for (int i = 0; i < 64; i++) {
                        region3->data[i] = i;
                    }
                    
                    // Signal initialization complete
                    *current_phase = 1;
                    
                    for (int round = 0; round < 5; round++) {
                        // Wait for core 1 to process
                        while (*sync_counter <= round * 2) {
                            delay(50);
                        }
                        
                        printf("Core 0: Processing migratory data, round %d\n", round);
                        
                        // Read and modify all data
                        int sum = 0;
                        for (int i = 0; i < 64; i++) {
                            sum += region3->data[i];
                            region3->data[i] += 2;  // Increment by 2
                            delay(2);
                        }
                        
                        printf("Core 0: Migratory data sum = %d\n", sum);
                        results[round] = sum;
                        
                        // Signal completion
                        *sync_counter = round * 2 + 2;
                    }
                } else {
                    // Wait for initialization
                    while (*current_phase < 1) {
                        delay(50);
                    }
                    
                    for (int round = 0; round < 5; round++) {
                        printf("Core 1: Processing migratory data, round %d\n", round);
                        
                        // Read and modify all data
                        int sum = 0;
                        for (int i = 0; i < 64; i++) {
                            sum += region3->data[i];
                            region3->data[i] += 3;  // Increment by 3
                            delay(2);
                        }
                        
                        printf("Core 1: Migratory data sum = %d\n", sum);
                        
                        // Signal completion
                        *sync_counter = round * 2 + 1;
                        
                        // Wait for core 0 to process
                        while (*sync_counter <= round * 2 + 1) {
                            delay(50);
                        }
                    }
                }
                break;
                
            case PATTERN_READ_MOSTLY:
                // Read-mostly pattern: multiple reads with occasional writes
                // Tests E→Sc→M transition sequences
                if (core_id == 0) {
                    // Initialize the read-mostly data
                    for (int i = 0; i < 64; i++) {
                        region4->data[i] = i * 2;
                    }
                    
                    // Signal initialization complete
                    *current_phase = 1;
                    
                    // Multiple rounds of mostly reading with occasional writes
                    for (int round = 0; round < 3; round++) {
                        int reads = 0;
                        int sum = 0;
                        
                        // Do many reads with occasional writes
                        for (int iter = 0; iter < 20; iter++) {
                            // Mostly reads
                            for (int i = 0; i < 64; i++) {
                                sum += region4->data[i];
                                reads++;
                                delay(1);
                            }
                            
                            // Occasional write
                            if (iter % 5 == 0) {
                                int idx = iter % 64;
                                region4->data[idx] = round * 100 + idx;
                                printf("Core 0: Modified index %d to %d\n", idx, round * 100 + idx);
                            }
                            
                            // Let other core get some work done
                            delay(50);
                        }
                        
                        printf("Core 0: Read-mostly round %d, reads=%d, sum=%d\n", 
                               round, reads, sum);
                        
                        // Signal round complete
                        *current_phase = round + 2;
                        
                        // Wait for core 1 to complete
                        while (*sync_counter < round + 1) {
                            delay(50);
                        }
                    }
                } else {
                    // Wait for initialization
                    while (*current_phase < 1) {
                        delay(50);
                    }
                    
                    // Multiple rounds of mostly reading with occasional writes
                    for (int round = 0; round < 3; round++) {
                        // Wait for core 0 to start the round
                        while (*current_phase < round + 1) {
                            delay(50);
                        }
                        
                        int reads = 0;
                        int sum = 0;
                        
                        // Do many reads with occasional writes
                        for (int iter = 0; iter < 15; iter++) {
                            // Mostly reads
                            for (int i = 0; i < 64; i++) {
                                sum += region4->data[i];
                                reads++;
                                delay(1);
                            }
                            
                            // Occasional write
                            if (iter % 5 == 0) {
                                int idx = (iter + 32) % 64;  // Different indices than core 0
                                region4->data[idx] = round * 200 + idx;
                                printf("Core 1: Modified index %d to %d\n", idx, round * 200 + idx);
                            }
                            
                            // Let other core get some work done
                            delay(30);
                        }
                        
                        printf("Core 1: Read-mostly round %d, reads=%d, sum=%d\n", 
                               round, reads, sum);
                        
                        // Signal round complete
                        *sync_counter = round + 1;
                    }
                }
                break;
                
            case PATTERN_FALSE_SHARING:
                // False sharing: cores access different data in same cache line
                // This is particularly interesting for Dragon protocol
                if (core_id == 0) {
                    // Initialize the false sharing region
                    for (int i = 0; i < 64; i++) {
                        region5->data[i] = 0;
                    }
                    
                    // Signal initialization complete
                    *current_phase = 1;
                    
                    // Wait for core 1 to be ready
                    while (*sync_counter < 1) {
                        delay(50);
                    }
                    
                    // Repeatedly write to first half of the cache line
                    for (int round = 0; round < 5; round++) {
                        printf("Core 0: False sharing round %d\n", round);
                        
                        for (int iter = 0; iter < 20; iter++) {
                            // Write to first half (indices 0-31)
                            for (int i = 0; i < 32; i++) {
                                region5->data[i] = round * 100 + i + iter;
                                delay(2);
                            }
                            
                            // Occasionally read from second half
                            if (iter % 5 == 0) {
                                int sum = 0;
                                for (int i = 32; i < 64; i++) {
                                    sum += region5->data[i];
                                }
                                printf("Core 0: Sum of second half = %d\n", sum);
                                results[round] = sum;
                            }
                        }
                        
                        // Signal round complete
                        *current_phase = round + 2;
                        
                        // Wait for core 1 to complete
                        while (*sync_counter < round + 2) {
                            delay(50);
                        }
                    }
                } else {
                    // Wait for initialization
                    while (*current_phase < 1) {
                        delay(50);
                    }
                    
                    // Signal ready
                    *sync_counter = 1;
                    
                    // Repeatedly write to second half of the cache line
                    for (int round = 0; round < 5; round++) {
                        // Wait for core 0 to start the round
                        while (*current_phase < round + 1) {
                            delay(50);
                        }
                        
                        printf("Core 1: False sharing round %d\n", round);
                        
                        for (int iter = 0; iter < 20; iter++) {
                            // Write to second half (indices 32-63)
                            for (int i = 32; i < 64; i++) {
                                region5->data[i] = round * 200 + i + iter;
                                delay(2);
                            }
                            
                            // Occasionally read from first half
                            if (iter % 5 == 0) {
                                int sum = 0;
                                for (int i = 0; i < 32; i++) {
                                    sum += region5->data[i];
                                }
                                printf("Core 1: Sum of first half = %d\n", sum);
                            }
                        }
                        
                        // Signal round complete
                        *sync_counter = round + 2;
                    }
                }
                break;
        }
        
        // Synchronize before moving to next pattern
        if (core_id == 0) {
            // Reset synchronization variables
            *current_phase = 0;
            *sync_counter = 0;
            
            // Print results for this pattern
            printf("Core 0: Pattern %d completed. Results: ", pattern);
            for (int i = 0; i < 5; i++) {
                printf("%d ", results[i]);
            }
            printf("\n");
            
            // Wait a bit before starting next pattern
            delay(1000);
        } else {
            // Wait for core 0 to complete processing and reset
            while (*current_phase != 0 || *sync_counter != 0) {
                delay(100);
            }
            printf("Core 1: Pattern %d completed\n", pattern);
        }
    }
    
    printf("Core %d: All Dragon protocol tests completed\n", core_id);
    return 0;
}