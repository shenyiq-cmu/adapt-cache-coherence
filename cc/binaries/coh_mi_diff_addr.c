#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    int offset = atoi(argv[1]);
    volatile char* shmem_ptr = (volatile char*) (4096 * 8 + offset);
    shmem_ptr[0] = 74 + 20*offset;
    shmem_ptr[0] = 75 + 20*offset;
    printf("Core %d read %d\n", offset, shmem_ptr[0]);
    return 0;
}