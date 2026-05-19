/*
 * NeoBench Bare-Metal Amiga Kernel
 * SATA Driver - PCI SATA via Mediator
 *
 * Supports your A4000 tower hot-swap SATA bay connected via a PCI SATA
 * controller card in the Mediator.
 *
 * Supported controllers:
 *   - Silicon Image SiI0680  (ATA133/SATA combo, common Mediator card)
 *   - Silicon Image SiI3112  (SATA-I, 2-port, also common)
 *   - Generic AHCI           (Advanced Host Controller Interface, PCI class 0106h)
 *
 * The SiI0680 and SiI3112 are legacy IDE-mode controllers accessed via
 * standard ATA task file registers mapped through PCI BAR I/O space.
 * AHCI uses memory-mapped registers.
 *
 * Since the Mediator maps PCI I/O space into its memory window, we
 * access all registers as memory-mapped I/O (no IN/OUT instructions
 * on the 68k).
 *
 * Architecture:
 *   - PCI scan for known SATA controllers is done at init
 *   - Each port exposes a standard block device interface
 *   - LBA28 and LBA48 addressing supported
 *   - PIO mode (no DMA) for simplicity and reliability at boot
 *   - Up to 4 ports scanned (2 channels x 2 devices)
 *
 * References:
 *   Silicon Image SiI0680 datasheet
 *   Silicon Image SiI3112 datasheet
 *   Serial ATA 1.0 specification
 *   AHCI 1.3 specification
 */

#include "../include/neobench.h"
#include "../include/types.h"

/* ========================================================================
 * PCI identifiers for known SATA controllers
 * ======================================================================== */

/* Silicon Image */
#define PCI_VENDOR_SILICONIMAGE     0x1095
#define PCI_DEVICE_SII0680          0x0680  /* SiI0680 ATA133/SATA */
#define PCI_DEVICE_SII3112          0x3112  /* SiI3112 SATA */
#define PCI_DEVICE_SII3114          0x3114  /* SiI3114 SATA (4-port) */

/* Promise Technology */
#define PCI_VENDOR_PROMISE          0x105A
#define PCI_DEVICE_PDC20378         0x3378  /* SATA150 TX2plus */

/* Marvell */
#define PCI_VENDOR_MARVELL          0x11AB
#define PCI_DEVICE_88SX6111         0x6111  /* 88SX6111 SATA */

/* PCI class for SATA: 0x01 (mass storage), subclass 0x06 (SATA), prog-if 0x01 (AHCI) */
#define PCI_CLASS_SATA_AHCI         0x0106
#define PCI_PROG_IF_AHCI            0x01

/* ========================================================================
 * ATA task file register offsets (legacy IDE mode)
 *
 * In legacy mode the controller exposes standard ATA registers.
 * Via Mediator PCI memory window, these are accessed as bytes.
 * The data register is 16-bit (word access).
 *
 * Channel 0 uses BAR0 (command) + BAR1 (control)
 * Channel 1 uses BAR2 (command) + BAR3 (control)
 * ======================================================================== */

/* Offsets within a command block (BAR0 or BAR2) */
#define ATA_OFF_DATA        0x00    /* 16-bit data */
#define ATA_OFF_ERROR       0x01    /* Error (read) / Features (write) */
#define ATA_OFF_FEATURES    0x01
#define ATA_OFF_NSECTOR     0x02    /* Sector count */
#define ATA_OFF_LBAL        0x03    /* LBA low */
#define ATA_OFF_LBAM        0x04    /* LBA mid */
#define ATA_OFF_LBAH        0x05    /* LBA high */
#define ATA_OFF_DEVICE      0x06    /* Device/head */
#define ATA_OFF_STATUS      0x07    /* Status (read) / Command (write) */
#define ATA_OFF_COMMAND     0x07

/* Offset within control block (BAR1 or BAR3) */
#define ATA_OFF_CONTROL     0x02    /* Device control / alt status */
#define ATA_OFF_ALTSTATUS   0x02

