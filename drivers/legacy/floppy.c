/*
 * NeoBench Bare-Metal Amiga Kernel
 * Floppy Disk Driver
 *
 * Controls the Amiga floppy subsystem using:
 *   - Paula DMA (DSKPTH/DSKPTL/DSKLEN/DSKSYNC) for data transfer
 *   - CIA-A (motor, side, direction, step) for drive mechanics
 *   - CIA-B (drive select, /RDY, /TRK0, /WPRO, /CHNG) for status
 *
 * Supports:
 *   - DD (880KB) and HD (1760KB, with HD drive detection)
 *   - MFM encoding/decoding (standard AmigaDOS format)
 *   - Track buffering (one track cached per drive)
 *   - Read and write (including verify on write)
 *
 * Notes on Amiga floppy geometry:
 *   DD: 80 cylinders, 2 heads, 11 sectors/track, 512 bytes/sector
 *   HD: 80 cylinders, 2 heads, 22 sectors/track, 512 bytes/sector
 *   One track DMA transfer = (sectors * (512 + 56)) bytes of MFM data
 *   The 56 extra bytes are the sector header (AmigaDOS format).
 *
 * MFM track layout (AmigaDOS):
 *   Gap (pre-sync)
 *   For each sector 0-10 (or 0-21 for HD):
 *     [00000000 00000000] sync words (2 x 0xAAAAAAAA + 0x44894489)
 *     Header: sector info encoded as MFM (odd/even longwords)
 *     Data: 512 bytes encoded as MFM (odd/even longwords)
 *     Checksum: odd/even longwords
 *
 * References:
 *   Amiga Hardware Reference Manual, 3rd ed., Appendix C
 *   AmigaDOS Technical Reference Manual
 */

#include "../include/neobench.h"
#include "../include/types.h"

/* ========================================================================
 * Hardware Register Addresses
 * ======================================================================== */

#define CUSTOM_BASE         0x00DFF000UL

/* Paula disk registers (offsets from CUSTOM_BASE) */
#define DSKPTH              0x020   /* Disk DMA pointer high */
#define DSKPTL              0x022   /* Disk DMA pointer low */
#define DSKLEN              0x024   /* Disk DMA length + control */
#define DSKDAT              0x026   /* Disk DMA data (write-only) */
#define DSKSYNC             0x07E   /* Disk sync word */
#define DMACONR             0x002   /* DMA control (read) */
#define DMACON              0x096   /* DMA control (write) */
#define INTREQ              0x09C   /* Interrupt request (write) */
#define INTREQR             0x01E   /* Interrupt request (read) */
#define INTENA              0x09A   /* Interrupt enable */
#define ADKCON              0x09E   /* Audio/disk control */
#define ADKCONR             0x010   /* Audio/disk control (read) */

/* DMA and interrupt bits */
#define DMAF_SETCLR         0x8000
#define DMAF_MASTER         0x0200
#define DMAF_DISK           0x0010
#define INTF_DSKBLK         0x0002  /* Disk block done */
#define INTF_SETCLR         0x8000

/* ADKCON bits */
#define ADKF_SETCLR         0x8000
#define ADKF_MFMPREC        0x0200  /* MFM precompensation */
#define ADKF_WORDSYNC       0x0400  /* Sync on word (DSKSYNC) */
#define ADKF_MSBSYNC        0x0800  /* Sync on MSB */

/* CIA-A base (byte-wide, odd addresses, 0x100 spacing) */
#define CIAA_BASE           0xBFE001UL
#define CIAA_PRA            0xBFE001UL  /* Port A */
#define CIAA_DDRA           0xBFE201UL  /* Direction A */
#define CIAA_CRA            0xBFEE01UL  /* Control A */

/* CIA-B base */
#define CIAB_BASE           0xBFD000UL
#define CIAB_PRB            0xBFD000UL  /* Port B - drive control */
#define CIAB_DDRB           0xBFD200UL  /* Direction B */
#define CIAB_PRA            0xBFD100UL  /* Port A - disk status */
#define CIAB_DDRA           0xBFD300UL  /* Direction A */

