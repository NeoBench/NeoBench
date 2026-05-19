/*
 * NeoBench Bare-Metal Amiga Kernel
 * CD-ROM Driver (ATAPI over A4000 IDE / Gayle)
 *
 * ATAPI (ATA Packet Interface) allows ATAPI devices (CD-ROM, tape, etc.)
 * to be connected to a standard ATA/IDE bus.  Commands are 12-byte
 * SCSI-like packets sent via the ATA command interface.
 *
 * This driver sits on top of the A4000 IDE hardware (Gayle chip) and
 * implements the ATAPI packet command interface for CD-ROM drives.
 *
 * Supported operations:
 *   - ATAPI device detection and initialisation
 *   - READ TOC (table of contents)
 *   - READ CD (raw sector read)
 *   - READ(10) for data CDs (ISO9660)
 *   - PLAY AUDIO MSF / PAUSE / RESUME / STOP
 *   - GET/SET VOLUME
 *   - INQUIRY / REQUEST SENSE
 *
 * A4000 IDE / Gayle register addresses:
 *   The Gayle chip maps the IDE interface at 0xDD2020 (primary channel).
 *   Registers are byte-wide at even addresses (16-bit data port at 0xDD2020).
 *
 * References:
 *   AT Attachment with Packet Interface (ATAPI) specification
 *   SFF-8020i (MMC-2 subset)
 *   Amiga A4000 Technical Reference Manual (Gayle section)
 */

#include "../include/neobench.h"
#include "../include/types.h"

/* ========================================================================
 * A4000 Gayle IDE Register Addresses
 *
 * Primary IDE channel base: 0xDD2020
 * Each register is at base + (reg * 4) for byte-wide access.
 * The data register (reg 0) is 16-bit.
 *
 * Note: The Gayle IDE interface is big-endian aware; 16-bit reads/writes
 * to the data port work as expected on the 68k without byte swapping.
 * ======================================================================== */

#define GAYLE_IDE_BASE      0x00DD2020UL
#define GAYLE_STATUS_REG    0x00DD2018UL  /* Gayle status / interrupt */

/* ATA/IDE register offsets (each register is 4 bytes apart on Gayle) */
#define IDE_REG(n)          (GAYLE_IDE_BASE + ((n) * 4))

#define IDE_DATA            IDE_REG(0)   /* 16-bit data port */
#define IDE_ERROR           IDE_REG(1)   /* Error register (read) */
#define IDE_FEATURES        IDE_REG(1)   /* Features register (write) */
#define IDE_SECTOR_COUNT    IDE_REG(2)   /* Sector count / ATAPI byte count low */
#define IDE_LBA_LOW         IDE_REG(3)   /* LBA low / ATAPI byte count high */
#define IDE_LBA_MID         IDE_REG(4)   /* LBA mid / ATAPI byte count low */
#define IDE_LBA_HIGH        IDE_REG(5)   /* LBA high / ATAPI byte count high */
#define IDE_DRIVE_HEAD      IDE_REG(6)   /* Drive/head select */
#define IDE_STATUS          IDE_REG(7)   /* Status register (read) */
#define IDE_COMMAND         IDE_REG(7)   /* Command register (write) */

/* ATA status register bits */
#define ATA_STAT_BSY        0x80    /* Busy */
#define ATA_STAT_DRDY       0x40    /* Drive ready */
#define ATA_STAT_DWF        0x20    /* Drive write fault */
#define ATA_STAT_DSC        0x10    /* Seek complete */
#define ATA_STAT_DRQ        0x08    /* Data request */
#define ATA_STAT_CORR       0x04    /* Corrected data */
#define ATA_STAT_IDX        0x02    /* Index */
#define ATA_STAT_ERR        0x01    /* Error */

/* ATA error register bits */
#define ATA_ERR_ABRT        0x04    /* Command aborted */
#define ATA_ERR_IDNF        0x10    /* ID not found */
#define ATA_ERR_UNC         0x40    /* Uncorrectable data error */

/* ATA commands */
#define ATA_CMD_RESET       0x08    /* Device reset */
#define ATA_CMD_IDENTIFY    0xEC    /* Identify device */
#define ATA_CMD_IDENTIFY_PACKET 0xA1  /* Identify packet device (ATAPI) */
#define ATA_CMD_PACKET      0xA0    /* Send packet command (ATAPI) */

