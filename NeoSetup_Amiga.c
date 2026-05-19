#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* NeoBench Master Setup v1.0 
 * Manually writes RDB and Partition blocks to force boot settings.
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

/* Amiga Checksum for RDB/PART blocks */
unsigned int AmigaChecksum(unsigned int *block) {
    unsigned int chk = 0;
    int i;
    block[2] = 0; /* Clear sum field */
    for (i = 0; i < 128; i++) chk += block[i];
    return (unsigned int)(0 - chk);
}

int main(int argc, char **argv) {
    void *MsgPort;
    struct IOStdReq *IOReq;
    unsigned int *rdb, *part;
    unsigned char *disk_buf;
    int unit;

    printf("NeoBench Master Setup v1.0\n");
    if (argc < 2) {
        printf("Usage: NeoSetup_Amiga [Unit#]\nExample: NeoSetup_Amiga 1\n");
        return 10;
    }
    unit = atoi(argv[1]);

    disk_buf = malloc(1024);
    memset(disk_buf, 0, 1024);
    rdb = (unsigned int *)disk_buf;
    part = (unsigned int *)(disk_buf + 512);

    /* 1. Build RDSK (Sector 0) */
    rdb[0] = 0x5244534B; /* 'RDSK' */
    rdb[1] = 64;         /* Size in longs */
    rdb[3] = 7;          /* HostID */
    rdb[4] = 512;        /* BlockSize */
    rdb[5] = 2;          /* Flags: LastRDB */
    rdb[7] = 1;          /* PartitionList at Sector 1 */
    rdb[8] = 1000;       /* Cyls */
    rdb[9] = 63;         /* Sectors */
    rdb[10] = 16;        /* Heads */
    rdb[2] = AmigaChecksum(rdb);

    /* 2. Build PART (Sector 1) */
    part[0] = 0x50415254; /* 'PART' */
    part[1] = 64;         /* Size in longs */
    part[4] = 0xFFFFFFFF; /* Next */
    part[5] = 1;          /* Flags: Bootable */
    ((unsigned char *)part)[36] = 3;
    ((unsigned char *)part)[37] = 'D';
    ((unsigned char *)part)[38] = 'H';
    ((unsigned char *)part)[39] = '0';
    part[32] = 11;        /* TableSize */
    part[33] = 1;         /* SizeBlock */
    part[35] = 16;        /* Surfaces */
    part[36] = 1;         /* SectorsPerBlock */
    part[37] = 63;        /* BlocksPerTrack */
    part[41] = 2;         /* LowCyl */
    part[42] = 999;       /* HighCyl */
    part[47] = 15;        /* BootPri - EXTREMELY HIGH */
    part[48] = 0x4E454F00; /* DosType 'NEO\0' */
    part[2] = AmigaChecksum(part);

    MsgPort = CreateMsgPort();
    IOReq = CreateIORequest(MsgPort, sizeof(struct IOStdReq));
    
    if (OpenDevice("uaehf.device", unit, IOReq, 0) == 0) {
        printf("Writing Master RDB/Partition to Unit %d...\n", unit);
        IOReq->io_Command = CMD_WRITE;
        IOReq->io_Data = disk_buf;
        IOReq->io_Length = 1024;
        IOReq->io_Offset = 0;
        DoIO(IOReq);
        CloseDevice(IOReq);
        printf("Settings applied! Partition DH0: is now Bootable with Priority 15.\n");
    } else {
        printf("Error: Could not open Unit %d.\n", unit);
    }

    DeleteIORequest(IOReq);
    DeleteMsgPort(MsgPort);
    free(disk_buf);
    return 0;
}