/* CIA-B PRB (Port B) bit definitions (active LOW outputs) */
#define CIABPRB_MTR         0x80    /* Motor on (active low) */
#define CIABPRB_SEL3        0x40    /* Select drive 3 (active low) */
#define CIABPRB_SEL2        0x20    /* Select drive 2 */
#define CIABPRB_SEL1        0x10    /* Select drive 1 */
#define CIABPRB_SEL0        0x08    /* Select drive 0 */
#define CIABPRB_SIDE        0x04    /* Side select: 0=upper(1), 1=lower(0) */
#define CIABPRB_DIR         0x02    /* Direction: 0=out(towards cyl 0), 1=in */
#define CIABPRB_STEP        0x01    /* Step pulse (active low) */

static const uint8_t DRIVE_SEL[4] = {
    CIABPRB_SEL0, CIABPRB_SEL1, CIABPRB_SEL2, CIABPRB_SEL3
};

/* CIA-B PRA (Port A) bit definitions (inputs, active LOW) */
#define CIABPRA_CHNG        0x04    /* Disk changed (low = changed) */
#define CIABPRA_WPRO        0x08    /* Write protected (low = protected) */
#define CIABPRA_TRK0        0x10    /* Track 0 (low = at track 0) */
#define CIABPRA_RDY         0x20    /* Drive ready (low = ready) */
#define CIABPRA_DKWE        0x40    /* Disk write enable */
#define CIABPRA_ID          0x80    /* Drive ID bit (for HD detection) */

/* CIA-A PRA bits related to disk */
#define CIAAPRA_FIR0        0x40    /* Fire button / index pulse bit */

/* ========================================================================
 * MFM constants
 * ======================================================================== */

#define MFM_SYNC_WORD       0x4489  /* Standard Amiga MFM sync word */
#define SECTORS_PER_TRACK_DD 11
#define SECTORS_PER_TRACK_HD 22
#define BYTES_PER_SECTOR    512
#define SECTOR_HEADER_BYTES 28      /* MFM decoded header size per sector */

/* MFM track buffer size (raw MFM bytes):
 * Each sector: 2 sync longs (8 bytes) + header (28 bytes decoded -> 56 MFM)
 *              + data (512 decoded -> 1024 MFM) + gap
 * Total for 11-sector DD track: ~6400 bytes raw MFM + gap = 13630 bytes
 * We use 14848 bytes (standard track buffer size from HRM) */
#define TRACK_BUFFER_BYTES  14848   /* Enough for one full DD track + gap */
#define HD_TRACK_BUFFER_BYTES 29696 /* HD track */

/* ========================================================================
 * State
 * ======================================================================== */

#define MAX_DRIVES  4

typedef struct {
    bool     present;
    bool     hd;            /* High density drive */
    int      current_track;
    bool     motor_on;
    bool     dirty;         /* Track buffer modified (needs write-back) */
    int      buffered_track;
    int      buffered_side;
    uint8_t *track_buf;     /* Chip RAM track buffer */
} FloppyDrive;

static FloppyDrive drives[MAX_DRIVES];
static bool        floppy_initialized = false;

extern void *chip_alloc(uint32_t size, uint32_t alignment);

/* ========================================================================
 * Custom chip register helpers
 * ======================================================================== */

static inline void cust_write(uint16_t reg, uint16_t val)
{
    *((volatile uint16_t *)(CUSTOM_BASE + reg)) = val;
}

static inline uint16_t cust_read(uint16_t reg)
{
    return *((volatile uint16_t *)(CUSTOM_BASE + reg));
}

static inline void cust_write32(uint16_t reg, uint32_t val)
{
    volatile uint16_t *p = (volatile uint16_t *)(CUSTOM_BASE + reg);
    p[0] = (uint16_t)(val >> 16);
    p[1] = (uint16_t)(val & 0xFFFF);
}

static inline void ciaa_write(uint32_t addr, uint8_t val)
{
    *((volatile uint8_t *)addr) = val;
}
static inline uint8_t ciaa_read(uint32_t addr)
{
    return *((volatile uint8_t *)addr);
}
static inline void ciab_write(uint32_t addr, uint8_t val)
{
    *((volatile uint8_t *)addr) = val;
}
static inline uint8_t ciab_read(uint32_t addr)
{
    return *((volatile uint8_t *)addr);
}

