// binaries/mesi_eviction_test.c
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    int core_id = atoi(argv[1]);
    volatile char* shmem_ptr = (volatile char*) (4096 * 8);
    
    // Use different memory patterns to force evictions
    // With a small cache, this should cause evictions
    if (core_id == 0) {
        printf("Core %d: Writing initial data\n", core_id);
        
        // Write to several cache lines to fill cache
        for (int i = 0; i < 32; i++) {
            shmem_ptr[i*32] = i;
        }
        
        // Now read back some values to verify
        printf("Core %d: Reading back values\n", core_id);
        for (int i = 0; i < 32; i += 4) {
            printf("Value at %d: %d\n", i*32, shmem_ptr[i*32]);
        }
        
        // Write to some more addresses to cause evictions of modified lines
        printf("Core %d: Writing more data to cause evictions\n", core_id);
        for (int i = 32; i < 64; i++) {
            shmem_ptr[i*32] = i;
        }
        
        // Read back again to verify writeback happened correctly
        printf("Core %d: Reading values after eviction\n", core_id);
        for (int i = 0; i < 64; i += 8) {
            printf("Value at %d: %d\n", i*32, shmem_ptr[i*32]);
        }
    } else {
        // Core 1 waits, then reads values to see if writebacks happened
        for (volatile int i = 0; i < 10000; i++);
        
        printf("Core %d: Reading values written by core 0\n", core_id);
        for (int i = 0; i < 64; i += 8) {
            printf("Value at %d: %d\n", i*32, shmem_ptr[i*32]);
        }
        
        // Write to some of the same lines, forcing shared->modified transitions
        printf("Core %d: Writing to shared lines\n", core_id);
        for (int i = 0; i < 64; i += 16) {
            shmem_ptr[i*32] = i + 100;
        }
        
        // Verify writes
        printf("Core %d: Reading back after writes\n", core_id);
        for (int i = 0; i < 64; i += 16) {
            printf("Value at %d: %d\n", i*32, shmem_ptr[i*32]);
        }
    }
    
    return 0;
}