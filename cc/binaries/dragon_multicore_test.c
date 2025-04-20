// dragon_multicore_test.c
#include <stdio.h>
#include <stdlib.h>

// Barrier implementation for synchronizing cores
volatile int barrier_count = 0;
volatile int barrier_sense = 0;

void barrier_wait(int num_cores) {
    // Simple barrier implementation
    int local_sense = !barrier_sense;
    
    __sync_fetch_and_add(&barrier_count, 1);
    
    if (barrier_count == num_cores) {
        barrier_count = 0;
        barrier_sense = local_sense;
    } else {
        while (barrier_sense != local_sense) {
            // Wait
            for (volatile int i = 0; i < 100; i++);
        }
    }
}

void delay(int cycles) {
    for (volatile int i = 0; i < cycles; i++);
}

int main(int argc, char** argv) {
    int core_id = atoi(argv[1]);
    int num_cores = argc > 2 ? atoi(argv[2]) : 4; // Default to 4 cores
    
    volatile char* shmem_ptr = (volatile char*) (4096 * 8);
    
    printf("Core %d: Starting multicore Dragon protocol test (num_cores = %d)\n", 
           core_id, num_cores);
    
    // Memory layout
    volatile char* data_array = &shmem_ptr[0];       // Main data area
    volatile char* barrier_area = &shmem_ptr[1000];  // Used for synchronization
    volatile char* result_area = &shmem_ptr[1100];   // Store results
    
    // Initialize memory (only core 0)
    if (core_id == 0) {
        printf("Core 0: Initializing shared memory\n");
        for (int i = 0; i < 1200; i++) {
            shmem_ptr[i] = 0;
        }
    }
    
    // Wait for initialization
    barrier_wait(num_cores);
    
    //=========================================================================
    // Test 1: Multiple Writer Test - Each core updates its own region
    //=========================================================================
    printf("Core %d: Starting Test 1 - Multiple Writers\n", core_id);
    
    // Each core writes to its own region
    int my_region_start = core_id * 16;
    
    for (int i = 0; i < 16; i++) {
        data_array[my_region_start + i] = (core_id * 10) + i;
    }
    
    // Synchronize after writing
    barrier_wait(num_cores);
    
    // Each core verifies another core's writes
    int check_core = (core_id + 1) % num_cores;
    int check_region_start = check_core * 16;
    int check_sum = 0;
    
    for (int i = 0; i < 16; i++) {
        check_sum += data_array[check_region_start + i];
    }
    
    printf("Core %d: Check sum of Core %d's region = %d\n", 
           core_id, check_core, check_sum);
    
    // Synchronize before next test
    barrier_wait(num_cores);
    
    //=========================================================================
    // Test 2: Producer-Consumer with Multiple Consumers
    //=========================================================================
    printf("Core %d: Starting Test 2 - Producer-Consumer with Multiple Consumers\n", core_id);
    
    // Core 0 is producer, others are consumers
    if (core_id == 0) {
        // Producer writes data for 5 rounds
        for (int round = 1; round <= 5; round++) {
            printf("Core 0: Producing data for round %d\n", round);
            
            // Write data
            for (int i = 0; i < 32; i++) {
                data_array[200 + i] = round * 10 + i;
            }
            
            // Signal round is ready
            barrier_area[0] = round;
            
            // Wait for all consumers to process
            while (barrier_area[1] < (num_cores - 1) * round) {
                delay(50);
            }
            
            printf("Core 0: All consumers processed round %d\n", round);
        }
    } else {
        // Consumer logic
        int last_round = 0;
        
        for (int round = 1; round <= 5; round++) {
            // Wait for producer to signal new data
            while (barrier_area[0] < round) {
                delay(50);
            }
            
            // Process data - each consumer calculates sum of a portion
            int segment_size = 32 / (num_cores - 1);
            int start_idx = 200 + ((core_id - 1) * segment_size);
            int end_idx = start_idx + segment_size;
            
            int sum = 0;
            for (int i = start_idx; i < end_idx; i++) {
                sum += data_array[i];
            }
            
            printf("Core %d: Round %d - processed segment sum = %d\n", 
                   core_id, round, sum);
            
            // Store result
            result_area[core_id] = sum;
            
            // Signal completion
            __sync_fetch_and_add(&barrier_area[1], 1);
        }
    }
    
    // Synchronize before next test
    barrier_wait(num_cores);
    
    //=========================================================================
    // Test 3: Moving Hotspot - One Location Updated by All Cores in Sequence
    //=========================================================================
    printf("Core %d: Starting Test 3 - Moving Hotspot\n", core_id);
    
    // Reset synchronization variables
    if (core_id == 0) {
        barrier_area[0] = 0;
        barrier_area[1] = 0;
    }
    
    barrier_wait(num_cores);
    
    // Cores take turns updating the same location
    for (int round = 0; round < 3; round++) {
        for (int active_core = 0; active_core < num_cores; active_core++) {
            if (core_id == active_core) {
                // This core's turn to update
                int old_value = data_array[400];
                data_array[400] = old_value + (core_id + 1) * 10;
                
                printf("Core %d: Updated hotspot from %d to %d\n", 
                       core_id, old_value, data_array[400]);
            }
            
            // Synchronize after each core's turn
            barrier_wait(num_cores);
            
            // All cores read the value
            int current_value = data_array[400];
            printf("Core %d: Round %d, after Core %d's update, value = %d\n", 
                   core_id, round, active_core, current_value);
            
            barrier_wait(num_cores);
        }
    }
    
    //=========================================================================
    // Test 4: Chain Update - Updates Propagate Through Cores
    //=========================================================================
    printf("Core %d: Starting Test 4 - Chain Update\n", core_id);
    
    // Reset synchronization
    barrier_wait(num_cores);
    
    // Initialize chain value
    if (core_id == 0) {
        data_array[500] = 1;
    }
    
    barrier_wait(num_cores);
    
    // Each core reads, updates, and then signals the next core
    for (int round = 0; round < 3; round++) {
        // Process in core_id order
        for (int step = 0; step < num_cores; step++) {
            if (core_id == step) {
                // Read current value
                int value = data_array[500];
                printf("Core %d: Round %d - Read chain value %d\n", 
                       core_id, round, value);
                
                // Update value
                data_array[500] = value * 2 + core_id;
                printf("Core %d: Round %d - Updated chain value to %d\n", 
                       core_id, round, data_array[500]);
            }
            
            // Synchronize to next core's turn
            barrier_wait(num_cores);
        }
    }
    
    //=========================================================================
    // Test 5: Cache Contention - Multiple Cores Update Same Cache Line
    //=========================================================================
    printf("Core %d: Starting Test 5 - Cache Contention\n", core_id);
    
    barrier_wait(num_cores);
    
    // Each core repeatedly updates its own offset within the same cache line
    // This should test how Dragon handles false sharing
    int my_offset = 600 + core_id;  // All within same or adjacent cache lines
    
    for (int i = 0; i < 100; i++) {
        // Update my location
        data_array[my_offset]++;
        
        if (i % 20 == 0) {
            printf("Core %d: Updated my location %d times, value=%d\n", 
                   core_id, i, data_array[my_offset]);
        }
    }
    
    // Final value check
    barrier_wait(num_cores);
    
    // Read and report all cores' final values
    for (int c = 0; c < num_cores; c++) {
        printf("Core %d: Final value for Core %d's location = %d\n", 
               core_id, c, data_array[600 + c]);
    }
    
    printf("Core %d: Completed all multicore tests\n", core_id);
    return 0;
}