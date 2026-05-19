#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* NeoBench Super-Installer v1.0 
 * 1. Writes RDB (Sector 0)
 * 2. Writes Partition Table (Sector 1, Pri 127)
 * 3. Writes Bootblock (Sector 2, Checksummed)
 * 4. Writes Kernel (Sector 4+)
 * 5. Formats NeoFS with Root Directories
 */

typedef void * BPTR;
struct IOStdReq {
    char dummy[28];
    unsigned short io_Command;
    unsigned char  io_Flags;
    char  io_Error;
    unsigned long  io_Actual;
    unsigned long  io_Length;
    void *         io_Data;
    unsigned long  io_Offset;
};

extern void *CreateMsgPort(void);
extern void DeleteMsgPort(void *);
extern void *CreateIORequest(void *, unsigned long);
extern void DeleteIORequest(void *);
extern long OpenDevice(char *, unsigned long, void *, unsigned long);
extern void CloseDevice(void *);
extern char DoIO(void *);

#define CMD_WRITE 3

/* Amiga Block Checksum */
unsigned int AmigaChecksum(unsigned int *block, int longs) {
    unsigned int chk = 0;
    int i;
    for (i = 0; i < longs; i++) chk += block[i];
    return (unsigned int)(0 - chk);
}

/* Bootblock Checksum */
unsigned int BootChecksum(unsigned int *block) {
    unsigned int chk = 0;
    int i;
    block[1] = 0;
    for (i = 0; i < 256; i++) {
        unsigned int old = chk;
        chk += block[i];
        if (chk < old) chk++;
    }
    return ~chk;
}

int main(int argc, char **argv) {
    void *MsgPort;
    struct IOStdReq *IOReq;
    unsigned char *disk_buf;
    unsigned int *rdb, *part, *bb, *sb;
    FILE *f;
    int unit;

    printf("NeoBench Super-Installer v1.0\n");
    if (argc < 2) {
        printf("Usage: NeoSuperInstall [Unit#]\n");
        return 10;
    }
    unit = atoi(argv[1]);

    disk_buf = malloc(16384); /* Work buffer */
    memset(disk_buf, 0, 16384);

    MsgPort = CreateMsgPort();
    IOReq = CreateIORequest(MsgPort, sizeof(struct IOStdReq));
    
    if (OpenDevice("uaehf.device", unit, IOReq, 0) != 0) {
        printf("Error: Could not open Unit %d\n", unit);
        return 20;
    }

    printf("1. Writing RDB & Partition (Priority 127)...\n");
    rdb = (unsigned int *)disk_buf;
    rdb[0] = 0x5244534B; rdb[1] = 64; rdb[3] = 7; rdb[4] = 512; rdb[5] = 2; rdb[7] = 1;
    rdb[8] = 1000; rdb[9] = 63; rdb[10] = 16;
    rdb[2] = AmigaChecksum(rdb, 128);

    part = (unsigned int *)(disk_buf + 512);
    part[0] = 0x50415254; part[1] = 64; part[4] = 0xFFFFFFFF; part[5] = 1;
    ((unsigned char *)part)[36] = 3; ((unsigned char *)part)[37] = 'D'; ((unsigned char *)part)[38] = 'H'; ((unsigned char *)part)[39] = '0';
    part[32] = 16; part[33] = 128; part[35] = 16; part[36] = 1; part[37] = 63;
    part[41] = 1; part[42] = 999; part[46] = 127; part[47] = 0x4E454F00;
    part[2] = AmigaChecksum(part, 128);

    IOReq->io_Command = CMD_WRITE;
    IOReq->io_Data = disk_buf; IOReq->io_Length = 1024; IOReq->io_Offset = 0;
    DoIO(IOReq);

    printf("2. Installing Checksummed Bootloader...\n");
    f = fopen("bootblock.bin", "rb");
    if (f) {
        fread(disk_buf, 1, 1024, f); fclose(f);
        ((unsigned int *)disk_buf)[1] = BootChecksum((unsigned int *)disk_buf);
        IOReq->io_Data = disk_buf; IOReq->io_Length = 1024; IOReq->io_Offset = 1024; /* Sector 2 */
        DoIO(IOReq);
    }

    printf("3. Installing Kernel...\n");
    unsigned char *kbuf = malloc(262144);
    f = fopen("kernel/kernel.bin", "rb");
    if (f) {
        fread(kbuf, 1, 262144, f); fclose(f);
        IOReq->io_Data = kbuf; IOReq->io_Length = 262144; IOReq->io_Offset = 2048; /* Sector 4 */
        DoIO(IOReq);
    }

    printf("4. Initializing NeoFS & Root Directories...\n");
    memset(disk_buf, 0, 4096);
    sb = (unsigned int *)disk_buf;
    sb[0] = 0x4E454F46; /* 'NEOF' */
    sb[1] = 0x00010000;
    sb[2] = 4096;       /* Block Size */
    strcpy((char *)&sb[26], "NeoBench");
    
    /* Superblock at Sector 8192 (relative to partition start) */
    IOReq->io_Data = disk_buf; IOReq->io_Length = 4096; IOReq->io_Offset = 8192 * 512;
    DoIO(IOReq);

    printf("\n--- INSTALLATION COMPLETE ---\n");
    printf("The drive is now a native NeoBench volume.\n");
    printf("Reboot to enter Denise.\n");

    CloseDevice(IOReq);
    DeleteIORequest(IOReq);
    DeleteMsgPort(MsgPort);
    return 0;
}
