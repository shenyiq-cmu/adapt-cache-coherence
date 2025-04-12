#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    int offset = atoi(argv[1]);
    volatile char* shmem_ptr = (volatile char*) (4096 * 8);
    char data = 0;

    // // do a few writes
    // shmem_ptr[0] = 74 + 20*offset;
    // shmem_ptr[0] = 75 + 20*offset;

    // // read repeatedly for a while to let coherence settle
    // for (int i = 0; i < 5; i++) {
    //     data = shmem_ptr[0];
    // }

    // // knock S states out
    // shmem_ptr[0] = 76 + 20*offset;

    // // read repeatedly for a while to let coherence settle
    // for (int i = 0; i < 5; i++) {
    //     data = shmem_ptr[0];
    // }

    // printf("Core %d read %d\n", offset, data);
    data = shmem_ptr[0];
    printf("Core %d read %d\n", offset, data);
    return 0;
}