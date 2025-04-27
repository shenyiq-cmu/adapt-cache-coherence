#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// Ticket lock structure
typedef struct {
    volatile unsigned int next_ticket;    // Ticket counter
    volatile unsigned int now_serving;    // Currently served ticket
} ticket_lock_t;

// Shared status array - frequently read by all cores
typedef struct {
    volatile unsigned int status[16];     // Status array (frequently read)
    volatile unsigned int counts[4];      // Per-core counter
} shared_data_t;

// Initialize the lock
void ticket_lock_init(ticket_lock_t *lock) {
    lock->next_ticket = 0;
    lock->now_serving = 0;
}

// Acquire the lock - simplified version that reduces shared counter accesses
unsigned int ticket_lock_acquire(ticket_lock_t *lock) {
    // Get my ticket
    unsigned int my_ticket = lock->next_ticket++;
    
    // Simple wait with minimal spinning
    if (lock->now_serving != my_ticket) {
        // This is more efficient - only check occasionally
        while (lock->now_serving != my_ticket) {
            // Shorter delay to create more bus traffic
            for (volatile int i = 0; i < 5; i++);
        }
    }
    
    return my_ticket;
}

// Release the lock
void ticket_lock_release(ticket_lock_t *lock) {
    lock->now_serving++;
}

int main(int argc, char** argv) {
    // Parse core ID
    if (argc < 2) {
        printf("Usage: %s <core_id>\n", argv[0]);
        return 1;
    }
    int core_id = atoi(argv[1]);
    
    // Shared memory region at 0x8000
    volatile ticket_lock_t *lock = (volatile ticket_lock_t*)(4096 * 8);
    
    // Shared data region - place at offset to avoid false sharing
    volatile shared_data_t *shared = (volatile shared_data_t*)(4096 * 8 + 64);
    
    // Local data - used to validate the counter
    volatile int *validation = (volatile int*)(4096 * 8 + 512);
    
    // Reduce iterations to create a more concise test
    int iterations = 50;
    
    // Initialize shared data (core 0 only)
    if (core_id == 0) {
        printf("Core 0: Initializing shared memory\n");
        
        // Initialize lock
        ticket_lock_init((ticket_lock_t*)lock);
        
        // Initialize shared data
        for (int i = 0; i < 16; i++) {
            shared->status[i] = 0;
        }
        
        for (int i = 0; i < 4; i++) {
            shared->counts[i] = 0;
        }
        
        // Initialize validation array
        for (int i = 0; i < 4; i++) {
            validation[i] = 0;
        }
        
        printf("Core 0: Initialization complete\n");
    }
    
    // Wait for initialization
    for (volatile int i = 0; i < 5000; i++);
    
    printf("Core %d: Starting ticket lock test\n", core_id);
    
    // Each core does the test
    for (int i = 0; i < iterations; i++) {
        // KEY DIFFERENCE: First read all status values frequently
        // This creates read sharing which favors update protocols
        for (int j = 0; j < 16; j++) {
            // Reading these values repeatedly creates coherence traffic
            // For invalidation protocols, this requires many invalidations
            // For update protocols, updates can be propagated efficiently
            volatile unsigned int status = shared->status[j];
            
            // Update my own status periodically (once per 4 reads)
            if (j % 4 == core_id) {
                shared->status[j]++;
            }
        }
        
        // Try to acquire the lock
        unsigned int my_ticket = ticket_lock_acquire((ticket_lock_t*)lock);
        
        // Critical section begins
        
        // Update shared status for this core
        shared->status[core_id] = i + 1;
        
        // Increment my counter in the shared array
        shared->counts[core_id]++;
        
        // Also validate locally
        validation[core_id]++;
        
        // Log activity occasionally
        if (i % 10 == 0) {
            printf("Core %d: Lock iteration %d (ticket %u)\n", 
                  core_id, i, my_ticket);
        }
        
        // KEY DIFFERENCE: Read other cores' status information
        // This creates more shared reads of potentially modified data
        for (int j = 0; j < 4; j++) {
            if (j != core_id) {
                volatile unsigned int other_count = shared->counts[j];
                volatile unsigned int other_status = shared->status[j];
                
                // Just access the values to create coherence traffic
                if (i % 10 == 0 && other_count > 0) {
                    printf("Core %d: Core %d status=%u count=%u\n", 
                           core_id, j, other_status, other_count);
                }
            }
        }
        
        // Critical section ends
        
        // Release the lock
        ticket_lock_release((ticket_lock_t*)lock);
        
        // Small delay between attempts - varied by core to avoid lockstep
        for (volatile int j = 0; j < core_id * 10 + 20; j++);
    }
    
    // Wait for all cores to finish
    for (volatile int i = 0; i < 10000; i++);
    
    // Final report
    printf("Core %d: Completed %d lock acquisitions. Count = %d, Validation = %d\n", 
           core_id, iterations, shared->counts[core_id], validation[core_id]);
    
    // Aggregate status (only core 0)
    if (core_id == 0) {
        printf("Final status values:\n");
        for (int i = 0; i < 16; i++) {
            printf("Status[%d] = %u\n", i, shared->status[i]);
        }
        
        int total_count = 0;
        for (int i = 0; i < 4; i++) {
            total_count += shared->counts[i];
            printf("Core %d count = %u\n", i, shared->counts[i]);
        }
        printf("Total iterations: %d\n", total_count);
    }
    
    return 0;
}