// simple_multicore_test.c
#include <stdio.h>
#include <stdlib.h>

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
    volatile char* flags = &shmem_ptr[1000];         // Status flags
    
    // Initialize memory (only core 0)
    if (core_id == 0) {
        printf("Core %d: Initializing shared memory\n", core_id);
        for (int i = 0; i < 1200; i++) {
            shmem_ptr[i] = 0;
        }
    }
    
    // Staggered start based on core ID to ensure initialization completes
    delay(core_id * 5000);
    
    //=========================================================================
    // Test 1: Multiple Writer Test - Each core updates its own region
    //=========================================================================
    printf("Core %d: Starting Test 1 - Multiple Writers\n", core_id);
    
    // Each core writes to its own region
    int my_region_start = core_id * 16;
    
    for (int i = 0; i < 16; i++) {
        data_array[my_region_start + i] = (core_id * 10) + i;
        printf("Core %d: Wrote %d to address 0x%x\n", 
               core_id, data_array[my_region_start + i], 
               (unsigned int)(4096*8 + my_region_start + i));
    }
    
    // Wait for all cores to finish writing
    delay(20000);
    
    // Each core verifies another core's writes
    int check_core = (core_id + 1) % num_cores;
    int check_region_start = check_core * 16;
    int check_sum = 0;
    
    printf("Core %d: Reading Core %d's region\n", core_id, check_core);
    for (int i = 0; i < 16; i++) {
        check_sum += data_array[check_region_start + i];
        printf("Core %d: Read %d from address 0x%x\n", 
               core_id, data_array[check_region_start + i], 
               (unsigned int)(4096*8 + check_region_start + i));
    }
    
    printf("Core %d: Check sum of Core %d's region = %d\n", 
           core_id, check_core, check_sum);
    
    // Wait before next test
    delay(10000);
    
    //=========================================================================
    // Test 2: Hotspot - Multiple cores update same location
    //=========================================================================
    printf("Core %d: Starting Test 2 - Hotspot\n", core_id);
    
    // One location updated by all cores in sequence
    volatile char* hotspot = &data_array[200];
    
    // Cores update in turn based on ID
    delay(core_id * 5000);
    
    // Read before update
    int old_value = *hotspot;
    printf("Core %d: Hotspot before update = %d\n", core_id, old_value);
    
    // Update
    *hotspot = old_value + (core_id + 1) * 10;
    printf("Core %d: Updated hotspot to %d\n", core_id, *hotspot);
    
    // All cores wait for updates to complete
    delay(num_cores * 5000);
    
    // All cores read final value
    printf("Core %d: Final hotspot value = %d\n", core_id, *hotspot);
    
    // Wait before next test
    delay(10000);
    
    //=========================================================================
    // Test 3: False Sharing - Update different variables in same cache line
    //=========================================================================
    printf("Core %d: Starting Test 3 - False Sharing\n", core_id);
    
    // Each core repeatedly updates its own offset within the same cache line
    int my_offset = 300 + core_id;  // All within same cache line or adjacent lines
    
    printf("Core %d: Updating offset %d in cache line\n", core_id, my_offset);
    for (int i = 0; i < 50; i++) {
        // Update my location
        data_array[my_offset]++;
        
        if (i % 10 == 0) {
            printf("Core %d: Updated my location %d times, value=%d\n", 
                   core_id, i+1, data_array[my_offset]);
        }
        
        // Small delay between updates
        delay(100);
    }
    
    // Wait for all updates to complete
    delay(10000);
    
    // Read all cores' values
    for (int c = 0; c < num_cores; c++) {
        printf("Core %d: Final value for Core %d's location = %d\n", 
               core_id, c, data_array[300 + c]);
    }
    
    //=========================================================================
    // Test 4: Producer-Consumer Pattern
    //=========================================================================
    printf("Core %d: Starting Test 4 - Producer-Consumer\n", core_id);
    
    // Core 0 is producer, others are consumers
    if (core_id == 0) {
        // Produce 5 items
        for (int i = 0; i < 5; i++) {
            // Write the data
            data_array[400 + i] = 100 + i;
            printf("Core 0: Produced item %d with value %d\n", 
                   i, data_array[400 + i]);
            
            // Signal that an item is ready
            flags[i] = 1;
            
            // Wait a bit for consumers
            delay(5000);
        }
    } else {
        // Each consumer waits for different flag
        int my_item = (core_id - 1) % 5;
        
        // Wait for producer to signal
        printf("Core %d: Waiting for item %d\n", core_id, my_item);
        while (flags[my_item] == 0) {
            delay(100);
        }
        
        // Consume the item
        printf("Core %d: Consumed item %d with value %d\n", 
               core_id, my_item, data_array[400 + my_item]);
        
        // Acknowledge consumption
        flags[my_item + 5] = core_id;
    }
    
    // Wait for all consumer acknowledgments
    delay(10000);
    
    // Check acknowledgments
    if (core_id == 0) {
        for (int i = 0; i < 5; i++) {
            printf("Core 0: Item %d was consumed by Core %d\n", 
                   i, flags[i + 5]);
        }
    }
    
    printf("Core %d: Completed all multicore tests\n", core_id);
    return 0;
}