/* ATAPI packet commands (12-byte, SCSI-compatible subset) */
#define ATAPI_TEST_UNIT_READY   0x00
#define ATAPI_REQUEST_SENSE     0x03
#define ATAPI_INQUIRY           0x12
#define ATAPI_START_STOP        0x1B
#define ATAPI_PREVENT_ALLOW     0x1E
#define ATAPI_READ_FORMAT_CAP   0x23
#define ATAPI_READ_CAPACITY     0x25
#define ATAPI_READ_10           0x28
#define ATAPI_SEEK              0x2B
#define ATAPI_PLAY_AUDIO_MSF    0x47
#define ATAPI_PAUSE_RESUME      0x4B
#define ATAPI_STOP_PLAY         0x4E
#define ATAPI_READ_TOC          0x43
#define ATAPI_READ_HEADER       0x44
#define ATAPI_PLAY_AUDIO        0x45
#define ATAPI_GET_CONFIG        0x46
#define ATAPI_GET_EVENT         0x4A
#define ATAPI_READ_DISC_INFO    0x51
#define ATAPI_READ_TRACK_INFO   0x52
#define ATAPI_MODE_SELECT       0x55
#define ATAPI_MODE_SENSE        0x5A
#define ATAPI_READ_CD           0xBE

/* ATAPI device signature in IDENTIFY */
#define ATAPI_SIG_SC        0x01    /* Sector count after reset */
#define ATAPI_SIG_SN        0x01    /* Sector number after reset */
#define ATAPI_SIG_CL        0x14    /* Cylinder low after reset */
#define ATAPI_SIG_CH        0xEB    /* Cylinder high after reset */

/* ========================================================================
 * Device state
 * ======================================================================== */

#define MAX_ATAPI_DEVICES   2   /* Primary master + slave */

typedef struct {
    bool     present;
    bool     is_atapi;
    uint8_t  device_type;   /* ATAPI device type from INQUIRY */
    uint32_t capacity_lba;  /* For data CDs */
    char     model[41];     /* 40 chars + NUL from IDENTIFY */
} ATAPIDevice;

static ATAPIDevice atapi_devices[MAX_ATAPI_DEVICES];
static bool        cdrom_initialized = false;

/* Sector data buffer (2352 bytes for raw CD sector, 2048 for data) */
static uint8_t atapi_buf[2352];

/* ========================================================================
 * Low-level ATA register access
 * ======================================================================== */

static inline uint8_t ide_read8(uint32_t reg)
{
    return *((volatile uint8_t *)reg);
}

static inline void ide_write8(uint32_t reg, uint8_t val)
{
    *((volatile uint8_t *)reg) = val;
}

static inline uint16_t ide_read16(void)
{
    return *((volatile uint16_t *)IDE_DATA);
}

static inline void ide_write16(uint16_t val)
{
    *((volatile uint16_t *)IDE_DATA) = val;
}

/* ========================================================================
 * ATA wait helpers
 * ======================================================================== */

#define IDE_TIMEOUT_LOOPS   5000000UL

static bool ide_wait_not_busy(void)
{
    for (uint32_t i = 0; i < IDE_TIMEOUT_LOOPS; i++) {
        if (!(ide_read8(IDE_STATUS) & ATA_STAT_BSY)) return true;
    }
    return false;
}

static bool ide_wait_drq(void)
{
    for (uint32_t i = 0; i < IDE_TIMEOUT_LOOPS; i++) {
        uint8_t status = ide_read8(IDE_STATUS);
        if (status & ATA_STAT_DRQ)  return true;
        if (status & ATA_STAT_ERR)  return false;
        if (status & ATA_STAT_BSY)  continue;
    }
    return false;
}

static bool ide_wait_ready(void)
{
    for (uint32_t i = 0; i < IDE_TIMEOUT_LOOPS; i++) {
        uint8_t status = ide_read8(IDE_STATUS);
        if ((status & (ATA_STAT_BSY | ATA_STAT_DRDY)) == ATA_STAT_DRDY)
            return true;
    }
    return false;
}

/* ========================================================================
 * Drive select
 *
 * The drive/head register selects master (0) or slave (1).
 * For LBA28: bits [3:0] = LBA[27:24], bit 4 = drive, bit 6 = LBA mode.
 * For ATAPI: we just set the drive bit.
 * ======================================================================== */

static void ide_select_drive(int drive)
{
    ide_write8(IDE_DRIVE_HEAD, (uint8_t)(0xA0 | ((drive & 1) << 4)));
    /* Allow drive select to settle */
    for (volatile int i = 0; i < 500; i++) {}
}

/* ========================================================================
 * ATAPI IDENTIFY PACKET DEVICE
 *
 * Sends IDENTIFY PACKET DEVICE (0xA1) and reads 256 words of device info.
 * We check words [0] for ATAPI signature and extract model string.
 * ======================================================================== */

