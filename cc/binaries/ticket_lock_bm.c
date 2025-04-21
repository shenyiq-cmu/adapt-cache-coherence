#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// Ticket lock structure
typedef struct {
    volatile unsigned int next_ticket;    // Shared counter for next available ticket
    volatile unsigned int now_serving;    // Shared counter for currently served ticket
} ticket_lock_t;

// Initialize the lock
void ticket_lock_init(ticket_lock_t *lock) {
    lock->next_ticket = 0;
    lock->now_serving = 0;
}

// Acquire the lock
unsigned int ticket_lock_acquire(ticket_lock_t *lock) {
    // Get my ticket - this would be an atomic fetch-and-increment in real implementation
    unsigned int my_ticket = lock->next_ticket++;
    
    // Wait until my ticket is served
    while (lock->now_serving != my_ticket) {
        // Busy wait - in a real implementation we might add a pause or yield here
        for (volatile int i = 0; i < 10; i++);
    }
    
    return my_ticket;
}

// Release the lock
void ticket_lock_release(ticket_lock_t *lock) {
    // Increment now_serving to let the next waiter proceed
    lock->now_serving++;
}

int main(int argc, char** argv) {
    int core_id = atoi(argv[1]);
    volatile ticket_lock_t *lock = (volatile ticket_lock_t*)(4096 * 8);
    volatile int *shared_counter = (volatile int*)(4096 * 8 + 64);
    volatile int *local_counts = (volatile int*)(4096 * 8 + 128);
    int iterations = 100;
    
    // Initialize shared data (core 0 only)
    if (core_id == 0) {
        ticket_lock_init((ticket_lock_t*)lock);
        *shared_counter = 0;
        for (int i = 0; i < 4; i++) {
            local_counts[i] = 0;
        }
    }
    
    // Wait for initialization
    for (volatile int i = 0; i < 1000; i++);
    
    printf("Core %d: Starting ticket lock test\n", core_id);
    
    // Each core acquires the lock, increments shared counter, and releases the lock
    for (int i = 0; i < iterations; i++) {
        // Try to acquire the lock
        unsigned int my_ticket = ticket_lock_acquire((ticket_lock_t*)lock);
        
        // Critical section begins
        
        // Read shared counter
        int temp = *shared_counter;
        
        // Small delay to increase chances of interference if lock is broken
        for (volatile int j = 0; j < 50; j++);
        
        // Increment shared counter
        *shared_counter = temp + 1;
        
        // Increment local count
        local_counts[core_id]++;
        
        // Log activity occasionally
        if (i % 10 == 0) {
            printf("Core %d: Acquired lock with ticket %u, incremented to %d\n", 
                  core_id, my_ticket, *shared_counter);
        }
        
        // Critical section ends
        
        // Release the lock
        ticket_lock_release((ticket_lock_t*)lock);
        
        // Small delay between attempts
        for (volatile int j = 0; j < core_id * 20 + 50; j++);
    }
    
    // Wait for all cores to finish
    for (volatile int i = 0; i < 10000; i++);
    
    // Final report
    printf("Core %d: Completed %d lock acquisitions. Local count = %d, Shared count = %d\n", 
           core_id, iterations, local_counts[core_id], *shared_counter);
    
    return 0;
}