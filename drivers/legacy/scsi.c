/*
 * NeoBench Bare-Metal Amiga Kernel
 * SCSI Driver - WD33C93A/B (A3000/A4000 onboard)
 *
 * The A4000 (and A3000) uses a Western Digital WD33C93A or WD33C93B
 * SCSI controller.  On the A4000 it is connected via the "Ramsey" chip
 * and accessible at 0xDD0000.
 *
 * The WD33C93 is an indirect-register chip:
 *   Write register address to ADDRESS port, then read/write DATA port.
 *   Address auto-increments after each data access if ADRP bit is set.
 *
 * This driver implements:
 *   - Controller reset and initialisation
 *   - SCSI bus scan (units 0-6, LUN 0)
 *   - Synchronous and asynchronous data transfer (polled, no DMA)
 *   - READ(10) and WRITE(10) for block devices
 *   - INQUIRY and REQUEST SENSE for device identification
 *
 * References:
 *   WD33C93A Product Specification (Western Digital, 1994)
 *   Amiga A4000 Technical Reference Manual
 *   Linux wd33c93.c driver (historical reference)
 */

#include "../include/neobench.h"
#include "../include/types.h"

/* ========================================================================
 * WD33C93 Register Addresses
 *
 * The chip has two I/O ports at the base address:
 *   BASE+0  ADDRESS register (write: set indirect register number)
 *           Bit 7 (ASR) is readable here: shows controller status
 *   BASE+2  DATA register (read/write: access indirect register)
 *
 * A4000 WD33C93 base address: 0xDD0000
 * (On A3000: 0xDD0000 as well, same chipset)
 * ======================================================================== */

#define WD33C93_BASE        0x00DD0000UL

/* Port offsets (byte-wide, but address decoding means we step by 2 or 4) */
/* On the A4000 the WD33C93 ports are at byte offsets 0 and 4 */
#define WD_ADDR_PORT        (WD33C93_BASE + 0x00)   /* Address/ASR register */
#define WD_DATA_PORT        (WD33C93_BASE + 0x04)   /* Data register */

/* ========================================================================
 * WD33C93 Indirect Register Numbers
 * ======================================================================== */

#define WD_OWN_ID           0x00    /* Own SCSI ID + freq select */
#define WD_CONTROL          0x01    /* Control register */
#define WD_TIMEOUT_PERIOD   0x02    /* Timeout period */
#define WD_CDB1             0x03    /* CDB byte 1 (total CDB regs 0x03-0x0E) */
#define WD_CDB_SIZE         12      /* 12 CDB bytes max (regs 0x03-0x0E) */
#define WD_TARGET_LUN       0x0F    /* Target/LUN register */
#define WD_CMD_PHASE        0x10    /* Command phase */
#define WD_SYNC_TRANSFER    0x11    /* Synchronous transfer register */
#define WD_TRANSFER_COUNT_MSB 0x12  /* Transfer count MSB */
#define WD_TRANSFER_COUNT    0x13   /* Transfer count mid */
#define WD_TRANSFER_COUNT_LSB 0x14  /* Transfer count LSB */
#define WD_DEST_ID          0x15    /* Destination ID */
#define WD_SRC_ID           0x16    /* Source ID */
#define WD_SCSI_STATUS      0x17    /* SCSI status (read-only) */
#define WD_COMMAND          0x18    /* Command register */
#define WD_DATA             0x19    /* Data register (SCSI data) */
#define WD_QUEUE_TAG        0x1A    /* Queue tag */
#define WD_AUX_STATUS       0x1F    /* Auxiliary status (read via ADDR port) */

/* ========================================================================
 * WD33C93 Register Bit Definitions
 * ======================================================================== */

/* OWN_ID register */
#define OWNID_FS_MASK       0x03    /* Frequency select: 00=8-10MHz, 01=12-15MHz, 10=16-20MHz */
#define OWNID_FS_8MHZ       0x00
#define OWNID_FS_12MHZ      0x01
#define OWNID_FS_16MHZ      0x02
#define OWNID_EHP           0x10    /* Enable host parity */
#define OWNID_EAF           0x08    /* Enable advanced features */
#define OWNID_RAF           0x04    /* RAF bit */

