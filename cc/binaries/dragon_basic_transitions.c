// dragon_basic_transitions.c
#include <stdio.h>
#include <stdlib.h>

// Simulates a delay to let operations complete
void delay(int cycles) {
    for (volatile int i = 0; i < cycles; i++);
}

int main(int argc, char** argv) {
    int core_id = atoi(argv[1]);
    volatile char* shmem_ptr = (volatile char*) (4096 * 8);
    
    printf("Core %d: Starting Dragon basic state transition test\n", core_id);
    
    // Test basic E->M->SC->SM transitions
    if (core_id == 0) {
        // Initial read should get Exclusive state
        printf("Core 0: Reading address 0x8000 (expecting state: Exclusive)\n");
        char val = shmem_ptr[0];
        printf("Core 0: Read value = %d\n", val);
        
        // Write to move to Modified
        printf("Core 0: Writing to address 0x8000 (transition: Exclusive -> Modified)\n");
        shmem_ptr[0] = 42;
        printf("Core 0: Wrote value 42\n");
        
        // Delay to let Core 1 read
        delay(5000);
        
        // Read again to confirm state
        printf("Core 0: Reading address 0x8000 (expected state: Shared Clean)\n");
        val = shmem_ptr[0];
        printf("Core 0: Read value = %d\n", val);
        
        // Write again - this should cause BusUpd
        printf("Core 0: Writing to address 0x8000 (transition: SC -> SM with BusUpd)\n");
        shmem_ptr[0] = 100;
        printf("Core 0: Wrote value 100\n");
        
        // Final read to verify state
        delay(5000);
        val = shmem_ptr[0];
        printf("Core 0: Final read value = %d\n", val);
    } else {
        // Core 1 waits to let Core 0 get into Modified state
        delay(3000);
        
        // Reading when Core 0 is Modified should cause M->SC transition with bus flush
        printf("Core 1: Reading address 0x8000 (causes Core 0: M->SC)\n");
        char val = shmem_ptr[0];
        printf("Core 1: Read value = %d (should be 42)\n", val);
        
        // Delay to let Core 0 write again
        delay(5000);
        
        // Read again to see the updated value via BusUpd
        printf("Core 1: Reading after Core 0 write (updated via BusUpd)\n");
        val = shmem_ptr[0];
        printf("Core 1: Read value = %d (should be 100)\n", val);
        
        // Now write to cause SC->SM transition
        printf("Core 1: Writing to address 0x8000 (transition: SC -> SM with BusUpd)\n");
        shmem_ptr[0] = 150;
        printf("Core 1: Wrote value 150\n");
    }
    
    printf("Core %d: Completed basic state transition test\n", core_id);
    return 0;
}