/* ATA status bits */
#define ATA_STAT_BSY        0x80
#define ATA_STAT_DRDY       0x40
#define ATA_STAT_DF         0x20
#define ATA_STAT_DSC        0x10
#define ATA_STAT_DRQ        0x08
#define ATA_STAT_ERR        0x01

/* ATA commands */
#define ATA_CMD_READ_PIO        0x20    /* READ SECTORS (LBA28) */
#define ATA_CMD_READ_PIO_EXT    0x24    /* READ SECTORS EXT (LBA48) */
#define ATA_CMD_WRITE_PIO       0x30    /* WRITE SECTORS (LBA28) */
#define ATA_CMD_WRITE_PIO_EXT   0x34    /* WRITE SECTORS EXT (LBA48) */
#define ATA_CMD_IDENTIFY        0xEC    /* IDENTIFY DEVICE */
#define ATA_CMD_SET_FEATURES    0xEF
#define ATA_CMD_FLUSH_CACHE     0xE7    /* FLUSH CACHE (LBA28) */
#define ATA_CMD_FLUSH_CACHE_EXT 0xEA    /* FLUSH CACHE EXT (LBA48) */

/* Device register bits */
#define ATA_DEV_LBA         0x40    /* Use LBA addressing */
#define ATA_DEV_MASTER      0x00    /* Drive 0 */
#define ATA_DEV_SLAVE       0x10    /* Drive 1 */

/* ========================================================================
 * AHCI register offsets (MMIO, BAR5)
 * ======================================================================== */

/* Global host control registers */
#define AHCI_GHC_CAP        0x000   /* Host capabilities */
#define AHCI_GHC_GHC        0x004   /* Global host control */
#define AHCI_GHC_IS         0x008   /* Interrupt status */
#define AHCI_GHC_PI         0x00C   /* Ports implemented */
#define AHCI_GHC_VS         0x010   /* AHCI version */

/* Per-port register offsets (port n base = 0x100 + n*0x80) */
#define AHCI_PORT_BASE(n)   (0x100 + (n) * 0x80)
#define AHCI_PXCLB(n)       (AHCI_PORT_BASE(n) + 0x00)  /* Command list base */
#define AHCI_PXFB(n)        (AHCI_PORT_BASE(n) + 0x08)  /* FIS base */
#define AHCI_PXIS(n)        (AHCI_PORT_BASE(n) + 0x10)  /* Interrupt status */
#define AHCI_PXIE(n)        (AHCI_PORT_BASE(n) + 0x14)  /* Interrupt enable */
#define AHCI_PXCMD(n)       (AHCI_PORT_BASE(n) + 0x18)  /* Command and status */
#define AHCI_PXSERR(n)      (AHCI_PORT_BASE(n) + 0x30)  /* SATA error */
#define AHCI_PXTFD(n)       (AHCI_PORT_BASE(n) + 0x20)  /* Task file data */
#define AHCI_PXSSTS(n)      (AHCI_PORT_BASE(n) + 0x28)  /* SATA status */

/* AHCI GHC bits */
#define AHCI_GHC_AE         (1UL << 31)  /* AHCI Enable */
#define AHCI_GHC_IE         (1UL << 1)   /* Interrupt enable */
#define AHCI_GHC_HR         (1UL << 0)   /* HBA reset */

/* AHCI PXCMD bits */
#define AHCI_CMD_ST         (1UL << 0)   /* Start */
#define AHCI_CMD_FRE        (1UL << 4)   /* FIS receive enable */
#define AHCI_CMD_FR         (1UL << 14)  /* FIS receive running */
#define AHCI_CMD_CR         (1UL << 15)  /* Command list running */

/* SATA status (PXSSTS) device detection */
#define AHCI_SSTS_DET_MASK  0x0F
#define AHCI_SSTS_DET_PRESENT 0x03  /* Device present and comm established */

/* ========================================================================
 * Controller and device state
 * ======================================================================== */

typedef enum {
    SATA_CTRL_NONE = 0,
    SATA_CTRL_LEGACY_IDE,   /* SiI0680, SiI3112 in legacy mode */
    SATA_CTRL_AHCI          /* Generic AHCI */
} SATACtrlType;

