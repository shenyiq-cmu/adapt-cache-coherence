#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// N=2

// Test data at fixed addresses
volatile char* test_data1 = (volatile char*)(4096 * 8 + 0x20);
volatile char* test_data2 = (volatile char*)(4096 * 8 + 0x40);

// Replace usleep with busy-wait
void busy_wait(int iterations) {
    for (volatile int i = 0; i < iterations; i++);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <core_id>\n", argv[0]);
        return 1;
    }
    
    int core_id = atoi(argv[1]);
    
    printf("Core %d starting hybrid test\n", core_id);
    fflush(stdout);
    
    // Test 1: Sc → Sm transition with BusUpd
    // Both cores read the same address to establish Sc state
    char data = *test_data1;
    printf("Core %d initial read: %d\n", core_id, data);
    fflush(stdout);
    
    // Delay to ensure both cores have performed the read
    busy_wait(10000);
    
    if (core_id == 0) {
        // part 1:
        // Core 0 writes to the shared line - Sc→Sm transition with BusUpd
        *test_data1 = 55;
        printf("Core %d wrote 55\n", core_id);
        fflush(stdout);
        *test_data1 = 55;
        *test_data1 = 54;
        *test_data1 = 55;
        *test_data1 = 54;
        *test_data1 = 56;
        
        // Delay to let Core 1 invalidate the value
        busy_wait(15000);

        // part 3:
        data = *test_data1;
        printf("Core %d read %d after Core 1's Update\n", core_id, data);
        fflush(stdout);
        busy_wait(15000);
        // part 5
        data = *test_data1;
        printf("Core %d read %d after Core 0's BusRdx, should rdmiss\n", core_id, data);

        
    } else {
        // Longer delay to ensure Core 0 writes first
        busy_wait(5000);
        
        // part 2:
        // Read to verify what Core 0 wrote - should be updated via BusUpd
        data = *test_data1;
        printf("Core %d read %d after Core 0's BusRdx, should rdmiss\n", core_id, data);
        fflush(stdout);

        *test_data1 = 57;
        printf("Core %d wrote 57, should update core 0\n", core_id);

        *test_data1 = 56;
        *test_data1 = 57;

        busy_wait(15000);
        // part 4:
        *test_data1 = 56;
        *test_data1 = 57;
        *test_data1 = 56;
        *test_data1 = 57;
        *test_data1 = 56;
        *test_data1 = 58;
    }
    
    
    return 0;
}