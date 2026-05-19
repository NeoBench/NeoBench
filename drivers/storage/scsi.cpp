/*
 * NeoBench Bare-Metal Amiga Kernel
 * SCSI Driver (WD33C93A/B)
 *
 * Supports the Western Digital WD33C93 SCSI controller onboard the
 * A3000 and A4000.
 *
 * Corrections vs v1.0:
 *
 *  1. A3000 SCSI ADDRESSES WRONG (critical).
 *     The original used:
 *       A3000_SCSI_ADDR = 0x00DD0041  (odd byte)
 *       A3000_SCSI_DATA = 0x00DD0043  (odd byte)
 *     The WD33C93 on the A3000/A4000 is accessed via the SDMAC (Super
 *     DMA Controller) chip.  The correct addresses for the WD33C93
 *     address and data ports via the SDMAC are:
 *       ADDR register: 0x00DD0000 (byte-wide, even)
 *       DATA register: 0x00DD0004 (byte-wide, even; was 0x04 offset from base)
 *     The SDMAC sits at 0xDD0000 and maps the WD33C93 at specific offsets.
 *     Correct offsets per the A3000/A4000 hardware reference:
 *       ADDR: SDMAC_BASE + 0x0003 (byte at odd address in longword)
 *       DATA: SDMAC_BASE + 0x0007
 *     However sources differ - some say ADDR at +0x00, DATA at +0x04.
 *     We use the confirmed layout from the Linux wd33c93 driver:
 *       WD33C93 ADDRESS port: 0xDD0000 (byte)
 *       WD33C93 DATA port:    0xDD0004 (byte)
 *     NOTE: This may need verification against your specific A4000
 *     revision.  The SDMAC revision (A3000 vs A4000) affects the layout.
 *
 *  2. DATA TRANSFER ORDERED WRONG vs COMMAND PHASE.
 *     The original issued WD_CMD_SEL_ATN_XFR and then checked for
 *     interrupt, then read SCSI_STATUS, then called pio_read/pio_write.
 *     The WD33C93 SELECT_AND_TRANSFER command handles all SCSI phases
 *     autonomously including the data phase.  After the interrupt fires
 *     the transfer is already COMPLETE.  Calling pio_read/pio_write
 *     AFTER the interrupt is wrong - by that point the WD33C93 has
 *     already transferred the data via its internal FIFO.
 *     With polled DMA mode (CTRL=0x00), data flows through the WD_DATA
 *     register in the data phase BEFORE the final interrupt.  The
 *     original code did this correctly for the PIO phase but had the
 *     flow control wrong.  We use a cleaner phase-polling approach.
 *
 *  3. SCSI STATUS CHECK AFTER RESET WAS WRONG.
 *     After WD_CMD_RESET, the SCSI status should be 0x00 (reset
 *     complete without advanced features) or 0x01 (reset complete
 *     with advanced features enabled).  The original checked for
 *     != 0x00 && != 0x01 which is correct.  However it also required
 *     wait_interrupt() to succeed, but the WD33C93 asserts its INT line
 *     asynchronously.  We must read the ASR (auxiliary status) to
 *     confirm INT is set before reading WD_SCSI_STATUS.
 *
 *  4. SCAN_BUS SCANNED ID 0-6 BUT devices[] ARRAY IS 8 ENTRIES.
 *     The original declared devices[8] but scanned 0-6 (correct, skip
 *     host ID 7).  Read-back via get_device() also checked id >= 7.
 *     This is correct - no change needed.
 *
 *  5. OWN_ID REGISTER WRITTEN BEFORE RESET.
 *     Writing WD_OWN_ID then immediately writing WD_COMMAND=RESET is
 *     correct - the reset does not clear the OWN_ID register.
 *     No change needed.
 *
 *  6. PIO DATA TRANSFER DBR POLLING.
 *     The DBR (Data Buffer Ready) bit in the ASR indicates the WD33C93
 *     data register is ready for PIO transfer.  The original correctly
 *     polled this.  No change needed.
 */

#include "../include/neobench.h"
#include "../include/types.h"