/* CONTROL register */
#define CTRL_HSP            0x01    /* Halt on SCSI parity error */
#define CTRL_HA             0x02    /* Halt on ATN */
#define CTRL_IDI            0x04    /* Intermediate disconnect interrupt */
#define CTRL_EDI            0x08    /* Ending disconnect interrupt */
#define CTRL_HHP            0x10    /* Halt on host parity error */
#define CTRL_DM_POLLED      0x00    /* DMA mode: polled */
#define CTRL_DM_BURST       0x20    /* DMA mode: burst */
#define CTRL_DM_DMA         0x40    /* DMA mode: DMA */
#define CTRL_DM_INTDMA      0x60    /* DMA mode: interrupt-driven DMA */

/* SCSI STATUS register (read from WD_SCSI_STATUS after command) */
#define STAT_INTR_MASK      0xF0    /* Interrupt status upper nibble */
#define STAT_PHASE_MASK     0x07    /* Phase bits */
#define STAT_RESET          0x00    /* Reset complete */
#define STAT_RESET_AFM      0x01    /* Reset with advanced features */
#define STAT_RESELECT       0x10    /* Reselected */
#define STAT_SELECT_ATN     0x11    /* Select with ATN */
#define STAT_SELECT         0x12    /* Select without ATN */
#define STAT_SEL_ATN_XFER   0x16    /* Select with ATN and transfer */
#define STAT_SEL_XFER       0x17    /* Select and transfer */
#define STAT_SEL_NORESP     0x42    /* Selection timeout - no response */
#define STAT_SUCCESS        0x16    /* Command completed successfully */
#define STAT_DISCONNECT     0x85    /* Disconnected */

/* Auxiliary Status Register (read from ADDRESS port when bit 7 set) */
#define ASR_INT             0x80    /* Interrupt pending */
#define ASR_LCI             0x40    /* Last command ignored */
#define ASR_BSY             0x20    /* Busy (chip processing command) */
#define ASR_CIP             0x10    /* Command in progress */
#define ASR_PE              0x02    /* Parity error */
#define ASR_DBR             0x01    /* Data buffer ready */

/* WD33C93 Commands */
#define WD_CMD_RESET        0x00    /* Reset chip */
#define WD_CMD_ABORT        0x01    /* Abort operation */
#define WD_CMD_ASSERT_ATN   0x02
#define WD_CMD_NEGATE_ACK   0x03
#define WD_CMD_DISCONNECT   0x04
#define WD_CMD_RESELECT     0x05
#define WD_CMD_SEL_ATN      0x06    /* Select with ATN */
#define WD_CMD_SEL          0x07    /* Select without ATN */
#define WD_CMD_SEL_ATN_XFER 0x08    /* Select with ATN and transfer */
#define WD_CMD_SEL_XFER     0x09    /* Select and transfer (most used) */
#define WD_CMD_RESEL_RECV   0x0A
#define WD_CMD_RESEL_SEND   0x0B
#define WD_CMD_WAIT_SEL_RECV 0x0C
#define WD_CMD_SEND_STATUS  0x0D
#define WD_CMD_SEND_DATA    0x0E
#define WD_CMD_RECV_CMD     0x0F
#define WD_CMD_RECV_DATA    0x10
#define WD_CMD_RECV_MSG_OUT 0x11
#define WD_CMD_RECV_UNSPEC  0x12
#define WD_CMD_SEND_MSG_IN  0x13
#define WD_CMD_SEND_STATUS2 0x14
#define WD_CMD_XFER_PAD     0x18    /* Transfer information (pad) */
#define WD_CMD_XFER_INFO    0x20    /* Transfer information */
#define WD_CMD_TRANSFER_PAD 0x18

/* SCSI bus phases */
#define PHASE_DATA_OUT      0x00
#define PHASE_DATA_IN       0x01
#define PHASE_CMD           0x02
#define PHASE_STATUS        0x03
#define PHASE_MSG_OUT       0x06
#define PHASE_MSG_IN        0x07

/* SCSI standard command opcodes */
#define SCSI_TEST_UNIT_READY 0x00
#define SCSI_REQUEST_SENSE   0x03
#define SCSI_INQUIRY         0x12
#define SCSI_READ_CAPACITY   0x25
#define SCSI_READ_10         0x28
#define SCSI_WRITE_10        0x2A