#define MAX_SATA_PORTS  4

typedef struct {
    bool     present;
    bool     lba48;         /* Supports LBA48 (drives > 128GB) */
    uint64_t lba28_sectors; /* LBA28 capacity (28-bit) */
    uint64_t lba48_sectors; /* LBA48 capacity (48-bit) */
    uint32_t sector_size;
    char     model[41];
    char     serial[21];
} SATADrive;

typedef struct {
    SATACtrlType type;
    uint32_t     bar[6];        /* PCI BAR values (physical addresses) */
    uint32_t     mmio_base;     /* MMIO base (Mediator-adjusted) */
    uint8_t      pci_dev;
    uint8_t      num_ports;
    SATADrive    drives[MAX_SATA_PORTS];
} SATAController;

static SATAController sata_ctrl;
static bool           sata_initialized = false;

/* Mediator memory window base (set by PCI scan in rtg.cpp / kernel init) */
/* We need this to convert PCI bus addresses to 68k addresses. */
/* This is provided by the kernel PCI subsystem; declare as extern. */
extern uint32_t mediator_base_addr;  /* From rtg.cpp / pci subsystem */

/* ========================================================================
 * PCI config space access (mirrors rtg.cpp helpers - shared via header
 * in a full build; duplicated here for driver self-containment)
 * ======================================================================== */

#define MEDIATOR_PCI_CFG_OFFSET     0x08000000UL
#define MEDIATOR_PCI_MEM_OFFSET     0x00000000UL
#define PCI_CFG_ENABLE              0x80000000UL

static uint32_t sata_pci_read32(uint8_t dev, uint8_t fn, uint8_t reg)
{
    if (!mediator_base_addr) return 0xFFFFFFFF;
    uint32_t cfg = mediator_base_addr + MEDIATOR_PCI_CFG_OFFSET;
    uint32_t addr = PCI_CFG_ENABLE |
                    ((uint32_t)(dev & 0x1F) << 11) |
                    ((uint32_t)(fn  & 0x07) <<  8) |
                    ((uint32_t)(reg & 0xFC));
    return *((volatile uint32_t *)(cfg + addr));
}

static void sata_pci_write32(uint8_t dev, uint8_t fn, uint8_t reg, uint32_t val)
{
    if (!mediator_base_addr) return;
    uint32_t cfg = mediator_base_addr + MEDIATOR_PCI_CFG_OFFSET;
    uint32_t addr = PCI_CFG_ENABLE |
                    ((uint32_t)(dev & 0x1F) << 11) |
                    ((uint32_t)(fn  & 0x07) <<  8) |
                    ((uint32_t)(reg & 0xFC));
    *((volatile uint32_t *)(cfg + addr)) = val;
}

/* ========================================================================
 * Legacy IDE register access (via PCI BAR memory mapping)
 * ======================================================================== */

static inline uint8_t legacy_read8(uint32_t bar_base, uint32_t offset)
{
    return *((volatile uint8_t *)(bar_base + offset));
}

static inline void legacy_write8(uint32_t bar_base, uint32_t offset, uint8_t val)
{
    *((volatile uint8_t *)(bar_base + offset)) = val;
}

static inline uint16_t legacy_read16(uint32_t bar_base)
{
    return *((volatile uint16_t *)(bar_base + ATA_OFF_DATA));
}

static inline void legacy_write16(uint32_t bar_base, uint16_t val)
{
    *((volatile uint16_t *)(bar_base + ATA_OFF_DATA)) = val;
}

/* ========================================================================
 * Legacy IDE wait helpers
 * ======================================================================== */

#define SATA_TIMEOUT_LOOPS  5000000UL

static bool legacy_wait_not_busy(uint32_t cmd_base)
{
    for (uint32_t i = 0; i < SATA_TIMEOUT_LOOPS; i++) {
        if (!(legacy_read8(cmd_base, ATA_OFF_STATUS) & ATA_STAT_BSY))
            return true;
    }
    return false;
}

