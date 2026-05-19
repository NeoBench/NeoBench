#include "disk.h"

extern int NBFS_Get(nbfs_index* idx, const char* name, uint32_t* out);

void boot_main(disk_t* disk)
{
    alignas(4096) uint8_t buf[4096];

    disk->ops->read(disk, 6, buf, 8);

    nbfs_index* idx = (nbfs_index*)buf;

    uint32_t kernel_inode;

    if (!NBFS_Get(idx, "kernel.main", &kernel_inode))
        while(1); // panic

    alignas(4096) uint8_t inode_block[4096];

    disk->ops->read(disk, 4 + (kernel_inode / 16), inode_block, 1);

    void* elf = inode_block;

    load_elf(elf);

    jump_to_kernel();
}