/* ========================================================================
 * Device table
 * ======================================================================== */

#define SCSI_MAX_TARGETS    7       /* IDs 0-6 (7 = host adapter) */
#define SCSI_HOST_ID        7       /* Our SCSI ID */
#define SCSI_MAX_LUN        1       /* We only probe LUN 0 */

typedef struct {
    bool     present;
    uint8_t  device_type;       /* INQUIRY peripheral device type */
    uint32_t block_count;       /* Total blocks from READ CAPACITY */
    uint32_t block_size;        /* Bytes per block */
    char     vendor[9];         /* 8 chars + NUL */
    char     product[17];       /* 16 chars + NUL */
} SCSIDevice;

static SCSIDevice scsi_devices[SCSI_MAX_TARGETS];
static bool       scsi_initialized = false;

/* Scratch buffer for INQUIRY and REQUEST SENSE data */
static uint8_t scsi_buf[256];

/* ========================================================================
 * WD33C93 Low-Level Register Access
 * ======================================================================== */

static inline void wd_write_addr(uint8_t reg)
{
    *((volatile uint8_t *)WD_ADDR_PORT) = reg;
}

static inline void wd_write_data(uint8_t val)
{
    *((volatile uint8_t *)WD_DATA_PORT) = val;
}

static inline uint8_t wd_read_data(void)
{
    return *((volatile uint8_t *)WD_DATA_PORT);
}

static inline uint8_t wd_read_asr(void)
{
    return *((volatile uint8_t *)WD_ADDR_PORT);
}

static void wd_write_reg(uint8_t reg, uint8_t val)
{
    wd_write_addr(reg);
    wd_write_data(val);
}

static uint8_t wd_read_reg(uint8_t reg)
{
    wd_write_addr(reg);
    return wd_read_data();
}

/* Write 24-bit transfer count */
static void wd_set_count(uint32_t count)
{
    wd_write_reg(WD_TRANSFER_COUNT_MSB, (uint8_t)(count >> 16));
    wd_write_reg(WD_TRANSFER_COUNT,     (uint8_t)(count >>  8));
    wd_write_reg(WD_TRANSFER_COUNT_LSB, (uint8_t)(count));
}

/* ========================================================================
 * WD33C93 Wait Helpers
 * ======================================================================== */

#define WD_TIMEOUT_LOOPS    2000000UL

/* Wait for interrupt pending (ASR bit 7) */
static bool wd_wait_int(void)
{
    for (uint32_t i = 0; i < WD_TIMEOUT_LOOPS; i++) {
        if (wd_read_asr() & ASR_INT) return true;
    }
    return false;  /* Timeout */
}

/* Wait for chip not busy */
static bool wd_wait_not_busy(void)
{
    for (uint32_t i = 0; i < WD_TIMEOUT_LOOPS; i++) {
        uint8_t asr = wd_read_asr();
        if (!(asr & (ASR_BSY | ASR_CIP))) return true;
    }
    return false;
}

/* Wait for data buffer ready (DBR) */
static bool wd_wait_dbr(void)
{
    for (uint32_t i = 0; i < WD_TIMEOUT_LOOPS; i++) {
        if (wd_read_asr() & ASR_DBR) return true;
    }
    return false;
}

/* ========================================================================
 * Reset
 * ======================================================================== */

static bool wd_reset(void)
{
    /* Issue software reset */
    wd_write_reg(WD_COMMAND, WD_CMD_RESET);

    /* Wait for interrupt indicating reset complete */
    if (!wd_wait_int()) return false;

    /* Read and discard status */
    uint8_t status = wd_read_reg(WD_SCSI_STATUS);
    (void)status;

    /* Configure own ID (7), frequency select for 7MHz A4000 SCSI clock */
    wd_write_reg(WD_OWN_ID, SCSI_HOST_ID | OWNID_FS_8MHZ | OWNID_EAF);

    /* Control: polled DMA, halt on parity error */
    wd_write_reg(WD_CONTROL, CTRL_DM_POLLED | CTRL_HSP);

    /* Timeout period: 250ms (register value * ~80ms, value=3 -> ~240ms) */
    wd_write_reg(WD_TIMEOUT_PERIOD, 20);

    /* Synchronous: asynchronous transfer (offset=0) */
    wd_write_reg(WD_SYNC_TRANSFER, 0x00);

    return true;
}

