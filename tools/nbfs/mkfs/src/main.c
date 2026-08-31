#include <stdio.h>
#include <stdlib.h>

#include "mkfs.h"

int main(int argc, char **argv)
{
    uint64_t size_mb;
    uint64_t size_bytes;

    if (argc < 2)
    {
        printf("Usage:\n");
        printf("  mkfs.nbfs disk.nbfs [size_mb]\n");
        printf("  (default size 128 MB)\n");
        return 1;
    }

    if (argc >= 3)
    {
        size_mb = (uint64_t)strtoull(argv[2], NULL, 0);

        size_bytes = size_mb * 1024ULL * 1024ULL;

        return mkfs_create_ex(argv[1], size_bytes);
    }

    return mkfs_create(argv[1]);
}