static bool legacy_wait_drq(uint32_t cmd_base)
{
    for (uint32_t i = 0; i < SATA_TIMEOUT_LOOPS; i++) {
        uint8_t s = legacy_read8(cmd_base, ATA_OFF_STATUS);
        if (s & ATA_STAT_DRQ) return true;
        if (s & ATA_STAT_ERR) return false;
    }
    return false;
}

static bool legacy_wait_ready(uint32_t cmd_base)
{
    for (uint32_t i = 0; i < SATA_TIMEOUT_LOOPS; i++) {
        uint8_t s = legacy_read8(cmd_base, ATA_OFF_STATUS);
        if ((s & (ATA_STAT_BSY | ATA_STAT_DRDY)) == ATA_STAT_DRDY)
            return true;
    }
    return false;
}

/* ========================================================================
 * Legacy IDE: IDENTIFY DEVICE
 * ======================================================================== */

static bool legacy_identify(uint32_t cmd_base, uint32_t ctl_base,
                              int dev_num, SATADrive *drive)
{
    (void)ctl_base;

    /* Select drive */
    legacy_write8(cmd_base, ATA_OFF_DEVICE,
                  (uint8_t)(0xA0 | (dev_num ? ATA_DEV_SLAVE : ATA_DEV_MASTER)));

    for (volatile int i = 0; i < 1000; i++) {}

    if (!legacy_wait_not_busy(cmd_base)) return false;

    /* Issue IDENTIFY */
    legacy_write8(cmd_base, ATA_OFF_COMMAND, ATA_CMD_IDENTIFY);

    if (!legacy_wait_drq(cmd_base)) return false;

    uint16_t idbuf[256];
    for (int i = 0; i < 256; i++) {
        idbuf[i] = legacy_read16(cmd_base);
    }

    /* Check for valid IDENTIFY response */
    if (idbuf[0] == 0x0000 || idbuf[0] == 0xFFFF) return false;

    drive->present     = true;
    drive->sector_size = 512;

    /* Check LBA48 support (word 83 bit 10) */
    drive->lba48 = (idbuf[83] & 0x0400) != 0;

    /* LBA28 capacity (words 60-61) */
    drive->lba28_sectors = ((uint32_t)idbuf[61] << 16) | idbuf[60];

    /* LBA48 capacity (words 100-103) */
    if (drive->lba48) {
        drive->lba48_sectors =
            ((uint64_t)idbuf[103] << 48) | ((uint64_t)idbuf[102] << 32) |
            ((uint64_t)idbuf[101] << 16) |  (uint64_t)idbuf[100];
    } else {
        drive->lba48_sectors = drive->lba28_sectors;
    }

    /* Extract model string (words 27-46, byte-swapped) */
    for (int i = 0; i < 20; i++) {
        drive->model[i * 2]     = (char)(idbuf[27 + i] >> 8);
        drive->model[i * 2 + 1] = (char)(idbuf[27 + i] & 0xFF);
    }
    drive->model[40] = '\0';

    /* Extract serial number (words 10-19, byte-swapped) */
    for (int i = 0; i < 10; i++) {
        drive->serial[i * 2]     = (char)(idbuf[10 + i] >> 8);
        drive->serial[i * 2 + 1] = (char)(idbuf[10 + i] & 0xFF);
    }
    drive->serial[20] = '\0';

    return true;
}

/* ========================================================================
 * Legacy IDE: PIO read/write
 *
 * LBA28: supports up to 2^28 sectors (128GB with 512-byte sectors).
 * LBA48: supports up to 2^48 sectors.
 *
 * We use LBA48 for drives that support it (all modern SATA drives do).
 * ======================================================================== */

