#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

volatile int *lock = (volatile int*)(4096 * 8);

void delay(int cycles) {
    for (volatile int i = 0; i < cycles; i++);
}

// Atomic test-and-set operation
int test_and_set(volatile int *lock) {
    int old = *lock;
    *lock = 1;
    return old;
}


// Acquire the lock - simplified version that reduces shared counter accesses
void tas_lock_acquire() {
    // Get my ticket
    while (test_and_set(lock)) {
        // spin
    }
}

// Release the lock
void tas_lock_release() {
    *lock = 0;
}

int main(int argc, char** argv) {
    // Parse core ID
    if (argc < 2) {
        printf("Usage: %s <core_id>\n", argv[0]);
        return 1;
    }
    int core_id = atoi(argv[1]);

    delay(core_id * 10);

    if(core_id == 0) *lock = 0;
    
    tas_lock_acquire();

    printf("Core %d holds the lock\n", core_id);

    delay(500);

    tas_lock_release();
    
    return 0;
}