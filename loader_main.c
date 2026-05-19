void loader_main(void* bootinfo, void* memmap)
{
    (void)bootinfo;

    nbfs_index_t* index = (nbfs_index_t*)0x400000; 
    // loaded earlier from disk block 6

    uint32_t kernel_inode;

    if (!NBFS_Get(index, "kernel.main", &kernel_inode))
        panic("No kernel.main entry in NBFS");

    void* kernel_img = neofs_read_inode(kernel_inode);

    if (!is_elf(kernel_img))
        panic("Kernel not ELF");

    load_elf(kernel_img);

    neoboot_info_t info;
    info.magic = 0xNEO1;
    info.memmap_ptr = (uint32_t)memmap;
    info.cmdline_ptr = (uint32_t)"nbfs_boot=1";

    jump_to_kernel(get_entry(kernel_img), &info);
}