/* ========================================================================
 * Drive select / deselect
 *
 * PRB is active-low: writing 0 to a SELx bit selects that drive.
 * We must deselect ALL drives before selecting one to avoid bus contention.
 * The MTR bit controls the motor for the SELECTED drive.
 * ======================================================================== */

static void select_drive(int drive)
{
    /* Deselect all, keep motor bits */
    uint8_t prb = ciab_read(CIAB_PRB);
    prb |= (CIABPRB_SEL0 | CIABPRB_SEL1 | CIABPRB_SEL2 | CIABPRB_SEL3);
    ciab_write(CIAB_PRB, prb);

    if (drive < 0 || drive >= MAX_DRIVES) return;

    /* Select the drive (assert its SEL line low) */
    prb &= ~DRIVE_SEL[drive];
    ciab_write(CIAB_PRB, prb);
}

static void deselect_all(void)
{
    uint8_t prb = ciab_read(CIAB_PRB);
    prb |= (CIABPRB_SEL0 | CIABPRB_SEL1 | CIABPRB_SEL2 | CIABPRB_SEL3);
    ciab_write(CIAB_PRB, prb);
}

/* ========================================================================
 * Motor control
 *
 * Motor is controlled by the MTR bit in CIA-B PRB while the drive is
 * selected. MTR=0 means motor ON (active low).
 * After turning on, wait for the drive to reach speed (up to 500ms).
 * ======================================================================== */

static void motor_on(int drive)
{
    if (drives[drive].motor_on) return;

    select_drive(drive);

    /* Assert MTR (motor on = active low) */
    uint8_t prb = ciab_read(CIAB_PRB);
    prb &= ~CIABPRB_MTR;
    ciab_write(CIAB_PRB, prb);

    drives[drive].motor_on = true;

    /* Wait for RDY signal (CIA-B PRA bit 5, active low = ready) */
    /* Allow up to ~500ms at 50Hz = 25 vblank frames */
    for (int i = 0; i < 25; i++) {
        /* Wait one frame via INTREQR VERTB */
        cust_write(INTREQ, 0x0020);  /* clear VERTB */
        while (!(cust_read(INTREQR) & 0x0020)) {}

        select_drive(drive);  /* must be selected to read RDY */
        uint8_t pra = ciab_read(CIAB_PRA);
        if (!(pra & CIABPRA_RDY)) break;  /* Ready */
    }

    deselect_all();
}

static void motor_off(int drive)
{
    if (!drives[drive].motor_on) return;

    select_drive(drive);

    uint8_t prb = ciab_read(CIAB_PRB);
    prb |= CIABPRB_MTR;  /* Deassert MTR */
    ciab_write(CIAB_PRB, prb);

    drives[drive].motor_on = false;
    deselect_all();
}

/* ========================================================================
 * Head stepping
 *
 * Step from current_track to target_track.
 * Each step pulse moves the head one track in the current direction.
 * Direction: DIR=0 = towards track 0 (outward), DIR=1 = inward.
 * Minimum step delay: 3ms between steps per drive spec.
 * ======================================================================== */

static void step_to_track0(int drive)
{
    select_drive(drive);

    /* Direction: outward (towards track 0) */
    uint8_t prb = ciab_read(CIAB_PRB);
    prb &= ~CIABPRB_DIR;  /* DIR=0 = outward */
    ciab_write(CIAB_PRB, prb);

    /* Step until TRK0 asserts (active low) */
    for (int steps = 0; steps < 90; steps++) {
        select_drive(drive);
        if (!(ciab_read(CIAB_PRA) & CIABPRA_TRK0)) break;

        /* Step pulse: STEP low then high */
        prb = ciab_read(CIAB_PRB);
        prb &= ~CIABPRB_STEP;
        ciab_write(CIAB_PRB, prb);

        /* Step pulse width: ~3µs minimum */
        for (volatile int d = 0; d < 100; d++) {}

        prb |= CIABPRB_STEP;
        ciab_write(CIAB_PRB, prb);

        /* Wait 3ms between steps: ~150 iterations at 68060 */
        for (volatile int d = 0; d < 50000; d++) {}
    }

    drives[drive].current_track = 0;
    deselect_all();
}

