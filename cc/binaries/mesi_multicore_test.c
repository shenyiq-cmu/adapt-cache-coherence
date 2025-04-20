// binaries/mesi_multicore_test.c
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    int core_id = atoi(argv[1]);
    volatile char* shmem_ptr = (volatile char*) (4096 * 8);
    
    // Different patterns per core to test coherence
    if (core_id == 0) {
        // Core 0 writes initial values
        printf("Core %d: Writing initial values\n", core_id);
        for (int i = 0; i < 16; i++) {
            shmem_ptr[i*16] = i * 10;
        }
    } else if (core_id == 1) {
        // Core 1 reads some values, then modifies them
        for (volatile int i = 0; i < 5000; i++); // Wait a bit
        
        printf("Core %d: Reading values\n", core_id);
        for (int i = 0; i < 16; i += 2) {
            printf("Value at %d: %d\n", i*16, shmem_ptr[i*16]);
        }
        
        printf("Core %d: Modifying odd indices\n", core_id);
        for (int i = 1; i < 16; i += 2) {
            shmem_ptr[i*16] = i * 10 + 5;
        }
    } else if (core_id == 2) {
        // Core 2 reads everything after others have written
        for (volatile int i = 0; i < 10000; i++); // Wait longer
        
        printf("Core %d: Reading all values\n", core_id);
        for (int i = 0; i < 16; i++) {
            printf("Value at %d: %d\n", i*16, shmem_ptr[i*16]);
        }
        
        // Now write to even indices
        printf("Core %d: Modifying even indices\n", core_id);
        for (int i = 0; i < 16; i += 2) {
            shmem_ptr[i*16] = i * 10 + 2;
        }
    } else if (core_id == 3) {
        // Core 3 reads and modifies last
        for (volatile int i = 0; i < 15000; i++); // Wait longest
        
        printf("Core %d: Reading final values\n", core_id);
        for (int i = 0; i < 16; i++) {
            printf("Value at %d: %d\n", i*16, shmem_ptr[i*16]);
        }
        
        // Write to all indices
        printf("Core %d: Writing to all locations\n", core_id);
        for (int i = 0; i < 16; i++) {
            shmem_ptr[i*16] = i + 100;
        }
        
        // Read back to verify
        printf("Core %d: Final verification\n", core_id);
        for (int i = 0; i < 16; i++) {
            printf("Value at %d: %d\n", i*16, shmem_ptr[i*16]);
        }
    }
    
    return 0;
}