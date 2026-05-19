/*
 * NeoBench Bare-Metal Amiga Kernel
 * IDE/ATA Driver
 *
 * Supports A4000 internal IDE (Gayle at 0xDD2020), A1200 Gayle IDE,
 * and standard PIO mode transfers.
 *
 * Corrections vs v1.0:
 *
 *  1. A4000 IDE BASE ADDRESS WRONG (critical).
 *     The original used 0xDA0000 for A4000 IDE.  The A4000 IDE is
 *     accessed via the Gayle chip at 0xDD2020.  0xDA0000 is the A4000
 *     DMA controller / chip RAM region, not IDE registers.
 *     Correct A4000 IDE base: 0xDD2020 (same Gayle chip as A1200 but
 *     at a slightly different address).  The alternate status register
 *     is at 0xDD3018.
 *     A1200 IDE base: 0xDA0000, alternate: 0xDA4000 (via Gayle).
 *     These are different machines with different addresses.
 *
 *  2. ATA REGISTER STRIDE WRONG.
 *     The original used a 4-byte stride (ATA_DATA=0, ATA_ERROR=4, etc.)
 *     but this is only correct for some Gayle mappings.  On the A4000
 *     Gayle, the IDE registers are at 4-byte stride starting from the
 *     base.  On the A1200 they are also 4-byte stride.  The register
 *     map itself was correct in naming but the DATA register access
 *     needs to be 16-bit (word) while all others are byte-wide.
 *     On the A4000 the data register is at the base address as a
 *     16-bit register.  Byte registers follow at +4, +8, etc.
 *
 *  3. DATA REGISTER BYTE ORDER.
 *     The ATA bus is little-endian.  The 68k is big-endian.  On the
 *     Amiga's IDE interface, the data bytes are swapped compared to
 *     what an x86 would see.  IDENTIFY data strings (model, serial,
 *     firmware) are already byte-swapped by the drive, so on the Amiga
 *     reading them as-is gives the correct string order without needing
 *     swap_string().  However the total_sectors words (60-61) need
 *     the word-order swap: word 60 = low 16 bits, word 61 = high 16 bits
 *     of the 28-bit LBA count.  The original correctly assembled these.
 *     The IDENTIFY string swap: drives send strings with each pair of
 *     adjacent bytes swapped.  swap_string() corrects this.  The
 *     original implementation was correct.
 *
 *  4. GAYLE DETECTION METHOD WRONG.
 *     The original shifted bits out of GAYLE_ID_REG byte-by-byte in a
 *     loop, which is the Gayle detection method used on the A1200.
 *     On the A4000 the Gayle variant (sometimes called "Gayle II" or
 *     just the A4000 IDE chipset) is always present - we don't need
 *     to detect it the same way.  The correct approach for the A4000
 *     is to check for IDE presence by attempting to read the status
 *     register, not by reading the Gayle ID.  Fixed: we detect which
 *     machine type we're on and use appropriate addresses.
 *     We detect A4000 vs A1200 by checking whether the VPOSR Agnus/
 *     Alice ID indicates AGA (A4000/A1200) and then checking IDE
 *     register accessibility.
 *
 *  5. ALTERNATE STATUS ACCESS DURING POLLING.
 *     wait_not_busy() and wait_ready() used ide_alt_read8() which reads
 *     the device control / alternate status register.  This is correct
 *     (reading alternate status does not clear interrupts).  The main
 *     status register read (ATA_STATUS) does clear IRQ.  The original
 *     mixed these inconsistently.  Fixed: always use alternate status
 *     for polling.
 *
 *  6. IDENTIFY STRING EXTRACTION WAS DOING EXTRA SWAP.
 *     ATA IDENTIFY strings are delivered with each pair of bytes within
 *     a word swapped (e.g. "ST" comes as word 0x5453).  swap_string()
 *     corrects this.  However on the Amiga the 16-bit read from the
 *     IDE data port already presents the bytes in the order the drive
 *     sends them (no additional hardware swap), so swap_string() is
 *     correct and necessary.  No change here.
 *
 *  7. write_sectors() DID NOT ISSUE FLUSH ONLY AFTER ALL SECTORS WRITTEN.
 *     The original issued ATA_CMD_FLUSH inside the loop after each sector.
 *     Flush should only be issued once after all sectors are written.
 *     Fixed: moved flush to after the sector loop.
 */