/* ========================================================================
 * Execute a SCSI command (polled, SELECT_AND_TRANSFER)
 *
 * Loads the CDB into the WD33C93 CDB registers, sets target/LUN,
 * programs the transfer count, and issues SEL_XFER.
 *
 * Data is transferred via polled PIO through WD_DATA register.
 *
 * Returns the SCSI status byte (0 = good), or 0xFF on controller error.
 * ======================================================================== */

static uint8_t wd_execute(uint8_t target, uint8_t lun,
                           const uint8_t *cdb, uint8_t cdb_len,
                           uint8_t *data, uint32_t data_len,
                           bool data_in)
{
    if (!wd_wait_not_busy()) return 0xFF;

    /* Set destination (target ID) */
    wd_write_reg(WD_DEST_ID, target & 0x07);

    /* Set target/LUN */
    wd_write_reg(WD_TARGET_LUN, (lun & 0x07) | 0x00);

    /* Write CDB bytes */
    wd_write_addr(WD_CDB1);
    for (uint8_t i = 0; i < cdb_len && i < 12; i++) {
        wd_write_data(cdb[i]);
    }

    /* Set transfer count */
    wd_set_count(data_len);

    /* Issue SELECT_AND_TRANSFER */
    wd_write_reg(WD_COMMAND, WD_CMD_SEL_XFER);

    /* Wait for interrupt (command phase completion or timeout) */
    if (!wd_wait_int()) return 0xFF;

    uint8_t scsi_stat = wd_read_reg(WD_SCSI_STATUS);

    /* Handle selection timeout */
    if (scsi_stat == STAT_SEL_NORESP) return 0xFF;

    /*
     * Transfer data phase: pump bytes through WD_DATA register.
     * The WD33C93 handles phase transitions; we just feed/drain bytes
     * when DBR (Data Buffer Ready) is asserted.
     */
    uint32_t transferred = 0;

    while (transferred < data_len) {
        /* Check if another interrupt is pending (phase change / completion) */
        if (wd_read_asr() & ASR_INT) {
            scsi_stat = wd_read_reg(WD_SCSI_STATUS);
            break;
        }

        if (!wd_wait_dbr()) break;

        if (data_in) {
            data[transferred++] = wd_read_reg(WD_DATA);
        } else {
            wd_write_reg(WD_DATA, data[transferred++]);
        }
    }

    /* Wait for final interrupt (status/message in phase) */
    if (!(wd_read_asr() & ASR_INT)) {
        wd_wait_int();
    }

    scsi_stat = wd_read_reg(WD_SCSI_STATUS);

    /* Read actual SCSI status byte from target */
    uint8_t cmd_status = wd_read_reg(WD_CMD_PHASE);

    (void)scsi_stat;
    return cmd_status & 0x3E;  /* SCSI status bits [5:1] */
}

/* ========================================================================
 * SCSI Commands
 * ======================================================================== */

static bool scsi_inquiry(uint8_t target, uint8_t *buf, uint8_t len)
{
    uint8_t cdb[6] = {
        SCSI_INQUIRY, 0x00, 0x00, 0x00, len, 0x00
    };
    uint8_t status = wd_execute(target, 0, cdb, 6, buf, len, true);
    return (status == 0);
}

static bool scsi_test_unit_ready(uint8_t target)
{
    uint8_t cdb[6] = { SCSI_TEST_UNIT_READY, 0, 0, 0, 0, 0 };
    uint8_t status = wd_execute(target, 0, cdb, 6, NULL, 0, true);
    return (status == 0);
}

