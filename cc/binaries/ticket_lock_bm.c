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
    
    printf("Core %d: Starting ticket lock test with Dragon protocol\n", core_id);
    
    // Each core acquires the lock, increments shared counter, and releases the lock
    for (int i = 0; i < iterations; i++) {
        // Try to acquire the lock
        unsigned int my_ticket = ticket_lock_acquire((ticket_lock_t*)lock);
        
        // --- Critical section begins ---

        // First load shared counter
        int temp1 = *shared_counter;
        for (volatile int j = 0; j < 10; j++);

        // Load shared counter again to simulate contention
        int temp2 = *shared_counter;
        int combined = temp1 + temp2;

        // Write an intermediate value back to shared counter
        *shared_counter = combined / 2;
        for (volatile int j = 0; j < 10; j++);

        // Update my own local count
        local_counts[core_id] += 1;

        // False sharing: touch neighbors' counters (simulating wider critical section)
        if (core_id > 0) {
            local_counts[core_id - 1] += 1;
        }
        if (core_id < 3) {
            local_counts[core_id + 1] += 1;
        }

        // More updates to shared counter
        *shared_counter = *shared_counter + 1;
        for (volatile int j = 0; j < 5; j++);
        *shared_counter = *shared_counter + core_id;

        // Dummy reads to shared counter to generate more load traffic
        volatile int dummy1 = *shared_counter;
        volatile int dummy2 = *shared_counter;
        (void)dummy1;
        (void)dummy2;

        // False sharing: touch unrelated memory (simulate another array)
        volatile int *false_shared_area = (volatile int*)(4096 * 8 + 256);
        false_shared_area[core_id] += 1;
        false_shared_area[(core_id + 1) % 4] += 1;
        
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