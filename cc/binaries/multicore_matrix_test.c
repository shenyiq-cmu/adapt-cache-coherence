// dragon_improved_multicore_test.c
// Improved multi-core test for Dragon protocol with safe synchronization
#include <stdio.h>
#include <stdlib.h>

// Simple delay function
void delay(int cycles) {
    for (volatile int i = 0; i < cycles; i++);
}

// Define maximum number of cores and array size
#define MAX_CORES 8
#define ARRAY_SIZE 32

// Safe synchronization function
int wait_for_value(volatile int *addr, int expected_value, int timeout_loops) {
    int timeout = 0;
    while (*addr != expected_value && timeout < timeout_loops) {
        delay(100);
        timeout++;
    }
    return (timeout < timeout_loops); // Return 1 if succeeded, 0 if timed out
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <core_id>\n", argv[0]);
        return 1;
    }

    int core_id = atoi(argv[1]);
    
    // Shared memory region at virtual address 0x8000
    volatile void* shmem_ptr = (volatile void*) (4096 * 8);
    
    printf("Core %d: Starting improved multi-core test\n", core_id);
    
    // Memory layout - compact and simple
    volatile int* data = (volatile int*) shmem_ptr;                // Data array [0-31]
    volatile int* num_cores = (volatile int*) (data + ARRAY_SIZE); // Number of cores [32]
    volatile int* phase = (volatile int*) (num_cores + 1);         // Current phase [33]
    volatile int* ready = (volatile int*) (phase + 1);             // Ready flags [34-41]
    volatile int* done = (volatile int*) (ready + MAX_CORES);      // Done flags [42-49]
    
    if (core_id == 0) {
        printf("Core 0: Initializing shared memory\n");
        
        // Clear data array
        for (int i = 0; i < ARRAY_SIZE; i++) {
            data[i] = 0;
        }
        
        // Set test parameters
        *num_cores = 4;  // Change to 8 for 8-core test
        *phase = 0;      // Starting phase
        
        // Clear all flags
        for (int i = 0; i < MAX_CORES; i++) {
            ready[i] = 0;
            done[i] = 0;
        }
        
        printf("Core 0: Initialization complete\n");
    }
    
    // Wait a moment for initialization
    delay(10000);
    
    // Check if we should participate
    if (core_id >= *num_cores) {
        printf("Core %d: Not needed for this test\n", core_id);
        return 0;
    }
    
    // Signal that this core is ready
    printf("Core %d: Signaling ready\n", core_id);
    ready[core_id] = 1;
    
    // Core 0 waits for all cores and starts the test
    if (core_id == 0) {
        // Wait for all cores to be ready
        for (int i = 1; i < *num_cores; i++) {
            int success = wait_for_value(&ready[i], 1, 100);
            if (!success) {
                printf("Core 0: Timeout waiting for core %d to be ready\n", i);
                // Continue anyway, core might catch up
            }
        }
        
        printf("Core 0: All cores ready, starting phase 1\n");
        *phase = 1;
    } else {
        // Wait for all cores to be ready and phase 1 to start
        int success = wait_for_value(phase, 1, 100);
        if (!success) {
            printf("Core %d: Timeout waiting for phase 1 to start\n", core_id);
            return 1;
        }
    }
    
    // ---------------------- PHASE 1: WRITE OWN DATA ----------------------
    printf("Core %d: Starting phase 1 - writing own data\n", core_id);
    
    // Calculate my section of the array
    int items_per_core = ARRAY_SIZE / *num_cores;
    int start_idx = core_id * items_per_core;
    int end_idx = (core_id == *num_cores-1) ? ARRAY_SIZE : start_idx + items_per_core;
    
    // Write to my section
    for (int i = start_idx; i < end_idx; i++) {
        data[i] = 100 * (core_id + 1) + (i - start_idx);
        // Small delay to create some interleaving
        if (i % 2 == 0) delay(20);
    }
    
    // Signal completion of phase 1
    printf("Core %d: Completed phase 1\n", core_id);
    done[core_id] = 1;
    
    // Core 0 coordinates transition to phase 2
    if (core_id == 0) {
        // Wait for all cores to complete phase 1
        for (int i = 1; i < *num_cores; i++) {
            int success = wait_for_value(&done[i], 1, 100);
            if (!success) {
                printf("Core 0: Timeout waiting for core %d to complete phase 1\n", i);
                // Continue anyway
            }
        }
        
        // Reset done flags
        for (int i = 0; i < *num_cores; i++) {
            done[i] = 0;
        }
        
        // Signal start of phase 2
        printf("Core 0: All cores completed phase 1, starting phase 2\n");
        __sync_synchronize(); // Memory barrier
        *phase = 2;
    } else {
        // Wait for phase 2 to start
        int success = wait_for_value(phase, 2, 100);
        if (!success) {
            printf("Core %d: Timeout waiting for phase 2 to start\n", core_id);
            return 1;
        }
    }
    
    // ---------------------- PHASE 2: READ ALL DATA ----------------------
    printf("Core %d: Starting phase 2 - reading all data\n", core_id);
    
    // Read all sections to generate coherence traffic
    int checksum = 0;
    for (int i = 0; i < ARRAY_SIZE; i++) {
        // Read the value
        int val = data[i];
        checksum += val;
        
        // Print some values to verify
        if (i % items_per_core == 0) {
            printf("Core %d: Read data[%d] = %d\n", core_id, i, val);
        }
        
        // Small delay to create some interleaving
        if (i % 3 == 0) delay(15);
    }
    
    printf("Core %d: Completed phase 2, checksum = %d\n", core_id, checksum);
    done[core_id] = 2;
    
    // Core 0 coordinates transition to phase 3
    if (core_id == 0) {
        // Wait for all cores to complete phase 2
        for (int i = 1; i < *num_cores; i++) {
            int success = wait_for_value(&done[i], 2, 100);
            if (!success) {
                printf("Core 0: Timeout waiting for core %d to complete phase 2\n", i);
                // Continue anyway
            }
        }
        
        // Reset done flags
        for (int i = 0; i < *num_cores; i++) {
            done[i] = 0;
        }
        
        // Signal start of phase 3
        printf("Core 0: All cores completed phase 2, starting phase 3\n");
        __sync_synchronize(); // Memory barrier
        *phase = 3;
    } else {
        // Wait for phase 3 to start
        int success = wait_for_value(phase, 3, 100);
        if (!success) {
            printf("Core %d: Timeout waiting for phase 3 to start\n", core_id);
            return 1;
        }
    }
    
    // ---------------------- PHASE 3: MODIFY OTHERS' DATA ----------------------
    printf("Core %d: Starting phase 3 - modifying other cores' data\n", core_id);
    
    // Determine which section to modify (the next core's section)
    int target_core = (core_id + 1) % *num_cores;
    int target_start = target_core * items_per_core;
    int target_end = (target_core == *num_cores-1) ? ARRAY_SIZE : target_start + items_per_core;
    
    printf("Core %d: Modifying core %d's section (indices %d-%d)\n", 
           core_id, target_core, target_start, target_end-1);
    
    // Modify the target section
    for (int i = target_start; i < target_end; i++) {
        // Read current value
        int current = data[i];
        
        // Modify it
        data[i] = current + 1000;
        
        // Print some values to verify
        if (i == target_start || i == target_end-1) {
            printf("Core %d: Modified data[%d] from %d to %d\n", 
                   core_id, i, current, data[i]);
        }
        
        // Small delay to create some interleaving
        if (i % 2 == 1) delay(25);
    }
    
    printf("Core %d: Completed phase 3\n", core_id);
    done[core_id] = 3;
    
    // Final synchronization
    if (core_id == 0) {
        // Wait for all cores to complete phase 3
        for (int i = 1; i < *num_cores; i++) {
            int success = wait_for_value(&done[i], 3, 100);
            if (!success) {
                printf("Core 0: Timeout waiting for core %d to complete phase 3\n", i);
                // Continue anyway
            }
        }
        
        // Signal completion
        printf("Core 0: All cores completed phase 3, test complete\n");
        *phase = 4;
    } else {
        // Wait for test completion
        int success = wait_for_value(phase, 4, 100);
        if (!success) {
            printf("Core %d: Timeout waiting for test completion\n", core_id);
        }
    }
    
    // One final read to verify results
    checksum = 0;
    for (int i = 0; i < ARRAY_SIZE; i++) {
        checksum += data[i];
    }
    
    printf("Core %d: Final checksum = %d, test completed\n", core_id, checksum);
    return 0;
}