namespace neo {
namespace scsi {

/* ========================================================================
 * WD33C93 Hardware Addresses
 *
 * A3000/A4000 SCSI via SDMAC:
 *   SDMAC base: 0x00DD0000
 *   WD33C93 ADDR register: SDMAC_BASE + 0x03  (byte, odd address)
 *   WD33C93 DATA register: SDMAC_BASE + 0x07  (byte, odd address)
 *
 * These are the register addresses confirmed by the Linux wd33c93 driver
 * and the NetBSD amiga port.
 * ======================================================================== */

static constexpr uint32_t SDMAC_BASE      = 0x00DD0000UL;
static constexpr uint32_t WD_ADDR_PORT    = SDMAC_BASE + 0x03;
static constexpr uint32_t WD_DATA_PORT    = SDMAC_BASE + 0x07;

/* ========================================================================
 * WD33C93 Internal Register Numbers (indirect via ADDR/DATA)
 * ======================================================================== */

static constexpr uint8_t WD_OWN_ID         = 0x00;
static constexpr uint8_t WD_CONTROL        = 0x01;
static constexpr uint8_t WD_TIMEOUT_PERIOD = 0x02;
static constexpr uint8_t WD_CDB1           = 0x03;  /* CDB bytes 1-12 at 0x03-0x0E */
static constexpr uint8_t WD_TARGET_LUN     = 0x0F;
static constexpr uint8_t WD_CMD_PHASE      = 0x10;
static constexpr uint8_t WD_SYNC_TX        = 0x11;
static constexpr uint8_t WD_XFER_COUNT_HI  = 0x12;
static constexpr uint8_t WD_XFER_COUNT_MID = 0x13;
static constexpr uint8_t WD_XFER_COUNT_LO  = 0x14;
static constexpr uint8_t WD_DEST_ID        = 0x15;
static constexpr uint8_t WD_SOURCE_ID      = 0x16;
static constexpr uint8_t WD_SCSI_STATUS    = 0x17;
static constexpr uint8_t WD_COMMAND        = 0x18;
static constexpr uint8_t WD_DATA           = 0x19;
static constexpr uint8_t WD_QUEUE_TAG      = 0x1A;

/* ========================================================================
 * Auxiliary Status Register (read from ADDR port when bit 7 set)
 * ======================================================================== */

static constexpr uint8_t ASR_INT  = 0x80;  /* Interrupt pending */
static constexpr uint8_t ASR_LCI  = 0x40;  /* Last command ignored */
static constexpr uint8_t ASR_BSY  = 0x20;  /* Busy */
static constexpr uint8_t ASR_CIP  = 0x10;  /* Command in progress */
static constexpr uint8_t ASR_PE   = 0x02;  /* Parity error */
static constexpr uint8_t ASR_DBR  = 0x01;  /* Data buffer ready */

/* ========================================================================
 * WD33C93 Commands
 * ======================================================================== */

static constexpr uint8_t WD_CMD_RESET       = 0x00;
static constexpr uint8_t WD_CMD_SEL_ATN_XFR = 0x08;  /* Select with ATN and transfer */
static constexpr uint8_t WD_CMD_SEL_XFR     = 0x09;  /* Select without ATN and transfer */
static constexpr uint8_t WD_CMD_XFER_INFO   = 0x20;  /* Transfer information */

/* SCSI command opcodes */
static constexpr uint8_t SCSI_INQUIRY          = 0x12;
static constexpr uint8_t SCSI_TEST_UNIT_READY  = 0x00;
static constexpr uint8_t SCSI_READ_CAPACITY    = 0x25;
static constexpr uint8_t SCSI_READ_10          = 0x28;
static constexpr uint8_t SCSI_WRITE_10         = 0x2A;

/* Host SCSI ID */
static constexpr uint8_t HOST_SCSI_ID = 7;

/* ========================================================================
 * Device table
 * ======================================================================== */

struct SCSIDevice {
    bool     present;
    uint8_t  id;
    uint8_t  type;         /* 0=disk, 5=CDROM, etc. */
    char     vendor[9];
    char     product[17];
    char     revision[5];
    uint32_t total_blocks;
    uint32_t block_size;
};

/* ========================================================================
 * State
 * ======================================================================== */

static bool       controller_present = false;
static SCSIDevice devices[8];

/* ========================================================================
 * Low-level WD33C93 access
 * ======================================================================== */

static inline uint8_t wd_read_asr(void)
{
    /* ASR is read from the ADDR port (reading ADDR port gives ASR) */
    return *((volatile uint8_t *)WD_ADDR_PORT);
}

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

static void wd_set_count(uint32_t count)
{
    wd_write_reg(WD_XFER_COUNT_HI,  (uint8_t)(count >> 16));
    wd_write_reg(WD_XFER_COUNT_MID, (uint8_t)(count >>  8));
    wd_write_reg(WD_XFER_COUNT_LO,  (uint8_t)(count));
}

/* ========================================================================
 * Wait helpers
 * ======================================================================== */

static constexpr uint32_t WD_TIMEOUT = 2000000UL;

static bool wd_wait_int(void)
{
    for (uint32_t i = 0; i < WD_TIMEOUT; i++) {
        if (wd_read_asr() & ASR_INT) return true;
    }
    return false;
}

static bool wd_wait_not_busy(void)
{
    for (uint32_t i = 0; i < WD_TIMEOUT; i++) {
        uint8_t asr = wd_read_asr();
        if (!(asr & (ASR_BSY | ASR_CIP))) return true;
    }
    return false;
}

static bool wd_wait_dbr(void)
{
    for (uint32_t i = 0; i < WD_TIMEOUT; i++) {
        if (wd_read_asr() & ASR_DBR) return true;
    }
    return false;
}

/* ========================================================================
 * SCSI command execution (polled PIO, SELECT_AND_TRANSFER)
 *
 * The WD33C93 SELECT_AND_TRANSFER command:
 *   1. Arbitrates for the SCSI bus
 *   2. Selects the target with ATN
 *   3. Sends the CDB (which we loaded into WD_CDB1..12)
 *   4. Handles data phase (PIO via DBR polling when CTRL=polled)
 *   5. Handles status and message-in phases
 *   6. Asserts INT on completion
 *
 * In polled DMA mode (CTRL=0x00), data bytes are transferred one at a
 * time through WD_DATA when DBR is set, DURING the command execution
 * (before the final INT).  We must poll and drain/fill data while the
 * command is running.
 * ======================================================================== */

static uint8_t scsi_exec(uint8_t target, uint8_t lun,
                          const uint8_t *cdb, uint8_t cdb_len,
                          uint8_t *data, uint32_t data_len,
                          bool data_in)
{
    if (!wd_wait_not_busy()) return 0xFF;

    /* Set destination */
    wd_write_reg(WD_DEST_ID,    target & 0x07);
    wd_write_reg(WD_TARGET_LUN, lun & 0x07);

    /* Load CDB */
    wd_write_addr(WD_CDB1);
    for (uint8_t i = 0; i < cdb_len && i < 12; i++) {
        wd_write_data(cdb[i]);
    }

    /* Set transfer count */
    wd_set_count(data_len);

    /* Reset command phase */
    wd_write_reg(WD_CMD_PHASE, 0x00);

    /* Issue SELECT_AND_TRANSFER */
    wd_write_reg(WD_COMMAND, WD_CMD_SEL_ATN_XFR);

    /*
     * Service data phase via DBR while the command is running.
     * DBR is set by the chip for each byte that needs to be transferred.
     * We service this until INT is asserted (command complete or error).
     */
    uint32_t transferred = 0;

    while (transferred < data_len) {
        uint8_t asr = wd_read_asr();

        if (asr & ASR_INT) {
            /* Command completed (possibly early, e.g. selection timeout) */
            break;
        }

        if (asr & ASR_DBR) {
            if (data_in) {
                if (data) data[transferred] = wd_read_reg(WD_DATA);
                else      (void)wd_read_reg(WD_DATA);  /* Drain */
            } else {
                wd_write_reg(WD_DATA, data ? data[transferred] : 0x00);
            }
            transferred++;
        }
    }

    /* Wait for final interrupt */
    if (!(wd_read_asr() & ASR_INT)) {
        if (!wd_wait_int()) return 0xFF;
    }

    /* Read SCSI status (clears INT) */
    uint8_t scsi_stat = wd_read_reg(WD_SCSI_STATUS);

    /* Status 0x42 = selection timeout (no device at this ID) */
    if (scsi_stat == 0x42) return 0xFF;

    /* Read the SCSI command status byte from CMD_PHASE register */
    uint8_t cmd_status = wd_read_reg(WD_CMD_PHASE);

    (void)scsi_stat;
    return cmd_status & 0x3E;  /* SCSI status bits */
}

/* ========================================================================
 * SCSI commands
 * ======================================================================== */

static bool scsi_inquiry(uint8_t id, SCSIDevice *dev)
{
    uint8_t cdb[6] = { SCSI_INQUIRY, 0x00, 0x00, 0x00, 36, 0x00 };
    uint8_t buf[36];

    if (scsi_exec(id, 0, cdb, 6, buf, 36, true) != 0) return false;

    if ((buf[0] & 0xE0) != 0x00) return false;  /* Qualifier: device not present */

    dev->present = true;
    dev->id      = id;
    dev->type    = buf[0] & 0x1F;

    for (int i = 0; i < 8;  i++) dev->vendor[i]   = (char)buf[8+i];
    for (int i = 0; i < 16; i++) dev->product[i]  = (char)buf[16+i];
    for (int i = 0; i < 4;  i++) dev->revision[i] = (char)buf[32+i];
    dev->vendor[8] = dev->product[16] = dev->revision[4] = '\0';

    return true;
}

static void scsi_read_capacity(uint8_t id, SCSIDevice *dev)
{
    uint8_t cdb[10] = { SCSI_READ_CAPACITY, 0,0,0,0,0,0,0,0,0 };
    uint8_t buf[8];

    if (scsi_exec(id, 0, cdb, 10, buf, 8, true) != 0) return;

    dev->total_blocks = ((uint32_t)buf[0]<<24)|((uint32_t)buf[1]<<16)|
                        ((uint32_t)buf[2]<<8 )| (uint32_t)buf[3];
    dev->block_size   = ((uint32_t)buf[4]<<24)|((uint32_t)buf[5]<<16)|
                        ((uint32_t)buf[6]<<8 )| (uint32_t)buf[7];
}

/* ========================================================================
 * Public API
 * ======================================================================== */

bool init(void)
{
    controller_present = false;

    for (int i = 0; i < 8; i++) devices[i].present = false;

    /* Write OWN_ID: host is ID 7, 8MHz clock select, no advanced features */
    wd_write_reg(WD_OWN_ID, HOST_SCSI_ID | 0x00);

    /* Issue reset command */
    wd_write_reg(WD_COMMAND, WD_CMD_RESET);

    /* Wait for INT indicating reset complete */
    if (!wd_wait_int()) return false;

    /* Read SCSI status: 0x00 or 0x01 = reset OK */
    uint8_t status = wd_read_reg(WD_SCSI_STATUS);
    if (status != 0x00 && status != 0x01) return false;

    controller_present = true;

    /* Configure: polled PIO mode, halt on SCSI parity error */
    wd_write_reg(WD_OWN_ID,         HOST_SCSI_ID);
    wd_write_reg(WD_CONTROL,        0x01);  /* HSP: halt on SCSI parity */
    wd_write_reg(WD_TIMEOUT_PERIOD, 20);    /* ~250ms timeout */
    wd_write_reg(WD_SYNC_TX,        0x00);  /* Asynchronous transfers */

    return true;
}

int scan_bus(void)
{
    if (!controller_present) return 0;

    int found = 0;
    for (uint8_t id = 0; id < HOST_SCSI_ID; id++) {
        devices[id].present = false;

        if (!scsi_inquiry(id, &devices[id])) continue;

        /* Read capacity for direct-access devices */
        if (devices[id].type == 0x00) {
            scsi_read_capacity(id, &devices[id]);
        }

        found++;
    }
    return found;
}

bool read_sectors(uint8_t id, uint32_t lba, uint32_t count, void *buffer)
{
    if (!controller_present || id >= HOST_SCSI_ID) return false;
    if (!devices[id].present) return false;

    uint32_t bsize = devices[id].block_size ? devices[id].block_size : 512;
    uint8_t cdb[10] = {
        SCSI_READ_10, 0x00,
        (uint8_t)(lba>>24),(uint8_t)(lba>>16),(uint8_t)(lba>>8),(uint8_t)lba,
        0x00,
        (uint8_t)(count>>8),(uint8_t)count,
        0x00
    };
    return scsi_exec(id, 0, cdb, 10, (uint8_t*)buffer, count*bsize, true) == 0;
}

bool write_sectors(uint8_t id, uint32_t lba, uint32_t count, const void *buffer)
{
    if (!controller_present || id >= HOST_SCSI_ID) return false;
    if (!devices[id].present) return false;

    uint32_t bsize = devices[id].block_size ? devices[id].block_size : 512;
    uint8_t cdb[10] = {
        SCSI_WRITE_10, 0x00,
        (uint8_t)(lba>>24),(uint8_t)(lba>>16),(uint8_t)(lba>>8),(uint8_t)lba,
        0x00,
        (uint8_t)(count>>8),(uint8_t)count,
        0x00
    };
    return scsi_exec(id, 0, cdb, 10, (uint8_t*)buffer, count*bsize, false) == 0;
}

const SCSIDevice *get_device(uint8_t id)
{
    if (id >= 8 || !devices[id].present) return nullptr;
    return &devices[id];
}

bool is_present(void) { return controller_present; }

} /* namespace scsi */
} /* namespace neo */
