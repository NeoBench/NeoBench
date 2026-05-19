#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* NeoBench Universal Installer v2.1
 * Formats NeoFS and Installs Bootloader + Kernel 
 */

typedef void * BPTR;
extern BPTR Open(char *, long);
extern long Write(BPTR, void *, long);
extern long Read(BPTR, void *, long);
extern void Close(BPTR);
extern long Seek(BPTR, long, long);
extern void * AllocVec(unsigned long, unsigned long);
extern void FreeVec(void *);
extern long IoErr(void);
extern long Inhibit(char *, long);

#define MODE_READWRITE 1004
#define MODE_OLDFILE 1005
#define OFFSET_BEGINNING -1
#define MEMF_PUBLIC (1L<<0)
#define MEMF_CLEAR (1L<<16)

/* NeoFS Superblock Struct */
struct Superblock {
    unsigned int magic;
    unsigned int version;
    unsigned int block_size;
    unsigned int total_blocks;
    unsigned int free_blocks;
    unsigned int total_inodes;
    unsigned int free_inodes;
    unsigned int inode_table_start;
    unsigned int inode_table_blocks;
    unsigned int data_start;
    char volume_name[32];
    unsigned int crc32;
};

unsigned int ComputeCRC32C(const unsigned char* data, int length) {
    unsigned int crc = 0xFFFFFFFFUL;
    int i, b;
    for (i = 0; i < length; ++i) {
        crc ^= data[i];
        for (b = 0; b < 8; ++b) {
            if (crc & 1) crc = (crc >> 1) ^ 0x82F63B78UL;
            else crc >>= 1;
        }
    }
    return ~crc;
}

int main(int argc, char **argv) {
    BPTR disk_fh = (BPTR)0;
    BPTR file_fh = (BPTR)0;
    unsigned char *buffer = (unsigned char *)0;
    char *dev_name;
    struct Superblock *sb;

    printf("NeoBench Universal Installer v2.1\n");

    if (argc < 4) {
        printf("Usage: NeoInstall_Amiga [Device:] [Kernel] [Bootblock]\n");
        return 10;
    }

    dev_name = argv[1];

    /* 1. Inhibit and Open */
    printf("Accessing %s...\n", dev_name);
    Inhibit(dev_name, -1L);
    disk_fh = Open(dev_name, MODE_READWRITE);
    if (!disk_fh) {
        printf("Error: Could not open %s (IoErr: %ld)\n", dev_name, IoErr());
        Inhibit(dev_name, 0L);
        return 20;
    }

    /* 2. Format NeoFS */
    printf("Formatting %s as NeoFS...\n", dev_name);
    buffer = AllocVec(4096, MEMF_PUBLIC | MEMF_CLEAR);
    if (buffer) {
        sb = (struct Superblock *)buffer;
        sb->magic = 0x4E454F46; /* NEO F */
        sb->version = 0x00010000;
        sb->block_size = 4096;
        sb->total_blocks = 10000; /* Placeholder */
        sb->inode_table_start = 4;
        sb->inode_table_blocks = 256;
        sb->data_start = 300;
        strcpy(sb->volume_name, "NeoBench");
        sb->crc32 = ComputeCRC32C(buffer, 512 - 4);

        /* NeoFS Superblock at Sector 8192 (relative to partition start) */
        Seek(disk_fh, 8192 * 512, OFFSET_BEGINNING);
        if (Write(disk_fh, buffer, 4096) == 4096) {
            printf("NeoFS Superblock installed.\n");
        } else {
            printf("Error writing NeoFS superblock.\n");
        }
        FreeVec(buffer);
    }

    /* 3. Install Bootblock */
    printf("Installing Bootloader...\n");
    file_fh = Open(argv[3], MODE_OLDFILE);
    if (file_fh) {
        buffer = AllocVec(1024, MEMF_PUBLIC | MEMF_CLEAR);
        if (buffer) {
            Read(file_fh, buffer, 1024);
            Close(file_fh);
            Seek(disk_fh, 0, OFFSET_BEGINNING);
            if (Write(disk_fh, buffer, 1024) == 1024) printf("Bootloader installed.\n");
            FreeVec(buffer);
        }
    }

    /* 4. Install Kernel */
    printf("Installing Kernel...\n");
    file_fh = Open(argv[2], MODE_OLDFILE);
    if (file_fh) {
        buffer = AllocVec(262144, MEMF_PUBLIC | MEMF_CLEAR);
        if (buffer) {
            Read(file_fh, buffer, 262144);
            Close(file_fh);
            Seek(disk_fh, 1024, OFFSET_BEGINNING);
            if (Write(disk_fh, buffer, 262144) == 262144) printf("Kernel installed.\n");
            FreeVec(buffer);
        }
    }

    /* 5. Cleanup */
    Close(disk_fh);
    Inhibit(dev_name, 0L);
    printf("\nNeoBench Installation Success!\n");
    printf("Reboot to enter NeoBench Denise.\n");
    return 0;
}