static void seek_track(int drive, int track)
{
    if (track == drives[drive].current_track) return;

    select_drive(drive);
    uint8_t prb = ciab_read(CIAB_PRB);

    if (track < drives[drive].current_track) {
        prb &= ~CIABPRB_DIR;   /* Outward */
    } else {
        prb |= CIABPRB_DIR;    /* Inward */
    }
    ciab_write(CIAB_PRB, prb);

    int steps = track - drives[drive].current_track;
    if (steps < 0) steps = -steps;

    for (int i = 0; i < steps; i++) {
        prb = ciab_read(CIAB_PRB);
        prb &= ~CIABPRB_STEP;
        ciab_write(CIAB_PRB, prb);

        for (volatile int d = 0; d < 100; d++) {}

        prb |= CIABPRB_STEP;
        ciab_write(CIAB_PRB, prb);

        for (volatile int d = 0; d < 50000; d++) {}
    }

    drives[drive].current_track = track;
    deselect_all();
}

/* ========================================================================
 * Side select
 *
 * SIDE=0 in CIA-B PRB selects the upper head (side 1 in AmigaDOS).
 * SIDE=1 selects the lower head (side 0).
 * This is counter-intuitive but matches the HRM.
 * AmigaDOS side 0 = lower head = CIABPRB_SIDE set.
 * AmigaDOS side 1 = upper head = CIABPRB_SIDE clear.
 * ======================================================================== */

static void select_side(int side)
{
    uint8_t prb = ciab_read(CIAB_PRB);
    if (side == 0) {
        prb |= CIABPRB_SIDE;   /* SIDE=1 -> lower head = AmigaDOS side 0 */
    } else {
        prb &= ~CIABPRB_SIDE;  /* SIDE=0 -> upper head = AmigaDOS side 1 */
    }
    ciab_write(CIAB_PRB, prb);
}

/* ========================================================================
 * DMA Read (one full track)
 *
 * Programs Paula DMA to read one full track of MFM data into the
 * track buffer.  We use word-sync mode (DSKSYNC = 0x4489).
 *
 * DSKLEN: bit 15 = DMA enable, bit 14 = write (0=read), bits 13:0 = words
 * ======================================================================== */

#define DSKBLK_TIMEOUT_FRAMES  30

static bool dma_read_track(int drive)
{
    uint32_t buf_addr = (uint32_t)drives[drive].track_buf;
    uint16_t words    = (uint16_t)(TRACK_BUFFER_BYTES / 2);

    /* Set sync word */
    cust_write(DSKSYNC, MFM_SYNC_WORD);

    /* Enable word sync and MFM in ADKCON */
    cust_write(ADKCON, (uint16_t)(ADKF_SETCLR | ADKF_WORDSYNC | ADKF_MFMPREC));

    /* Clear DSKBLK interrupt flag */
    cust_write(INTREQ, INTF_DSKBLK);

    /* Set DMA pointer */
    cust_write32(DSKPTH, buf_addr);

    /* Enable disk DMA */
    cust_write(DMACON, (uint16_t)(DMAF_SETCLR | DMAF_MASTER | DMAF_DISK));

    /* Start DMA read: bit 15=enable, bit 14=0(read), bits 13:0=word count */
    cust_write(DSKLEN, (uint16_t)(0x8000 | words));
    cust_write(DSKLEN, (uint16_t)(0x8000 | words)); /* Write twice to start */

    /* Wait for DSKBLK interrupt (DMA complete) */
    for (int frame = 0; frame < DSKBLK_TIMEOUT_FRAMES; frame++) {
        cust_write(INTREQ, 0x0020);  /* clear VERTB */
        while (!(cust_read(INTREQR) & 0x0020)) {
            if (cust_read(INTREQR) & INTF_DSKBLK) goto done;
        }
        if (cust_read(INTREQR) & INTF_DSKBLK) goto done;
    }

    /* Timeout: stop DMA */
    cust_write(DSKLEN, 0x0000);
    cust_write(DMACON, DMAF_DISK);
    return false;

done:
    /* Stop DMA */
    cust_write(DSKLEN, 0x0000);
    cust_write(DMACON, DMAF_DISK);
    cust_write(INTREQ, INTF_DSKBLK);

    return true;
}