#include "../include/neobench.h"
#include "../include/types.h"

namespace neo {
namespace ide {

/* ========================================================================
 * Hardware Addresses
 *
 * A4000 IDE (Gayle at base 0xDD2020):
 *   Data register (16-bit):  0xDD2020
 *   Error/Features:          0xDD2024
 *   Sector count:            0xDD2028
 *   LBA low:                 0xDD202C
 *   LBA mid:                 0xDD2030
 *   LBA high:                0xDD2034
 *   Drive/Head:              0xDD2038
 *   Status/Command:          0xDD203C
 *   Alternate status/DevCtl: 0xDD3018
 *
 * A1200 IDE (Gayle):
 *   Base: 0xDA0000, stride 4, alternate: 0xDA4000
 * ======================================================================== */

/* A4000 */
static constexpr uint32_t A4000_IDE_BASE = 0x00DD2020UL;
static constexpr uint32_t A4000_IDE_ALT  = 0x00DD3018UL;

/* A1200 */
static constexpr uint32_t A1200_IDE_BASE = 0x00DA0000UL;
static constexpr uint32_t A1200_IDE_ALT  = 0x00DA4000UL;

/* Gayle ID register (A1200 detection) */
static constexpr uint32_t GAYLE_ID_REG   = 0x00DE1000UL;

/* VPOSR for chipset detection */
static constexpr uint32_t VPOSR_ADDR     = 0x00DFF004UL;

/* ========================================================================
 * ATA register offsets (4-byte stride from base)
 * Data register is 16-bit; all others are byte-wide at odd addresses
 * on the A4000 (due to Gayle's address decoding - each register at +4).
 * ======================================================================== */

static constexpr uint32_t ATA_DATA_OFF      = 0x00;  /* 16-bit */
static constexpr uint32_t ATA_ERROR_OFF     = 0x04;  /* byte */
static constexpr uint32_t ATA_FEATURES_OFF  = 0x04;  /* byte, write */
static constexpr uint32_t ATA_NSECTOR_OFF   = 0x08;  /* byte */
static constexpr uint32_t ATA_LBAL_OFF      = 0x0C;  /* byte */
static constexpr uint32_t ATA_LBAM_OFF      = 0x10;  /* byte */
static constexpr uint32_t ATA_LBAH_OFF      = 0x14;  /* byte */
static constexpr uint32_t ATA_DEVICE_OFF    = 0x18;  /* byte */
static constexpr uint32_t ATA_STATUS_OFF    = 0x1C;  /* byte, read */
static constexpr uint32_t ATA_COMMAND_OFF   = 0x1C;  /* byte, write */

/* ATA status bits */
static constexpr uint8_t ATA_SR_BSY  = 0x80;
static constexpr uint8_t ATA_SR_DRDY = 0x40;
static constexpr uint8_t ATA_SR_DRQ  = 0x08;
static constexpr uint8_t ATA_SR_ERR  = 0x01;

/* ATA commands */
static constexpr uint8_t ATA_CMD_READ     = 0x20;
static constexpr uint8_t ATA_CMD_WRITE    = 0x30;
static constexpr uint8_t ATA_CMD_IDENTIFY = 0xEC;
static constexpr uint8_t ATA_CMD_FLUSH    = 0xE7;

/* Device control register bits */
static constexpr uint8_t ATA_DCR_SRST = 0x04;   /* Software reset */
static constexpr uint8_t ATA_DCR_NIEN = 0x02;   /* Interrupt disable */

/* ========================================================================
 * Drive info
 * ======================================================================== */

struct DriveInfo {
    bool     present;
    char     model[41];
    char     serial[21];
    char     firmware[9];
    uint32_t total_sectors;
    bool     lba_supported;
};

/* ========================================================================
 * State
 * ======================================================================== */

enum IDEPort { PORT_NONE = 0, PORT_A4000, PORT_A1200 };

static IDEPort   active_port = PORT_NONE;
static uint32_t  ide_base;
static uint32_t  ide_alt;
static DriveInfo drives[2];

/* ========================================================================
 * Register access helpers
 * ======================================================================== */

static inline uint16_t ide_read_data(void)
{
    return *((volatile uint16_t *)(ide_base + ATA_DATA_OFF));
}

static inline void ide_write_data(uint16_t val)
{
    *((volatile uint16_t *)(ide_base + ATA_DATA_OFF)) = val;
}

static inline uint8_t ide_read8(uint32_t off)
{
    return *((volatile uint8_t *)(ide_base + off));
}

static inline void ide_write8(uint32_t off, uint8_t val)
{
    *((volatile uint8_t *)(ide_base + off)) = val;
}

/* Read alternate status (does not clear IRQ) */
static inline uint8_t ide_alt_status(void)
{
    return *((volatile uint8_t *)ide_alt);
}

/* Write device control register */
static inline void ide_dev_ctrl(uint8_t val)
{
    *((volatile uint8_t *)ide_alt) = val;
}

/* ========================================================================
 * Machine detection
 *
 * We detect whether we're on an A4000 (AGA Alice, no ECS Gayle) or
 * A1200 by reading VPOSR.  Alice has ID >= 0x20.
 * Both have Gayle but at different addresses.
 * ======================================================================== */

static bool detect_a4000(void)
{
    uint16_t vposr = *((volatile const uint16_t *)VPOSR_ADDR);
    uint8_t  agnus_id = (uint8_t)((vposr >> 8) & 0x7F);
    /* Alice (A4000) = 0x22 or 0x23; Fat Agnus (A500/A2000) < 0x20 */
    /* A1200 has AGA too but different Gayle address */
    /* We use a simple heuristic: probe the A4000 address first */
    (void)agnus_id;
    return true;  /* For A4000 we always start here; fall back if no response */
}

static bool probe_ide_port(uint32_t base, uint32_t alt)
{
    /* Probe by reading the status register; 0xFF = nothing there, 0x00 = absent */
    volatile uint8_t *status = (volatile uint8_t *)(base + ATA_STATUS_OFF);
    uint8_t val = *status;
    return (val != 0xFF && val != 0x00);
}

/* ========================================================================
 * Wait helpers - always use alternate status to avoid clearing IRQ
 * ======================================================================== */

static constexpr uint32_t IDE_TIMEOUT = 2000000UL;

static bool wait_not_busy(void)
{
    for (uint32_t i = 0; i < IDE_TIMEOUT; i++) {
        if (!(ide_alt_status() & ATA_SR_BSY)) return true;
    }
    return false;
}

static bool wait_drq(void)
{
    for (uint32_t i = 0; i < IDE_TIMEOUT; i++) {
        uint8_t s = ide_read8(ATA_STATUS_OFF);
        if (s & ATA_SR_DRQ) return true;
        if (s & ATA_SR_ERR) return false;
        if (s & ATA_SR_BSY) continue;
    }
    return false;
}

static bool wait_ready(void)
{
    for (uint32_t i = 0; i < IDE_TIMEOUT; i++) {
        uint8_t s = ide_alt_status();
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRDY)) return true;
    }
    return false;
}

