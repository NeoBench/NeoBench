#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* NeoBench Master Setup v2.0
 * Fixed PART block offsets for DosType and BootPriority
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

unsigned int AmigaChecksum(unsigned int *block) {
    unsigned int chk = 0;
    int i;
    block[2] = 0;
    for (i = 0; i < 128; i++) chk += block[i];
    return (unsigned int)(0 - chk);
}

int main(int argc, char **argv) {
    void *MsgPort;
    struct IOStdReq *IOReq;
    unsigned int *rdb, *part;
    unsigned char *disk_buf;
    int unit;

    printf("NeoBench Master Setup v2.0 (Offset Fix)\n");
    if (argc < 2) {
        printf("Usage: NeoSetup_Amiga [Unit#]\n");
        return 10;
    }
    unit = atoi(argv[1]);

    disk_buf = malloc(1024);
    memset(disk_buf, 0, 1024);
    rdb = (unsigned int *)disk_buf;
    part = (unsigned int *)(disk_buf + 512);

    /* RDSK (Sector 0) */
    rdb[0] = 0x5244534B; 
    rdb[1] = 64;         
    rdb[3] = 7;          
    rdb[4] = 512;        
    rdb[5] = 2;          /* LastRDB */
    rdb[7] = 1;          /* PART at Sector 1 */
    rdb[8] = 1000;       
    rdb[9] = 63;         
    rdb[10] = 16;        
    rdb[2] = AmigaChecksum(rdb);

    /* PART (Sector 1) - FIXED OFFSETS */
    part[0] = 0x50415254; 
    part[1] = 64;         
    part[4] = 0xFFFFFFFF; /* No more partitions */
    part[5] = 1;          /* BOOTABLE */
    
    /* Device Name 'DH0' */
    ((unsigned char *)part)[36] = 3;
    ((unsigned char *)part)[37] = 'D';
    ((unsigned char *)part)[38] = 'H';
    ((unsigned char *)part)[39] = '0';

    /* Environment Table (Starts at Long 32) */
    part[32] = 16;         /* TableSize */
    part[33] = 128;        /* SizeBlock (longs) */
    part[35] = 16;         /* Heads */
    part[36] = 1;          /* SectorsPerBlock */
    part[37] = 63;         /* BlocksPerTrack */
    part[41] = 1;          /* LowCyl */
    part[42] = 999;        /* HighCyl */
    part[46] = 127;        /* Boot Priority (MAX) */
    part[47] = 0x4E454F00; /* DosType 'NEO\0' */
    
    part[2] = AmigaChecksum(part);

    MsgPort = CreateMsgPort();
    IOReq = CreateIORequest(MsgPort, sizeof(struct IOStdReq));
    
    if (OpenDevice("uaehf.device", unit, IOReq, 0) == 0) {
        printf("Applying Hardware Fix to Unit %d...\n", unit);
        IOReq->io_Command = CMD_WRITE;
        IOReq->io_Data = disk_buf;
        IOReq->io_Length = 1024;
        IOReq->io_Offset = 0;
        DoIO(IOReq);
        CloseDevice(IOReq);
        printf("SUCCESS: RDB Fixed. DH0: is now Priority 127.\n");
    } else {
        printf("Error: Unit %d not found.\n", unit);
    }

    DeleteIORequest(IOReq);
    DeleteMsgPort(MsgPort);
    free(disk_buf);
    return 0;
}