static bool scsi_read_capacity(uint8_t target, uint32_t *blocks, uint32_t *bsize)
{
    uint8_t cdb[10] = { SCSI_READ_CAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t buf[8];

    uint8_t status = wd_execute(target, 0, cdb, 10, buf, 8, true);
    if (status != 0) return false;

    *blocks = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
              ((uint32_t)buf[2] <<  8) |  (uint32_t)buf[3];
    *bsize  = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
              ((uint32_t)buf[6] <<  8) |  (uint32_t)buf[7];
    return true;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

bool scsi_init(void)
{
    for (int i = 0; i < SCSI_MAX_TARGETS; i++) {
        scsi_devices[i].present = false;
    }

    if (!wd_reset()) return false;

    scsi_initialized = true;
    return true;
}

int scsi_scan(void)
{
    if (!scsi_initialized) return 0;

    int found = 0;

    for (uint8_t id = 0; id < SCSI_MAX_TARGETS; id++) {
        if (id == SCSI_HOST_ID) continue;

        /* INQUIRY to see if a device is present */
        uint8_t inq[36];
        if (!scsi_inquiry(id, inq, 36)) continue;

        /* Peripheral qualifier [7:5] = 0 means device present */
        if ((inq[0] & 0xE0) != 0x00) continue;

        scsi_devices[id].present     = true;
        scsi_devices[id].device_type = inq[0] & 0x1F;

        /* Copy vendor (bytes 8-15) */
        for (int i = 0; i < 8; i++)
            scsi_devices[id].vendor[i] = (char)inq[8 + i];
        scsi_devices[id].vendor[8] = '\0';

        /* Copy product (bytes 16-31) */
        for (int i = 0; i < 16; i++)
            scsi_devices[id].product[i] = (char)inq[16 + i];
        scsi_devices[id].product[16] = '\0';

        /* READ CAPACITY for disk devices */
        if (scsi_devices[id].device_type == 0x00) {  /* Direct-access */
            scsi_read_capacity(id,
                               &scsi_devices[id].block_count,
                               &scsi_devices[id].block_size);
        }

        found++;
    }

    return found;
}

bool scsi_read_blocks(uint8_t target, uint32_t lba,
                      uint16_t count, uint8_t *buf)
{
    if (!scsi_initialized || target >= SCSI_MAX_TARGETS) return false;
    if (!scsi_devices[target].present) return false;

    uint32_t byte_count = (uint32_t)count * scsi_devices[target].block_size;

    uint8_t cdb[10] = {
        SCSI_READ_10,
        0x00,
        (uint8_t)(lba >> 24), (uint8_t)(lba >> 16),
        (uint8_t)(lba >>  8), (uint8_t)(lba),
        0x00,
        (uint8_t)(count >> 8), (uint8_t)(count),
        0x00
    };

    return wd_execute(target, 0, cdb, 10, buf, byte_count, true) == 0;
}

bool scsi_write_blocks(uint8_t target, uint32_t lba,
                       uint16_t count, const uint8_t *buf)
{
    if (!scsi_initialized || target >= SCSI_MAX_TARGETS) return false;
    if (!scsi_devices[target].present) return false;

    uint32_t byte_count = (uint32_t)count * scsi_devices[target].block_size;

    uint8_t cdb[10] = {
        SCSI_WRITE_10,
        0x00,
        (uint8_t)(lba >> 24), (uint8_t)(lba >> 16),
        (uint8_t)(lba >>  8), (uint8_t)(lba),
        0x00,
        (uint8_t)(count >> 8), (uint8_t)(count),
        0x00
    };

    return wd_execute(target, 0, cdb, 10,
                      (uint8_t *)buf, byte_count, false) == 0;
}

bool scsi_device_present(uint8_t target)
{
    if (target >= SCSI_MAX_TARGETS) return false;
    return scsi_devices[target].present;
}

uint8_t scsi_device_type(uint8_t target)
{
    if (target >= SCSI_MAX_TARGETS) return 0x1F;
    return scsi_devices[target].device_type;
}

const char *scsi_device_vendor(uint8_t target)
{
    if (target >= SCSI_MAX_TARGETS) return "";
    return scsi_devices[target].vendor;
}

const char *scsi_device_product(uint8_t target)
{
    if (target >= SCSI_MAX_TARGETS) return "";
    return scsi_devices[target].product;
}

uint32_t scsi_device_blocks(uint8_t target)
{
    if (target >= SCSI_MAX_TARGETS) return 0;
    return scsi_devices[target].block_count;
}

uint32_t scsi_device_block_size(uint8_t target)
{
    if (target >= SCSI_MAX_TARGETS) return 512;
    return scsi_devices[target].block_size;
}