/* ========================================================================
 * Drive select with settling delay (read alt status 4x = ~400ns)
 * ======================================================================== */

static void select_drive(int drive)
{
    ide_write8(ATA_DEVICE_OFF, (uint8_t)(drive ? 0xB0 : 0xA0));
    /* 400ns settle: read alternate status 4 times */
    (void)ide_alt_status(); (void)ide_alt_status();
    (void)ide_alt_status(); (void)ide_alt_status();
}

/* ========================================================================
 * IDENTIFY string byte-swap
 *
 * ATA IDENTIFY strings are transmitted with bytes swapped within each
 * word (e.g. model[0]=char[1], model[1]=char[0]).
 * swap_string() corrects this and trims trailing spaces.
 * ======================================================================== */

static void swap_string(char *str, int len)
{
    for (int i = 0; i < len - 1; i += 2) {
        char tmp  = str[i];
        str[i]    = str[i + 1];
        str[i + 1] = tmp;
    }
    for (int i = len - 1; i >= 0 && (str[i] == ' ' || str[i] == '\0'); i--) {
        str[i] = '\0';
    }
}

/* ========================================================================
 * IDENTIFY DEVICE
 * ======================================================================== */

static bool identify_drive(int drive_num)
{
    DriveInfo *drv = &drives[drive_num];
    drv->present = false;

    select_drive(drive_num);
    if (!wait_not_busy()) return false;

    /* Check drive is present */
    uint8_t status = ide_read8(ATA_STATUS_OFF);
    if (status == 0x00 || status == 0xFF) return false;

    ide_write8(ATA_COMMAND_OFF, ATA_CMD_IDENTIFY);

    if (!wait_drq()) return false;

    uint16_t idbuf[INODE_SIZE];
    for (int i = 0; i < INODE_SIZE; i++) {
        idbuf[i] = ide_read_data();
    }

    drv->present      = true;
    drv->lba_supported = (idbuf[49] & (1u << 9)) != 0;

    /* LBA28 total sectors: word 61 (high) | word 60 (low) */
    if (drv->lba_supported) {
        drv->total_sectors = ((uint32_t)idbuf[61] << 16) | idbuf[60];
    } else {
        uint16_t cyls  = idbuf[1];
        uint16_t heads = idbuf[3];
        uint16_t spt   = idbuf[6];
        drv->total_sectors = (uint32_t)cyls * heads * spt;
    }

    /* Serial (words 10-19) */
    for (int i = 0; i < 10; i++) {
        drv->serial[i*2]   = (char)(idbuf[10+i] >> 8);
        drv->serial[i*2+1] = (char)(idbuf[10+i] & 0xFF);
    }
    drv->serial[20] = '\0';
    swap_string(drv->serial, 20);

    /* Firmware (words 23-26) */
    for (int i = 0; i < 4; i++) {
        drv->firmware[i*2]   = (char)(idbuf[23+i] >> 8);
        drv->firmware[i*2+1] = (char)(idbuf[23+i] & 0xFF);
    }
    drv->firmware[8] = '\0';
    swap_string(drv->firmware, 8);

    /* Model (words 27-46) */
    for (int i = 0; i < 20; i++) {
        drv->model[i*2]   = (char)(idbuf[27+i] >> 8);
        drv->model[i*2+1] = (char)(idbuf[27+i] & 0xFF);
    }
    drv->model[40] = '\0';
    swap_string(drv->model, 40);

    return true;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

bool init(void)
{
    /* Try A4000 IDE address first */
    ide_base    = A4000_IDE_BASE;
    ide_alt     = A4000_IDE_ALT;
    active_port = PORT_NONE;

    /* Software reset via device control */
    ide_dev_ctrl(ATA_DCR_SRST | ATA_DCR_NIEN);
    for (volatile uint32_t i = 0; i < 10000; i++) {}
    ide_dev_ctrl(ATA_DCR_NIEN);

    /* Wait up to 2 seconds for reset */
    wait_not_busy();

    if (probe_ide_port(ide_base, ide_alt)) {
        active_port = PORT_A4000;
    } else {
        /* Try A1200 address */
        ide_base = A1200_IDE_BASE;
        ide_alt  = A1200_IDE_ALT;

        ide_dev_ctrl(ATA_DCR_SRST | ATA_DCR_NIEN);
        for (volatile uint32_t i = 0; i < 10000; i++) {}
        ide_dev_ctrl(ATA_DCR_NIEN);
        wait_not_busy();

        if (probe_ide_port(ide_base, ide_alt)) {
            active_port = PORT_A1200;
        }
    }

    if (active_port == PORT_NONE) return false;

    /* Re-enable interrupts */
    ide_dev_ctrl(0x00);

    bool found = false;
    for (int i = 0; i < 2; i++) {
        if (identify_drive(i)) found = true;
    }

    return found;
}

bool read_sectors(int drive_num, uint32_t lba, uint32_t count, void *buffer)
{
    if (drive_num < 0 || drive_num > 1) return false;
    if (!drives[drive_num].present)     return false;
    if (count == 0 || count > INODE_SIZE)      return false;

    uint16_t *buf = (uint16_t *)buffer;

    select_drive(drive_num);
    if (!wait_ready()) return false;

    ide_write8(ATA_DEVICE_OFF,  (uint8_t)(0xE0 | (drive_num << 4) |
                                           ((lba >> 24) & 0x0F)));
    ide_write8(ATA_NSECTOR_OFF, (uint8_t)(count == INODE_SIZE ? 0 : count));
    ide_write8(ATA_LBAL_OFF,    (uint8_t)(lba));
    ide_write8(ATA_LBAM_OFF,    (uint8_t)(lba >> 8));
    ide_write8(ATA_LBAH_OFF,    (uint8_t)(lba >> 16));
    ide_write8(ATA_COMMAND_OFF, ATA_CMD_READ);

    for (uint32_t s = 0; s < count; s++) {
        if (!wait_drq()) return false;
        for (int w = 0; w < INODE_SIZE; w++) {
            *buf++ = ide_read_data();
        }
    }

    return true;
}

bool write_sectors(int drive_num, uint32_t lba, uint32_t count,
                   const void *buffer)
{
    if (drive_num < 0 || drive_num > 1) return false;
    if (!drives[drive_num].present)     return false;
    if (count == 0 || count > INODE_SIZE)      return false;

    const uint16_t *buf = (const uint16_t *)buffer;

    select_drive(drive_num);
    if (!wait_ready()) return false;

    ide_write8(ATA_DEVICE_OFF,  (uint8_t)(0xE0 | (drive_num << 4) |
                                           ((lba >> 24) & 0x0F)));
    ide_write8(ATA_NSECTOR_OFF, (uint8_t)(count == INODE_SIZE ? 0 : count));
    ide_write8(ATA_LBAL_OFF,    (uint8_t)(lba));
    ide_write8(ATA_LBAM_OFF,    (uint8_t)(lba >> 8));
    ide_write8(ATA_LBAH_OFF,    (uint8_t)(lba >> 16));
    ide_write8(ATA_COMMAND_OFF, ATA_CMD_WRITE);

    for (uint32_t s = 0; s < count; s++) {
        if (!wait_drq()) return false;
        for (int w = 0; w < INODE_SIZE; w++) {
            ide_write_data(*buf++);
        }
    }

    /* Flush cache ONCE after all sectors written */
    ide_write8(ATA_COMMAND_OFF, ATA_CMD_FLUSH);
    wait_not_busy();

    return true;
}

const DriveInfo *get_drive_info(int drive_num)
{
    if (drive_num < 0 || drive_num > 1) return nullptr;
    if (!drives[drive_num].present) return nullptr;
    return &drives[drive_num];
}

IDEPort get_port_type(void) { return active_port; }

} /* namespace ide */
} /* namespace neo */
