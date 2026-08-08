#include <stdio.h>

#include "mkfs.h"

int main(int argc,char **argv)
{
    if(argc<2)
    {
        printf("Usage:\n");
        printf("  mkfs.nbfs disk.nbfs\n");
        return 1;
    }

    return mkfs_create(argv[1]);
}
