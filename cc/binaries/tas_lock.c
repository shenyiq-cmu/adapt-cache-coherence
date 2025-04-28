#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

volatile int *lock = (volatile int*)(4096 * 8);

// Acquire the lock - simplified version that reduces shared counter accesses
void tas_lock_acquire() {
    // Get my ticket
    while (__sync_lock_test_and_set(lock, 1)) {
        // spin
    }
}

// Release the lock
void tas_lock_release() {
    __sync_lock_release(lock);
}

int main(int argc, char** argv) {
    // Parse core ID
    if (argc < 2) {
        printf("Usage: %s <core_id>\n", argv[0]);
        return 1;
    }
    int core_id = atoi(argv[1]);

    if(core_id == 0) *lock = 0;
    
    tas_lock_acquire();

    printf("Core %d holds the lock\n", core_id);
    
    for(int i = 0; i < 50; i++);

    tas_lock_release();
    
    return 0;
}