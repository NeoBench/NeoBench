#include <stdio.h>
#include <nbfs/nbfs.h>

int main(void)
{
    printf("sizeof(nbfs_superblock_t) = %zu\n",
           sizeof(nbfs_superblock_t));
    return 0;
}
