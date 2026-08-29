#include "../include/kernel.h"
#include "console.h"
#include "banner.h"
#include "../include/console_color.h"
#include "../include/block/device.h"
#include "../include/block/memdisk.h"
#include "../include/nbfs.h"
#include "../include/vfs/vfs.h"
#include "../include/vfs/path.h"
#include "../include/vfs/file.h"
#include "../shell/neoshell.h"

/*
 * Small NBFS root image embedded into the kernel .data section.
 * Produced by scripts/build-rootfs.sh and linked via rootfs.o.
 */
extern const unsigned char _binary_neobench_root_nbfs_start[];
extern const unsigned char _binary_neobench_root_nbfs_end[];

static memdisk_t root_disk;

static void kernel_self_check(void)
{
    vfs_path_t root_path;
    vfs_file_t file;
    char line[128];
    ssize_t n;
    size_t pos;

    console_write("\n  Root filesystem self-test:\n");

    vfs_path_init(&root_path, vfs_root());

    if (vfs_open(&root_path, "/docs/readme.txt", 0, &file) != 0)
    {
        console_write("  /docs/readme.txt .... ");
        console_write_color(NB_COLOR_RED, "[fail]\n");
        return;
    }

    console_write("  /docs/readme.txt .... ");
    console_write_color(NB_COLOR_GREEN, "[ok]\n");

    pos = 0;

    for (;;)
    {
        n = vfs_file_read(&file, line, sizeof(line) - 1);

        if (n <= 0)
            break;

        if (((size_t)n) + pos > sizeof(line) - 1)
            n = (ssize_t)(sizeof(line) - 1 - pos);

        pos += (size_t)n;
    }

    vfs_file_destroy(&file);
    vfs_path_destroy(&root_path);

    line[pos] = '\0';

    console_write("  Contents: \"");
    console_write(line);

    if ((pos == 0) || line[pos - 1] != '\n')
        console_write("\"");

    console_write("\n");
}

void kernel_main(const nb_bootinfo_t *boot)
{
    (void)boot;

    console_init();
    kernel_banner();
    kernel_module_loading();

    /* Serial console driver */
    kernel_module_begin("Serial console driver");
    kernel_module_ok();

    /* Memory backed root block device */
    kernel_module_begin("Block device layer");
    if (memdisk_attach(
            &root_disk,
            "neobench-root",
            _binary_neobench_root_nbfs_start,
            (uint64_t)(_binary_neobench_root_nbfs_end -
                       _binary_neobench_root_nbfs_start),
            4096) != 0)
    {
        kernel_module_fail();
        while (1)
            __asm__ volatile ("nop");
    }
    else
        kernel_module_ok();

    /* NBFS kernel layer */
    kernel_module_begin("NBFS filesystem module");
    if (nbfs_kernel_init() != 0)
        kernel_module_fail();
    else
        kernel_module_ok();

    /* VFS layer */
    kernel_module_begin("VFS filesystem layer");
    if (vfs_init() != 0)
        kernel_module_fail();
    else
        kernel_module_ok();

    /* NBFS probing root device */
    kernel_module_begin("NBFS probing root device");
    if (nbfs_kernel_probe(&root_disk.dev) != 0)
    {
        kernel_module_fail();
        while (1)
            __asm__ volatile ("nop");
    }
    else
        kernel_module_ok();

    /* NBFS mounting root filesystem */
    kernel_module_begin("NBFS mounting root filesystem");
    if (vfs_mount_root("nbfs", &root_disk.dev) != 0)
    {
        kernel_module_fail();
        while (1)
            __asm__ volatile ("nop");
    }
    else
        kernel_module_ok();

    /* Root "/" mount via VFS */
    kernel_module_begin("VFS mount root");
    if (vfs_root() == 0)
        kernel_module_fail();
    else
        kernel_module_ok();

    kernel_self_check();

    /* Process scheduler */
    kernel_module_begin("Process scheduler (4BSD)");
    kernel_module_warn();

    /* Network stack */
    kernel_module_begin("Network stack");
    kernel_module_fail();

    /* RTG framebuffer */
    kernel_module_begin("RTG framebuffer");
    kernel_module_warn();

    /* NeoBench init */
    kernel_module_begin("NeoBench init");
    kernel_module_ok();

    kernel_boot_complete();

    /* Enter NeoShell — does not return. */
    neoshell_run();

    /* Unreachable. */
    while (1)
    {
        __asm__ volatile ("nop");
    }
}