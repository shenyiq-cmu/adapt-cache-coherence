#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    int offset = atoi(argv[1]);
    volatile char* shmem_ptr = (volatile char*) (4096 * 8);
    char data = 0;

    // scan-sum-like thing
    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++) {
            data += shmem_ptr[j*8 + offset*4 + i];
            shmem_ptr[j*8 + offset*4+i] = data;
        }
    }
    
    // fight!
    shmem_ptr[0] = data;

    // let the system settle
    for (int i = 0; i < 5; i++) {
        data = shmem_ptr[0];
    }

    printf("Core %d read %d\n", offset, data);
    return 0;
}