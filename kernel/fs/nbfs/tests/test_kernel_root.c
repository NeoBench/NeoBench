/*
 * test_kernel_root.c
 *
 * Full kernel-path test for the NBFS root filesystem:
 *
 *     neobench-root.nbfs  ->  memdisk (kernel block device)
 *                         ->  nbfs_kernel_mount  (kernel NBFS driver)
 *                         ->  VFS root "/"
 *                         ->  path lookup + vfs_file_read
 *
 * This compiles the actual kernel files:
 *
 *     kernel/fs/nbfs/nbfs.c
 *     kernel/fs/block/memdisk.c
 *     kernel/fs/vfs/{vfs,vnode,path,file,filesystem,mount,dentry}.c
 *
 * on the host so the filesystem integration is proven outside the
 * emulator.  The image read here is the same image embedded into
 * kernel.elf for the boot path.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "block/memdisk.h"
#include "block/device.h"
#include "vfs/vfs.h"
#include "vfs/path.h"
#include "vfs/file.h"
#include "nbfs.h"

#define TEST_IMAGE "../../../../rootfs/boot/neobench-root.nbfs"

static memdisk_t disk;

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

static uint8_t *load_image(const char *path, uint64_t *bytes_out)
{
    FILE *fp;
    long size;
    uint8_t *buf;

    fp = fopen(path, "rb");

    if (!fp)
    {
        fprintf(stderr, "FAIL: cannot open %s\n", path);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
        return NULL;

    size = ftell(fp);

    if (size < 0)
        return NULL;

    if (fseek(fp, 0, SEEK_SET) != 0)
        return NULL;

    buf = (uint8_t *)malloc((size_t)size);

    if (!buf)
        return NULL;

    if (fread(buf, 1, (size_t)size, fp) != (size_t)size)
    {
        free(buf);
        return NULL;
    }

    fclose(fp);

    *bytes_out = (uint64_t)size;

    return buf;
}

static int read_and_check(
    const char *path,
    const char *expected,
    size_t expected_len)
{
    vfs_path_t root_path;
    vfs_file_t file;
    char buf[4096];
    ssize_t got;

    printf("  %-32s", path);

    if (vfs_path_init(&root_path, vfs_root()) != 0)
        return fail("vfs_path_init(root)");

    if (vfs_open(&root_path, path, 0, &file) != 0)
    {
        vfs_path_destroy(&root_path);
        return fail("vfs_open");
    }

    memset(buf, 0, sizeof(buf));

    got = vfs_file_read(&file, buf, sizeof(buf) - 1);

    vfs_file_destroy(&file);
    vfs_path_destroy(&root_path);

    if (got < 0)
        return fail("vfs_file_read");

    if ((size_t)got != expected_len)
    {
        fprintf(stderr, "FAIL: %s length %zd != %zu\n",
                path, got, expected_len);
        return 1;
    }

    if (memcmp(buf, expected, expected_len) != 0)
    {
        fprintf(stderr, "FAIL: %s content mismatch\n", path);
        return 1;
    }

    printf("ok (%zd bytes)\n", got);

    return 0;
}

int main(void)
{
    uint8_t *image;
    uint64_t image_bytes;
    block_device_t *dev;
    vfs_vnode_t *root;

    static const char docs_readme[] =
        "NBFS root filesystem integration complete.\n"
        "Superblock, inode table, directory lookup and extent\n"
        "reads are serviced by the kernel over a memory backed\n"
        "block device.\n";

    static const char motd[] =
        "Welcome to NeoBench alpha.\n"
        "The root filesystem is backed by NBFS.\n";

    printf("NeoBench kernel NBFS root test\n");
    printf("===============================\n");

    image = load_image(TEST_IMAGE, &image_bytes);

    if (!image)
        return fail("load root image");

    printf("PASS: loaded %s (%llu bytes)\n",
           TEST_IMAGE, (unsigned long long)image_bytes);

    if (memdisk_attach(
            &disk,
            "memdisk",
            image,
            image_bytes,
            4096) != 0)
    {
        free(image);
        return fail("memdisk_attach");
    }

    dev = memdisk_device(&disk);

    if (!dev || !dev->read)
    {
        free(image);
        return fail("memdisk_device");
    }

    printf("PASS: memory block device ready "
           "(%u-byte blocks, %llu blocks)\n",
           dev->block_size,
           (unsigned long long)dev->block_count);

    if (nbfs_kernel_init() != 0)
    {
        free(image);
        return fail("nbfs_kernel_init");
    }

    printf("PASS: kernel NBFS initialized\n");

    if (vfs_init() != 0)
    {
        free(image);
        return fail("vfs_init");
    }

    printf("PASS: VFS initialized\n");

    if (nbfs_kernel_probe(dev) != 0)
    {
        free(image);
        return fail("superblock probe");
    }

    printf("PASS: NBFS superblock probed\n");

    if (vfs_mount_root("nbk", dev) != 0)
    {
        free(image);
        return fail("vfs_mount_root");
    }

    printf("PASS: NBFS mounted at \"/\"\n");

    root = vfs_root();

    if (!root)
    {
        free(image);
        return fail("vfs_root");
    }

    printf("PASS: root vnode inode %llu\n",
           (unsigned long long)root->ino);

    if (read_and_check(
            "/README.txt",
            "NeoBench alpha\n"
            "===============\n"
            "\n"
            "A from-scratch 68060 operating system booting an NBFS\n"
            "root filesystem through VFS.\n"
            "\n"
            "Try  nbroot   root mount status\n"
            "     nbcat /etc/motd\n"
            "     nbcat /docs/readme.txt\n",
            197) != 0)
        return 1;

    if (read_and_check(
            "/etc/motd",
            motd,
            sizeof(motd) - 1) != 0)
        return 1;

    if (read_and_check(
            "/docs/readme.txt",
            docs_readme,
            sizeof(docs_readme) - 1) != 0)
        return 1;

    {
        vfs_path_t root_path;
        vfs_vnode_t miss;

        if (vfs_path_init(&root_path, root) != 0)
            return fail("path init (missing)");

        if (vfs_lookup(&root_path, "does-not-exist", &miss) == 0)
        {
            vfs_path_destroy(&root_path);
            return fail("lookup of missing name should fail");
        }

        vfs_path_destroy(&root_path);

        printf("PASS: missing name lookup rejected\n");
    }

    free(image);

    printf("\nKernel NBFS root test: OK\n");

    return 0;
}