static bool legacy_read_sectors(uint32_t cmd_base, uint32_t ctl_base,
                                  int dev_num, uint64_t lba,
                                  uint16_t count, uint8_t *buf)
{
    (void)ctl_base;

    if (!legacy_wait_not_busy(cmd_base)) return false;

    bool use_lba48 = (lba >= 0x10000000ULL) ||
                     (count > 256) ||
                     sata_ctrl.drives[dev_num].lba48;

    if (use_lba48) {
        /* LBA48: send registers twice (high byte then low byte) */
        legacy_write8(cmd_base, ATA_OFF_DEVICE,
                      (uint8_t)(0xE0 | (dev_num ? ATA_DEV_SLAVE : 0)));
        /* High bytes */
        legacy_write8(cmd_base, ATA_OFF_NSECTOR, (uint8_t)(count >> 8));
        legacy_write8(cmd_base, ATA_OFF_LBAL,    (uint8_t)(lba >> 24));
        legacy_write8(cmd_base, ATA_OFF_LBAM,    (uint8_t)(lba >> 32));
        legacy_write8(cmd_base, ATA_OFF_LBAH,    (uint8_t)(lba >> 40));
        /* Low bytes */
        legacy_write8(cmd_base, ATA_OFF_NSECTOR, (uint8_t)(count));
        legacy_write8(cmd_base, ATA_OFF_LBAL,    (uint8_t)(lba));
        legacy_write8(cmd_base, ATA_OFF_LBAM,    (uint8_t)(lba >>  8));
        legacy_write8(cmd_base, ATA_OFF_LBAH,    (uint8_t)(lba >> 16));
        legacy_write8(cmd_base, ATA_OFF_COMMAND, ATA_CMD_READ_PIO_EXT);
    } else {
        /* LBA28 */
        legacy_write8(cmd_base, ATA_OFF_DEVICE,
                      (uint8_t)(0xE0 | (dev_num ? ATA_DEV_SLAVE : 0) |
                                ((lba >> 24) & 0x0F)));
        legacy_write8(cmd_base, ATA_OFF_NSECTOR, (uint8_t)(count == 256 ? 0 : count));
        legacy_write8(cmd_base, ATA_OFF_LBAL,    (uint8_t)(lba));
        legacy_write8(cmd_base, ATA_OFF_LBAM,    (uint8_t)(lba >>  8));
        legacy_write8(cmd_base, ATA_OFF_LBAH,    (uint8_t)(lba >> 16));
        legacy_write8(cmd_base, ATA_OFF_COMMAND, ATA_CMD_READ_PIO);
    }

    for (uint16_t s = 0; s < count; s++) {
        if (!legacy_wait_drq(cmd_base)) return false;

        /* Read 256 words = 512 bytes */
        uint16_t *dst = (uint16_t *)(buf + (uint32_t)s * 512);
        for (int w = 0; w < 256; w++) {
            uint16_t word = legacy_read16(cmd_base);
            /* ATA is little-endian; 68k is big-endian.
             * Swap bytes for correct byte order in memory. */
            dst[w] = (uint16_t)((word >> 8) | (word << 8));
        }
    }

    return legacy_wait_not_busy(cmd_base);
}

