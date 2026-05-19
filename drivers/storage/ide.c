/*
 * NeoBench Bare-Metal Amiga Kernel
 * IDE/ATA Driver (C99)
 */

#include "../include/neobench.h"
#include "../include/types.h"

/* Hardware Addresses */
#define A4000_IDE_BASE 0x00DD2020UL
#define A4000_IDE_ALT  0x00DD3018UL
#define A1200_IDE_BASE 0x00DA0000UL
#define A1200_IDE_ALT  0x00DA4000UL
#define GAYLE_ID_REG   0x00DE1000UL
#define VPOSR_ADDR     0x00DFF004UL

/* ATA register offsets */
#define ATA_DATA_OFF      0x00
#define ATA_ERROR_OFF     0x04
#define ATA_FEATURES_OFF  0x04
#define ATA_NSECTOR_OFF   0x08
#define ATA_LBAL_OFF      0x0C
#define ATA_LBAM_OFF      0x10
#define ATA_LBAH_OFF      0x14
#define ATA_DEVICE_OFF    0x18
#define ATA_STATUS_OFF    0x1C
#define ATA_COMMAND_OFF   0x1C

/* ATA status bits */
#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

/* ATA commands */
#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_FLUSH    0xE7

/* Device control bits */
#define ATA_DCR_SRST 0x04
#define ATA_DCR_NIEN 0x02

typedef struct {
    uint8  present;
    char   model[41];
    char   serial[21];
    char   firmware[9];
    uint32 total_sectors;
    uint8  lba_supported;
} DriveInfo;

typedef enum { PORT_NONE = 0, PORT_A4000, PORT_A1200 } IDEPort;

static IDEPort   active_port = PORT_NONE;
static uint32    ide_base;
static uint32    ide_alt;
static DriveInfo drives[2];

/* Register access */
static inline uint16 ide_read_data(void) { return *(volatile uint16*)(ide_base + ATA_DATA_OFF); }
static inline void ide_write_data(uint16 val) { *(volatile uint16*)(ide_base + ATA_DATA_OFF) = val; }
static inline uint8 ide_read8(uint32 off) { return *(volatile uint8*)(ide_base + off); }
static inline void ide_write8(uint32 off, uint8 val) { *(volatile uint8*)(ide_base + off) = val; }
static inline uint8 ide_alt_status(void) { return *(volatile uint8*)ide_alt; }
static inline void ide_dev_ctrl(uint8 val) { *(volatile uint8*)ide_alt = val; }

#define IDE_TIMEOUT 2000000UL

static uint8 wait_not_busy(void) {
    for (uint32 i = 0; i < IDE_TIMEOUT; i++) {
        if (!(ide_alt_status() & ATA_SR_BSY)) return 1;
    }
    return 0;
}

static uint8 wait_drq(void) {
    for (uint32 i = 0; i < IDE_TIMEOUT; i++) {
        uint8 s = ide_read8(ATA_STATUS_OFF);
        if (s & ATA_SR_DRQ) return 1;
        if (s & ATA_SR_ERR) return 0;
    }
    return 0;
}

static uint8 wait_ready(void) {
    for (uint32 i = 0; i < IDE_TIMEOUT; i++) {
        uint8 s = ide_alt_status();
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRDY)) return 1;
    }
    return 0;
}

static void select_drive(int drive) {
    ide_write8(ATA_DEVICE_OFF, (uint8)(drive ? 0xB0 : 0xA0));
    (void)ide_alt_status(); (void)ide_alt_status();
    (void)ide_alt_status(); (void)ide_alt_status();
}

static void swap_string(char *str, int len) {
    for (int i = 0; i < len - 1; i += 2) {
        char tmp = str[i];
        str[i] = str[i + 1];
        str[i + 1] = tmp;
    }
    for (int i = len - 1; i >= 0 && (str[i] == ' ' || str[i] == '\0'); i--) {
        str[i] = '\0';
    }
}