static bool atapi_identify(int drive, ATAPIDevice *dev)
{
    ide_select_drive(drive);
    if (!ide_wait_not_busy()) return false;

    /* Write IDENTIFY PACKET command */
    ide_write8(IDE_COMMAND, ATA_CMD_IDENTIFY_PACKET);

    if (!ide_wait_drq()) return false;

    /* Read 256 words of identify data */
    uint16_t idbuf[256];
    for (int i = 0; i < 256; i++) {
        idbuf[i] = ide_read16();
    }

    /* Word 0 bit [15:14] = 10 for ATAPI device */
    if ((idbuf[0] >> 14) != 2) return false;

    dev->is_atapi    = true;
    dev->device_type = (uint8_t)((idbuf[0] >> 8) & 0x1F);
    dev->present     = true;

    /* Extract model string (words 27-46, big-endian byte swap) */
    int pos = 0;
    for (int w = 27; w <= 46 && pos < 40; w++) {
        dev->model[pos++] = (char)(idbuf[w] >> 8);
        dev->model[pos++] = (char)(idbuf[w] & 0xFF);
    }
    dev->model[40] = '\0';

    return true;
}

/* ========================================================================
 * ATAPI Packet Command Execution
 *
 * Protocol:
 *   1. Select drive
 *   2. Write FEATURES=0, BYTE_COUNT_LOW, BYTE_COUNT_HIGH
 *   3. Write PACKET command (0xA0)
 *   4. Wait for DRQ
 *   5. Write 12-byte packet (6 words)
 *   6. Wait for BSY to clear
 *   7. If data phase: read/write data words
 *   8. Read status
 *
 * byte_count = maximum data transfer length (split across LBA_MID/HIGH).
 * ======================================================================== */