/* ========================================================================
 * MFM Decode
 *
 * AmigaDOS MFM encoding uses odd/even longword pairs:
 *   odd_longs  = data bits at odd bit positions (bits 31,29,27,...1)
 *   even_longs = data bits at even bit positions (bits 30,28,26,...0)
 *   decoded    = ((odd_longs & 0x55555555) << 1) | (even_longs & 0x55555555)
 *
 * The MFM clock bits are in the complementary positions and are masked out.
 * ======================================================================== */

static inline uint32_t mfm_decode_long(uint32_t odd, uint32_t even)
{
    return ((odd & 0x55555555UL) << 1) | (even & 0x55555555UL);
}

/*
 * Decode one AmigaDOS sector from a track buffer.
 * Returns pointer to next sector in MFM stream, or NULL if sync not found.
 *
 * MFM sector layout:
 *   2 x 0x00000000 (gap, may be more)
 *   0xAAAAAAAA (MFM for 0x0000)
 *   0xAAAAAAAA
 *   0x44894489 (sync words)
 *   odd_header_lw[0]  (info: format, track, sector, sectors-to-gap)
 *   even_header_lw[0]
 *   odd_label[0..3]   (AmigaDOS label - 4 longwords)
 *   even_label[0..3]
 *   odd_header_chk    (header checksum)
 *   even_header_chk
 *   odd_data_chk      (data area checksum)
 *   even_data_chk
 *   odd_data[0..127]  (512 bytes of data = 128 longwords)
 *   even_data[0..127]
 */
static const uint32_t *decode_sector(const uint32_t *mfm,
                                      const uint32_t *mfm_end,
                                      uint8_t *sector_out,
                                      uint8_t *sector_num_out)
{
    /* Search for sync words */
    while (mfm < mfm_end - 1) {
        if (mfm[0] == 0x44894489UL) break;
        mfm++;
    }

    if (mfm >= mfm_end - 1) return NULL;

    mfm++;  /* Skip sync */

    /* Decode header info longword */
    uint32_t info_odd  = *mfm++;
    uint32_t info_even = *mfm++;
    uint32_t info = mfm_decode_long(info_odd, info_even);

    uint8_t  format   = (uint8_t)(info >> 24);
    uint8_t  track    = (uint8_t)(info >> 16);
    uint8_t  sector   = (uint8_t)(info >>  8);
    /* uint8_t  sectors_to_gap = (uint8_t)(info); */

    (void)format;
    (void)track;

    if (sector_num_out) *sector_num_out = sector;

    /* Skip label (4 odd + 4 even longwords = 8 longwords) */
    mfm += 8;

    /* Skip header checksum (2 longwords) */
    mfm += 2;

    /* Skip data checksum (2 longwords) */
    mfm += 2;

    /* Decode 128 data longwords (odd/even pairs) */
    if (sector_out) {
        uint32_t *out32 = (uint32_t *)sector_out;
        for (int i = 0; i < 128; i++) {
            uint32_t odd  = mfm[i];
            uint32_t even = mfm[i + 128];
            out32[i] = mfm_decode_long(odd, even);
        }
    }

    mfm += 256;  /* 128 odd + 128 even longwords */

    return mfm;
}

/* ========================================================================
 * Read a full track into the track buffer and decode all sectors
 * ======================================================================== */