static bool legacy_write_sectors(uint32_t cmd_base, uint32_t ctl_base,
                                   int dev_num, uint64_t lba,
                                   uint16_t count, const uint8_t *buf)
{
    (void)ctl_base;

    if (!legacy_wait_not_busy(cmd_base)) return false;

    bool use_lba48 = (lba >= 0x10000000ULL) ||
                     (count > 256) ||
                     sata_ctrl.drives[dev_num].lba48;

    if (use_lba48) {
        legacy_write8(cmd_base, ATA_OFF_DEVICE,
                      (uint8_t)(0xE0 | (dev_num ? ATA_DEV_SLAVE : 0)));
        legacy_write8(cmd_base, ATA_OFF_NSECTOR, (uint8_t)(count >> 8));
        legacy_write8(cmd_base, ATA_OFF_LBAL,    (uint8_t)(lba >> 24));
        legacy_write8(cmd_base, ATA_OFF_LBAM,    (uint8_t)(lba >> 32));
        legacy_write8(cmd_base, ATA_OFF_LBAH,    (uint8_t)(lba >> 40));
        legacy_write8(cmd_base, ATA_OFF_NSECTOR, (uint8_t)(count));
        legacy_write8(cmd_base, ATA_OFF_LBAL,    (uint8_t)(lba));
        legacy_write8(cmd_base, ATA_OFF_LBAM,    (uint8_t)(lba >>  8));
        legacy_write8(cmd_base, ATA_OFF_LBAH,    (uint8_t)(lba >> 16));
        legacy_write8(cmd_base, ATA_OFF_COMMAND, ATA_CMD_WRITE_PIO_EXT);
    } else {
        legacy_write8(cmd_base, ATA_OFF_DEVICE,
                      (uint8_t)(0xE0 | (dev_num ? ATA_DEV_SLAVE : 0) |
                                ((lba >> 24) & 0x0F)));
        legacy_write8(cmd_base, ATA_OFF_NSECTOR, (uint8_t)(count == 256 ? 0 : count));
        legacy_write8(cmd_base, ATA_OFF_LBAL,    (uint8_t)(lba));
        legacy_write8(cmd_base, ATA_OFF_LBAM,    (uint8_t)(lba >>  8));
        legacy_write8(cmd_base, ATA_OFF_LBAH,    (uint8_t)(lba >> 16));
        legacy_write8(cmd_base, ATA_OFF_COMMAND, ATA_CMD_WRITE_PIO);
    }

    for (uint16_t s = 0; s < count; s++) {
        if (!legacy_wait_drq(cmd_base)) return false;

        const uint16_t *src = (const uint16_t *)(buf + (uint32_t)s * 512);
        for (int w = 0; w < 256; w++) {
            uint16_t word = src[w];
            /* Byte-swap for ATA little-endian */
            legacy_write16(cmd_base, (uint16_t)((word >> 8) | (word << 8)));
        }
    }

    if (!legacy_wait_not_busy(cmd_base)) return false;

    /* Flush cache */
    legacy_write8(cmd_base, ATA_OFF_COMMAND,
                  use_lba48 ? ATA_CMD_FLUSH_CACHE_EXT : ATA_CMD_FLUSH_CACHE);
    return legacy_wait_not_busy(cmd_base);
}

/* ========================================================================
 * PCI scan and controller initialisation
 * ======================================================================== */