static int atapi_exec(int drive, const uint8_t *packet,
                       uint8_t *data, uint16_t byte_count, bool data_in)
{
    ide_select_drive(drive);
    if (!ide_wait_not_busy()) return -1;

    /* Set up packet command */
    ide_write8(IDE_FEATURES,    0x00);
    ide_write8(IDE_LBA_MID,     (uint8_t)(byte_count & 0xFF));
    ide_write8(IDE_LBA_HIGH,    (uint8_t)(byte_count >> 8));
    ide_write8(IDE_COMMAND,     ATA_CMD_PACKET);

    /* Wait for DRQ (device ready to receive packet) */
    if (!ide_wait_drq()) return -1;

    /* Write 12-byte packet as 6 words (big-endian: send as-is) */
    for (int i = 0; i < 12; i += 2) {
        ide_write16((uint16_t)((packet[i] << 8) | packet[i + 1]));
    }

    /* Wait for command completion */
    if (!ide_wait_not_busy()) return -1;

    uint8_t status = ide_read8(IDE_STATUS);
    if (status & ATA_STAT_ERR) return -1;

    if (!data || byte_count == 0) return 0;

    /* Check if data phase follows */
    if (!(status & ATA_STAT_DRQ)) return 0;

    /* Get actual transfer size from byte count registers */
    uint16_t actual_count = (uint16_t)(
        (ide_read8(IDE_LBA_HIGH) << 8) | ide_read8(IDE_LBA_MID));

    if (actual_count > byte_count) actual_count = byte_count;

    /* Transfer data words */
    uint16_t words = actual_count / 2;
    if (data_in) {
        for (uint16_t i = 0; i < words; i++) {
            uint16_t w = ide_read16();
            data[i * 2]     = (uint8_t)(w >> 8);
            data[i * 2 + 1] = (uint8_t)(w & 0xFF);
        }
    } else {
        for (uint16_t i = 0; i < words; i++) {
            uint16_t w = (uint16_t)((data[i * 2] << 8) | data[i * 2 + 1]);
            ide_write16(w);
        }
    }

    /* Final status read */
    if (!ide_wait_not_busy()) return -1;
    status = ide_read8(IDE_STATUS);
    if (status & ATA_STAT_ERR) return -1;

    return (int)actual_count;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

bool cdrom_init(void)
{
    for (int i = 0; i < MAX_ATAPI_DEVICES; i++) {
        atapi_devices[i].present  = false;
        atapi_devices[i].is_atapi = false;
    }

    /* Try to identify devices on primary channel */
    for (int drive = 0; drive < MAX_ATAPI_DEVICES; drive++) {
        atapi_identify(drive, &atapi_devices[drive]);
    }

    cdrom_initialized = true;
    return true;
}

int cdrom_scan(void)
{
    int count = 0;
    for (int i = 0; i < MAX_ATAPI_DEVICES; i++) {
        if (atapi_devices[i].present &&
            atapi_devices[i].device_type == 0x05) {  /* CD-ROM */
            count++;
        }
    }
    return count;
}

bool cdrom_present(int drive)
{
    if (drive < 0 || drive >= MAX_ATAPI_DEVICES) return false;
    return atapi_devices[drive].present &&
           atapi_devices[drive].device_type == 0x05;
}

const char *cdrom_model(int drive)
{
    if (!cdrom_present(drive)) return "";
    return atapi_devices[drive].model;
}

bool cdrom_test_unit_ready(int drive)
{
    if (!cdrom_present(drive)) return false;
    uint8_t pkt[12] = { ATAPI_TEST_UNIT_READY, 0,0,0,0,0,0,0,0,0,0,0 };
    return atapi_exec(drive, pkt, NULL, 0, true) == 0;
}

/*
 * Read data sectors (MODE 1 / MODE 2 Form 1 = 2048 bytes/sector).
 * lba is the logical block address on the disc.
 */
bool cdrom_read_sectors(int drive, uint32_t lba,
                         uint16_t count, uint8_t *buf)
{
    if (!cdrom_present(drive)) return false;

    uint8_t pkt[12] = {
        ATAPI_READ_10,
        0x00,
        (uint8_t)(lba >> 24), (uint8_t)(lba >> 16),
        (uint8_t)(lba >>  8), (uint8_t)(lba),
        0x00,
        (uint8_t)(count >> 8), (uint8_t)(count),
        0x00, 0x00, 0x00
    };

    uint32_t total_bytes = (uint32_t)count * 2048;
    return atapi_exec(drive, pkt, buf, (uint16_t)total_bytes, true) > 0;
}

/*
 * Read TOC (Table of Contents).
 * Returns the raw TOC data in buf (up to buf_len bytes).
 * Use format 0 for normal TOC, format 1 for session info.
 */
int cdrom_read_toc(int drive, uint8_t format, uint8_t *buf, uint16_t buf_len)
{
    if (!cdrom_present(drive)) return -1;

    uint8_t pkt[12] = {
        ATAPI_READ_TOC,
        0x00,
        (uint8_t)(format & 0x0F),
        0x00, 0x00, 0x00,
        0x00,       /* Start track (0 = all) */
        (uint8_t)(buf_len >> 8), (uint8_t)(buf_len),
        0x00, 0x00, 0x00
    };

    return atapi_exec(drive, pkt, buf, buf_len, true);
}

/*
 * Play audio from MSF (minute/second/frame) position.
 * MSF format: each value is BCD or binary per drive.
 */
bool cdrom_play_audio_msf(int drive,
                           uint8_t start_m, uint8_t start_s, uint8_t start_f,
                           uint8_t end_m,   uint8_t end_s,   uint8_t end_f)
{
    if (!cdrom_present(drive)) return false;

    uint8_t pkt[12] = {
        ATAPI_PLAY_AUDIO_MSF,
        0x00,
        0x00,       /* Reserved */
        start_m, start_s, start_f,
        end_m,   end_s,   end_f,
        0x00, 0x00, 0x00
    };

    return atapi_exec(drive, pkt, NULL, 0, true) == 0;
}

bool cdrom_pause(int drive)
{
    if (!cdrom_present(drive)) return false;
    uint8_t pkt[12] = { ATAPI_PAUSE_RESUME, 0,0,0,0,0,0,0,0, 0,0,0 };
    /* Pause: byte 8 = 0 */
    return atapi_exec(drive, pkt, NULL, 0, true) == 0;
}

bool cdrom_resume(int drive)
{
    if (!cdrom_present(drive)) return false;
    uint8_t pkt[12] = { ATAPI_PAUSE_RESUME, 0,0,0,0,0,0,0,1, 0,0,0 };
    /* Resume: byte 8 = 1 */
    return atapi_exec(drive, pkt, NULL, 0, true) == 0;
}

bool cdrom_stop(int drive)
{
    if (!cdrom_present(drive)) return false;
    uint8_t pkt[12] = { ATAPI_STOP_PLAY, 0,0,0,0,0,0,0,0,0,0,0 };
    return atapi_exec(drive, pkt, NULL, 0, true) == 0;
}

bool cdrom_eject(int drive)
{
    if (!cdrom_present(drive)) return false;
    /* START/STOP UNIT: LoEj=1, Start=0 */
    uint8_t pkt[12] = { ATAPI_START_STOP, 0,0,0, 0x02, 0,0,0,0,0,0,0 };
    return atapi_exec(drive, pkt, NULL, 0, true) == 0;
}

bool cdrom_load(int drive)
{
    if (!cdrom_present(drive)) return false;
    /* START/STOP UNIT: LoEj=1, Start=1 */
    uint8_t pkt[12] = { ATAPI_START_STOP, 0,0,0, 0x03, 0,0,0,0,0,0,0 };
    return atapi_exec(drive, pkt, NULL, 0, true) == 0;
}