static uint8 identify_drive(int drive_num) {
    DriveInfo *drv = &drives[drive_num];
    drv->present = 0;

    select_drive(drive_num);
    if (!wait_not_busy()) return 0;

    uint8 status = ide_read8(ATA_STATUS_OFF);
    if (status == 0x00 || status == 0xFF) return 0;

    ide_write8(ATA_COMMAND_OFF, ATA_CMD_IDENTIFY);
    if (!wait_drq()) return 0;

    uint16 idbuf[256];
    for (int i = 0; i < 256; i++) {
        idbuf[i] = ide_read_data();
    }

    drv->present = 1;
    drv->lba_supported = (idbuf[49] & (1u << 9)) != 0;

    if (drv->lba_supported) {
        drv->total_sectors = ((uint32)idbuf[61] << 16) | idbuf[60];
    } else {
        uint16 cyls  = idbuf[1];
        uint16 heads = idbuf[3];
        uint16 spt   = idbuf[6];
        drv->total_sectors = (uint32)cyls * heads * spt;
    }

    for (int i = 0; i < 10; i++) {
        drv->serial[i*2]   = (char)(idbuf[10+i] >> 8);
        drv->serial[i*2+1] = (char)(idbuf[10+i] & 0xFF);
    }
    drv->serial[20] = '\0';
    swap_string(drv->serial, 20);

    for (int i = 0; i < 4; i++) {
        drv->firmware[i*2]   = (char)(idbuf[23+i] >> 8);
        drv->firmware[i*2+1] = (char)(idbuf[23+i] & 0xFF);
    }
    drv->firmware[8] = '\0';
    swap_string(drv->firmware, 8);

    for (int i = 0; i < 20; i++) {
        drv->model[i*2]   = (char)(idbuf[27+i] >> 8);
        drv->model[i*2+1] = (char)(idbuf[27+i] & 0xFF);
    }
    drv->model[40] = '\0';
    swap_string(drv->model, 40);

    return 1;
}

uint8 ide_init(void) {
    ide_base = A4000_IDE_BASE;
    ide_alt  = A4000_IDE_ALT;
    active_port = PORT_NONE;

    ide_dev_ctrl(ATA_DCR_SRST | ATA_DCR_NIEN);
    for (volatile uint32 i = 0; i < 10000; i++);
    ide_dev_ctrl(ATA_DCR_NIEN);
    wait_not_busy();

    uint8 val = *(volatile uint8*)(ide_base + ATA_STATUS_OFF);
    if (val != 0xFF && val != 0x00) {
        active_port = PORT_A4000;
    } else {
        ide_base = A1200_IDE_BASE;
        ide_alt  = A1200_IDE_ALT;
        ide_dev_ctrl(ATA_DCR_SRST | ATA_DCR_NIEN);
        for (volatile uint32 i = 0; i < 10000; i++);
        ide_dev_ctrl(ATA_DCR_NIEN);
        wait_not_busy();
        val = *(volatile uint8*)(ide_base + ATA_STATUS_OFF);
        if (val != 0xFF && val != 0x00) {
            active_port = PORT_A1200;
        }
    }

    if (active_port == PORT_NONE) return 0;

    ide_dev_ctrl(0x00);
    uint8 found = 0;
    for (int i = 0; i < 2; i++) {
        if (identify_drive(i)) found = 1;
    }
    return found;
}

uint8 ide_read_sectors(int drive_num, uint32 lba, uint32 count, void *buffer) {
    if (drive_num < 0 || drive_num > 1 || !drives[drive_num].present || count == 0 || count > 256) return 0;
    uint16 *buf = (uint16 *)buffer;
    select_drive(drive_num);
    if (!wait_ready()) return 0;
    ide_write8(ATA_DEVICE_OFF, (uint8)(0xE0 | (drive_num << 4) | ((lba >> 24) & 0x0F)));
    ide_write8(ATA_NSECTOR_OFF, (uint8)(count == 256 ? 0 : count));
    ide_write8(ATA_LBAL_OFF, (uint8)lba);
    ide_write8(ATA_LBAM_OFF, (uint8)(lba >> 8));
    ide_write8(ATA_LBAH_OFF, (uint8)(lba >> 16));
    ide_write8(ATA_COMMAND_OFF, ATA_CMD_READ);
    for (uint32 s = 0; s < count; s++) {
        if (!wait_drq()) return 0;
        for (int w = 0; w < 256; w++) *buf++ = ide_read_data();
    }
    return 1;
}

uint8 ide_write_sectors(int drive_num, uint32 lba, uint32 count, const void *buffer) {
    if (drive_num < 0 || drive_num > 1 || !drives[drive_num].present || count == 0 || count > 256) return 0;
    const uint16 *buf = (const uint16 *)buffer;
    select_drive(drive_num);
    if (!wait_ready()) return 0;
    ide_write8(ATA_DEVICE_OFF, (uint8)(0xE0 | (drive_num << 4) | ((lba >> 24) & 0x0F)));
    ide_write8(ATA_NSECTOR_OFF, (uint8)(count == 256 ? 0 : count));
    ide_write8(ATA_LBAL_OFF, (uint8)lba);
    ide_write8(ATA_LBAM_OFF, (uint8)(lba >> 8));
    ide_write8(ATA_LBAH_OFF, (uint8)(lba >> 16));
    ide_write8(ATA_COMMAND_OFF, ATA_CMD_WRITE);
    for (uint32 s = 0; s < count; s++) {
        if (!wait_drq()) return 0;
        for (int w = 0; w < 256; w++) ide_write_data(*buf++);
    }
    ide_write8(ATA_COMMAND_OFF, ATA_CMD_FLUSH);
    wait_not_busy();
    return 1;
}