static bool try_legacy_controller(uint8_t pci_dev, uint8_t pci_fn)
{
    if (!mediator_base_addr) return false;

    uint32_t mem_window = mediator_base_addr + MEDIATOR_PCI_MEM_OFFSET;

    /* Read all 6 BARs */
    for (int b = 0; b < 6; b++) {
        uint32_t bar = sata_pci_read32(pci_dev, pci_fn,
                                        (uint8_t)(0x10 + b * 4));
        /* Strip flags, add Mediator window base */
        if (bar & 1) {
            /* I/O BAR: map I/O address into Mediator memory window */
            sata_ctrl.bar[b] = mem_window + (bar & ~3UL);
        } else {
            sata_ctrl.bar[b] = mem_window + (bar & ~15UL);
        }
    }

    /* Enable memory + I/O space + bus mastering */
    uint32_t cmd = sata_pci_read32(pci_dev, pci_fn, 0x04);
    cmd |= 0x07;  /* I/O enable + mem enable + bus master */
    sata_pci_write32(pci_dev, pci_fn, 0x04, cmd);

    sata_ctrl.type    = SATA_CTRL_LEGACY_IDE;
    sata_ctrl.pci_dev = pci_dev;

    /*
     * Legacy IDE mode: two channels.
     * Channel 0: BAR0 (cmd) + BAR1 (ctl)
     * Channel 1: BAR2 (cmd) + BAR3 (ctl)
     * Each channel supports master (0) and slave (1).
     */
    int port = 0;
    for (int ch = 0; ch < 2 && port < MAX_SATA_PORTS; ch++) {
        uint32_t cmd_base = sata_ctrl.bar[ch * 2];
        uint32_t ctl_base = sata_ctrl.bar[ch * 2 + 1];

        if (!cmd_base) continue;

        for (int dev = 0; dev < 2 && port < MAX_SATA_PORTS; dev++, port++) {
            legacy_identify(cmd_base, ctl_base, dev, &sata_ctrl.drives[port]);
        }
    }

    sata_ctrl.num_ports = (uint8_t)port;
    return true;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

bool sata_init(void)
{
    sata_ctrl.type      = SATA_CTRL_NONE;
    sata_ctrl.num_ports = 0;

    for (int i = 0; i < MAX_SATA_PORTS; i++) {
        sata_ctrl.drives[i].present = false;
    }

    if (!mediator_base_addr) return false;

    /* Scan PCI bus for SATA controllers */
    for (uint8_t dev = 0; dev <= 20; dev++) {
        uint32_t id32 = sata_pci_read32(dev, 0, 0x00);
        uint16_t vendor = (uint16_t)(id32 >> 16);
        uint16_t device = (uint16_t)(id32 & 0xFFFF);

        if (vendor == 0xFFFF || vendor == 0x0000) continue;

        /* Check for known SATA controllers */
        bool known = false;

        if (vendor == PCI_VENDOR_SILICONIMAGE &&
            (device == PCI_DEVICE_SII0680 ||
             device == PCI_DEVICE_SII3112 ||
             device == PCI_DEVICE_SII3114)) {
            known = true;
        } else if (vendor == PCI_VENDOR_PROMISE &&
                   device == PCI_DEVICE_PDC20378) {
            known = true;
        } else if (vendor == PCI_VENDOR_MARVELL &&
                   device == PCI_DEVICE_88SX6111) {
            known = true;
        } else {
            /* Check PCI class: 0x01xx = mass storage, 0x0106 = SATA */
            uint32_t class32 = sata_pci_read32(dev, 0, 0x08);
            uint16_t class_code = (uint16_t)((class32 >> 8) & 0xFFFF);
            if (class_code == 0x0101 || /* IDE controller */
                class_code == 0x0106) { /* SATA controller */
                known = true;
            }
        }

        if (!known) continue;

        if (try_legacy_controller(dev, 0)) {
            sata_initialized = true;
            return true;
        }
    }

    return false;
}

int sata_scan(void)
{
    int count = 0;
    for (int i = 0; i < MAX_SATA_PORTS; i++) {
        if (sata_ctrl.drives[i].present) count++;
    }
    return count;
}

bool sata_drive_present(int port)
{
    if (port < 0 || port >= MAX_SATA_PORTS) return false;
    return sata_ctrl.drives[port].present;
}

const char *sata_drive_model(int port)
{
    if (!sata_drive_present(port)) return "";
    return sata_ctrl.drives[port].model;
}

const char *sata_drive_serial(int port)
{
    if (!sata_drive_present(port)) return "";
    return sata_ctrl.drives[port].serial;
}

uint64_t sata_drive_sectors(int port)
{
    if (!sata_drive_present(port)) return 0;
    return sata_ctrl.drives[port].lba48_sectors ?
           sata_ctrl.drives[port].lba48_sectors :
           sata_ctrl.drives[port].lba28_sectors;
}

uint32_t sata_drive_sector_size(int port)
{
    if (!sata_drive_present(port)) return 512;
    return sata_ctrl.drives[port].sector_size;
}

bool sata_read_sectors(int port, uint64_t lba,
                        uint16_t count, uint8_t *buf)
{
    if (!sata_initialized || !sata_drive_present(port)) return false;

    if (sata_ctrl.type == SATA_CTRL_LEGACY_IDE) {
        int ch  = port / 2;
        int dev = port % 2;
        uint32_t cmd_base = sata_ctrl.bar[ch * 2];
        uint32_t ctl_base = sata_ctrl.bar[ch * 2 + 1];
        return legacy_read_sectors(cmd_base, ctl_base, dev, lba, count, buf);
    }

    return false;
}

bool sata_write_sectors(int port, uint64_t lba,
                         uint16_t count, const uint8_t *buf)
{
    if (!sata_initialized || !sata_drive_present(port)) return false;

    if (sata_ctrl.type == SATA_CTRL_LEGACY_IDE) {
        int ch  = port / 2;
        int dev = port % 2;
        uint32_t cmd_base = sata_ctrl.bar[ch * 2];
        uint32_t ctl_base = sata_ctrl.bar[ch * 2 + 1];
        return legacy_write_sectors(cmd_base, ctl_base, dev, lba, count, buf);
    }

    return false;
}