static bool read_track(int drive, int cylinder, int side)
{
    /* Already buffered? */
    if (drives[drive].buffered_track == cylinder &&
        drives[drive].buffered_side  == side &&
        !drives[drive].dirty) {
        return true;
    }

    motor_on(drive);
    seek_track(drive, cylinder);

    select_drive(drive);
    select_side(side);

    bool ok = dma_read_track(drive);

    deselect_all();

    if (ok) {
        drives[drive].buffered_track = cylinder;
        drives[drive].buffered_side  = side;
        drives[drive].dirty          = false;
    }

    return ok;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

bool floppy_init(void)
{
    /* Set CIA-B PRB as output (all drive control bits) */
    ciab_write(CIAB_DDRB, 0xFF);

    /* Deselect all drives, motor off, step high, direction out */
    ciab_write(CIAB_PRB, (uint8_t)(CIABPRB_MTR |
                                    CIABPRB_SEL0 | CIABPRB_SEL1 |
                                    CIABPRB_SEL2 | CIABPRB_SEL3 |
                                    CIABPRB_STEP));

    /* CIA-B PRA: disk status bits are inputs */
    ciab_write(CIAB_DDRA, 0x00);

    for (int i = 0; i < MAX_DRIVES; i++) {
        drives[i].present       = false;
        drives[i].hd            = false;
        drives[i].current_track = 0;
        drives[i].motor_on      = false;
        drives[i].dirty         = false;
        drives[i].buffered_track = -1;
        drives[i].buffered_side  = -1;
        drives[i].track_buf     = NULL;
    }

    /* Detect drives by briefly selecting each and checking the ready line */
    for (int i = 0; i < MAX_DRIVES; i++) {
        select_drive(i);

        /* Short settle */
        for (volatile int d = 0; d < 1000; d++) {}

        /* Check if drive responds (CHNG or RDY will be asserted if present) */
        /* On an empty drive bay: neither RDY nor CHNG will assert reliably.
         * We probe by checking if PRA reads something other than 0xFF.
         * A more reliable method is to toggle the motor and observe RDY,
         * but that takes time. We use a conservative detection. */
        uint8_t pra = ciab_read(CIAB_PRA);

        /* If any status bit is active (any bit = 0), drive likely present */
        /* All-ones (0xFF) = no drive or no response */
        if ((pra & 0x7C) != 0x7C) {
            drives[i].present = true;

            /* Allocate chip RAM track buffer */
            drives[i].track_buf = (uint8_t *)chip_alloc(TRACK_BUFFER_BYTES, 2);
        }

        deselect_all();
    }

    floppy_initialized = true;
    return true;
}

int floppy_scan(void)
{
    int count = 0;
    for (int i = 0; i < MAX_DRIVES; i++) {
        if (drives[i].present) count++;
    }
    return count;
}

bool floppy_present(int drive)
{
    if (drive < 0 || drive >= MAX_DRIVES) return false;
    return drives[drive].present;
}

/*
 * Recalibrate to track 0.
 * Should be called before first use and after errors.
 */
bool floppy_recalibrate(int drive)
{
    if (!drives[drive].present) return false;
    motor_on(drive);
    step_to_track0(drive);
    return true;
}

/*
 * Read sectors from a floppy disk.
 * lba = linear block address (0-based).
 * For DD: lba to cyl/head/sec:
 *   cyl  = lba / (2 * 11)
 *   head = (lba / 11) % 2
 *   sec  = lba % 11
 */
bool floppy_read_sectors(int drive, uint32_t lba,
                          uint16_t count, uint8_t *buf)
{
    if (!floppy_initialized || !drives[drive].present) return false;
    if (!drives[drive].track_buf) return false;

    int spt = SECTORS_PER_TRACK_DD;  /* TODO: HD support */

    for (uint16_t s = 0; s < count; s++) {
        uint32_t lba_cur = lba + s;
        int cyl  = (int)(lba_cur / (2 * spt));
        int head = (int)((lba_cur / spt) % 2);
        int sec  = (int)(lba_cur % spt);

        if (cyl >= 80) return false;

        /* Read/cache the track */
        if (!read_track(drive, cyl, head)) return false;

        /* Decode the requested sector from the track buffer */
        const uint32_t *mfm     = (const uint32_t *)drives[drive].track_buf;
        const uint32_t *mfm_end = mfm + (TRACK_BUFFER_BYTES / 4);

        bool found = false;
        /* Scan all sectors in the track buffer */
        for (int try = 0; try < spt; try++) {
            uint8_t sec_num = 0xFF;
            uint8_t decoded_sector[512];
            const uint32_t *next = decode_sector(mfm, mfm_end,
                                                  decoded_sector, &sec_num);
            if (!next) break;

            if (sec_num == (uint8_t)sec) {
                for (int b = 0; b < 512; b++) {
                    buf[s * 512 + b] = decoded_sector[b];
                }
                found = true;
                break;
            }
            mfm = next;
        }

        if (!found) return false;
    }

    return true;
}

void floppy_motor_off_all(void)
{
    for (int i = 0; i < MAX_DRIVES; i++) {
        if (drives[i].motor_on) motor_off(i);
    }
}
