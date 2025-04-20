// dragon_eviction_test.c
#include <stdio.h>
#include <stdlib.h>

void delay(int cycles) {
    for (volatile int i = 0; i < cycles; i++);
}

int main(int argc, char** argv) {
    int core_id = atoi(argv[1]);
    volatile char* shmem_ptr = (volatile char*) (4096 * 8);
    
    printf("Core %d: Starting cache eviction test\n", core_id);
    
    if (core_id == 0) {
        // First, initialize some values
        printf("Core 0: Initializing first set of cache lines\n");
        for (int i = 0; i < 8; i++) {
            shmem_ptr[i*32] = i;
            printf("Core 0: Wrote %d to address 0x%x\n", i, (unsigned int)(4096*8 + i*32));
            printf("Core 0: Address 0x%x = %d\n", (unsigned int)(4096*8 + i*32), shmem_ptr[i*32]);
        }
        
        // Let Core 1 read some values
        delay(5000);
        
        // Now write many more values to cause evictions
        printf("Core 0: Writing many values to force evictions\n");
        for (int i = 8; i < 40; i++) {
            shmem_ptr[i*32] = i;
            
            // Print progress every 8 writes
            if (i % 8 == 0) {
                printf("Core 0: Wrote batch ending at address 0x%x\n", 
                       (unsigned int)(4096*8 + i*32));
            }
        }
        
        // Read back some early values that should have been evicted and reloaded
        printf("Core 0: Reading back early values (should have been evicted)\n");
        for (int i = 0; i < 8; i += 2) {
            printf("Core 0: Address 0x%x = %d\n", 
                   (unsigned int)(4096*8 + i*32), shmem_ptr[i*32]);
        }
    } else {
        // Wait for initial values to be set
        delay(2000);
        
        // Read and modify some values to get them in shared state
        printf("Core 1: Reading and modifying initial values\n");
        for (int i = 0; i < 8; i += 2) {
            // Read to get SC state
            char val = shmem_ptr[i*32];
            printf("Core 1: Read address 0x%x = %d\n", 
                   (unsigned int)(4096*8 + i*32), val);
            
            // Modify to get SM state
            shmem_ptr[i*32] = i + 100;
            printf("Core 1: Modified address 0x%x to %d\n", 
                   (unsigned int)(4096*8 + i*32), i + 100);
        }
        
        // Wait for Core 0 to evict these lines
        delay(10000);
        
        // Read back same values to check if writebacks happened correctly
        printf("Core 1: Reading back values after eviction\n");
        for (int i = 0; i < 8; i += 2) {
            printf("Core 1: Address 0x%x = %d\n", 
                   (unsigned int)(4096*8 + i*32), shmem_ptr[i*32]);
        }
        
        // Read some of Core 0's later writes to verify they're available
        printf("Core 1: Reading some of Core 0's later writes\n");
        for (int i = 8; i < 40; i += 8) {
            printf("Core 1: Address 0x%x = %d\n", 
                   (unsigned int)(4096*8 + i*32), shmem_ptr[i*32]);
        }
    }
    
    printf("Core %d: Completed cache eviction test\n", core_id);
    return 0;
}