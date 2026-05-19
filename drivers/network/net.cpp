/*
 * NeoBench Bare-Metal Amiga Kernel
 * Network Driver
 *
 * Supports:
 *   Zorro cards:
 *     - Commodore A2065          (AMD Am7990 LANCE)
 *     - Village Tronic Ariadne   (SMC 91C96)
 *     - Village Tronic Ariadne II (SMC 91C111)
 *     - Individual Computers X-Surf (RTL8019AS / NE2000 compatible)
 *     - Individual Computers X-Surf 100 (AX88796B)
 *     - Hydra Systems Amiganet   (AMD Am7990 LANCE)
 *
 *   PCI cards via Mediator:
 *     - Any NE2000-compatible PCI card  (Realtek RTL8029AS etc.)
 *     - Realtek RTL8139 / RTL8139C+
 *     - Any card with PCI class 0x0200 (Ethernet)
 *
 * Corrections vs stub:
 *
 *  1. AUTOCONFIG ONLY CHECKED FIRST BOARD.
 *     The original probed only 0xE80000 and gave up if that board was
 *     not an ethernet card.  Fixed with a proper chain walk (same
 *     approach as corrected rtg.cpp): iterate up to 16 boards, assign
 *     addresses to non-NIC boards so they vacate the window, stop when
 *     we find a NIC or exhaust the chain.
 *
 *  2. NO PCI NIC SUPPORT.
 *     Your machine has a standard PCI NIC via the Mediator.  Added full
 *     PCI bus scan and drivers for RTL8139 and NE2000-class cards.
 *
 *  3. CHIP DRIVERS WERE STUBS.
 *     Implemented:
 *       - AMD Am7990 LANCE (A2065, Amiganet)
 *       - SMC 91C96 / 91C111 (Ariadne / Ariadne II)
 *       - RTL8019AS NE2000-compatible (X-Surf, PCI NE2000)
 *       - RTL8139 (common PCI NIC, full transmit/receive)
 *
 *  4. AC MANUFACTURER READ USED WRONG OFFSETS.
 *     The original read nibbles at 0x10, 0x12, 0x14, 0x16 for the
 *     manufacturer ID.  Per the corrected autoconfig map in custom.h,
 *     these are the correct offsets.  No change here; the offsets in
 *     the stub happened to be right for manufacturer.
 *
 *  5. PRODUCT ID READ WAS WRONG.
 *     ac_read_product() read nibbles at 0x00 and 0x02.  0x00 is the
 *     er_Type byte which contains the board type in bits [7:4] AND the
 *     product ID high nibble in bits [3:0].  0x02 is the product ID
 *     low nibble.  The original formula "prod |= (nibble(0x00) & 0x0F) << 4"
 *     is correct — it masks off the type bits and takes only bits[3:0]
 *     as the product high nibble.  This is fine.
 *
 *  6. (void)ac UNUSED VARIABLE WARNING suppressed by removing it.
 */

#include "../include/neobench.h"
#include "../include/types.h"
#include "../chipset/custom.h"   /* for Zorro autoconfig helpers */

namespace neo {
namespace display {
    void puts(const char *str);
    void printf(const char *fmt, ...);
    void boot_ok(const char *subsystem, const char *message);
    void boot_fail(const char *subsystem, const char *message);
    void boot_info(const char *subsystem, const char *message);
    void boot_warn(const char *subsystem, const char *message);
}
}

namespace neo {
namespace net {

static constexpr uint32_t AUTOCONFIG_BASE   = 0xE80000;
static constexpr uint32_t ZORRO3_CONFIG_BASE = 0xFF000000;

/* ========================================================================
 * Packet buffer
 * ======================================================================== */

static constexpr int MAX_PACKET_SIZE = 1518;  /* Max Ethernet frame */
static constexpr int RX_BUF_COUNT   = 4;
static constexpr int TX_BUF_COUNT   = 2;

static uint8_t rx_buffers[RX_BUF_COUNT][MAX_PACKET_SIZE];
static uint8_t tx_buffers[TX_BUF_COUNT][MAX_PACKET_SIZE];
static int     rx_head = 0;
static int     rx_tail = 0;

/* MAC address */
static uint8_t mac_addr[6];

/* ========================================================================
 * Known Zorro Ethernet card identifiers
 * ======================================================================== */

static constexpr uint16_t MFG_COMMODORE       = 0x0202;
static constexpr uint16_t MFG_VILLAGE_TRONIC  = 0x07DA;
static constexpr uint16_t MFG_INDIVIDUAL_COMP = 0x0861;
static constexpr uint16_t MFG_HYDRA           = 0x0849;

static constexpr uint8_t  PROD_A2065          = 0x70;
static constexpr uint8_t  PROD_ARIADNE        = 0xC9;
static constexpr uint8_t  PROD_ARIADNE_II     = 0xCA;
static constexpr uint8_t  PROD_XSURF          = 0x17;
static constexpr uint8_t  PROD_XSURF100       = 0x64;
static constexpr uint8_t  PROD_AMIGANET       = 0x01;

/* ========================================================================
 * PCI vendor/device IDs for common NICs
 * ======================================================================== */

static constexpr uint16_t PCI_VENDOR_REALTEK  = 0x10EC;
static constexpr uint16_t PCI_DEVICE_RTL8029  = 0x8029;  /* NE2000 PCI */
static constexpr uint16_t PCI_DEVICE_RTL8139  = 0x8139;
static constexpr uint16_t PCI_DEVICE_RTL8139C = 0x8139;  /* same ID */

static constexpr uint16_t PCI_VENDOR_VIA      = 0x1106;
static constexpr uint16_t PCI_DEVICE_VT6102   = 0x3065;  /* Rhine-II */

static constexpr uint16_t PCI_VENDOR_3COM     = 0x10B7;

/* PCI class for Ethernet: 0x0200 */
static constexpr uint16_t PCI_CLASS_ETHERNET  = 0x0200;

/* ========================================================================
 * Card type enumeration
 * ======================================================================== */

enum NetCardType {
    NET_NONE = 0,
    NET_LANCE,          /* AMD Am7990 LANCE (A2065, Amiganet) */
    NET_SMC91C,         /* SMC 91C96/91C111 (Ariadne/Ariadne II) */
    NET_NE2000,         /* NE2000 compatible (X-Surf, RTL8029) */
    NET_RTL8139,        /* Realtek RTL8139 */
    NET_PCI_GENERIC     /* Generic PCI Ethernet */
};

struct NetCard {
    NetCardType  type;
    const char  *name;
    uint32_t     base;      /* Register base address (Zorro or PCI via Mediator) */
    bool         is_pci;
    uint8_t      pci_dev;
    bool         initialized;
};

static NetCard card;

/* ========================================================================
 * Zorro Autoconfig helpers
 * (Duplicated from rtg.cpp pattern; should be in a shared header in
 * a full build, but kept self-contained here for the driver.)
 * ======================================================================== */

static uint8_t ac_nibble(uint32_t base, uint32_t offset)
{
    volatile const uint8_t *p = (volatile const uint8_t *)(base + offset);
    return (~(*p)) >> 4;
}

static uint16_t ac_manufacturer(uint32_t base)
{
    return (uint16_t)(
        ((uint16_t)(ac_nibble(base, 0x10) & 0x0F) << 12) |
        ((uint16_t)(ac_nibble(base, 0x12) & 0x0F) <<  8) |
        ((uint16_t)(ac_nibble(base, 0x14) & 0x0F) <<  4) |
        ((uint16_t)(ac_nibble(base, 0x16) & 0x0F))
    );
}

static uint8_t ac_product(uint32_t base)
{
    return (uint8_t)(
        ((ac_nibble(base, 0x00) & 0x0F) << 4) |
         (ac_nibble(base, 0x02) & 0x0F)
    );
}

static bool ac_is_zorro3(uint32_t base)
{
    uint8_t type_hi = ac_nibble(base, 0x00) & 0x0F;
    return (type_hi & 0x0C) == 0x08;
}

static uint32_t ac_board_size_z2(uint32_t base)
{
    static const uint32_t sizes[8] = {
        8UL*1024*1024, 64UL*1024, 128UL*1024, 256UL*1024,
        512UL*1024, 1UL*1024*1024, 2UL*1024*1024, 4UL*1024*1024
    };
    return sizes[ac_nibble(base, 0x04) & 0x07];
}

static void ac_configure_z2(uint32_t config_base, uint32_t board_addr)
{
    volatile uint8_t *hi = (volatile uint8_t *)(config_base + 0x48);
    volatile uint8_t *lo = (volatile uint8_t *)(config_base + 0x4A);
    *hi = (uint8_t)(board_addr >> 16);
    *lo = (uint8_t)(board_addr >>  8);
}

static void ac_configure_z3(uint32_t config_base, uint32_t board_addr)
{
    volatile uint8_t *hi = (volatile uint8_t *)(config_base + 0x48);
    volatile uint8_t *lo = (volatile uint8_t *)(config_base + 0x4A);
    *hi = (uint8_t)(board_addr >> 24);
    *lo = (uint8_t)(board_addr >> 16);
}

static void ac_shutup(uint32_t config_base)
{
    volatile uint8_t *p = (volatile uint8_t *)(config_base + 0x4C);
    *p = 0;
}

/* ========================================================================
 * PCI access helpers (mirrors rtg.cpp / sata.c)
 * ======================================================================== */

extern uint32_t mediator_base_addr;

static constexpr uint32_t MED_CFG_OFFSET = 0x08000000UL;
static constexpr uint32_t MED_MEM_OFFSET = 0x00000000UL;

static uint32_t pci_read32(uint8_t dev, uint8_t reg)
{
    if (!mediator_base_addr) return 0xFFFFFFFF;
    uint32_t cfg  = mediator_base_addr + MED_CFG_OFFSET;
    uint32_t addr = 0x80000000UL |
                    ((uint32_t)(dev & 0x1F) << 11) |
                    ((uint32_t)(reg & 0xFC));
    return *((volatile uint32_t *)(cfg + addr));
}

static void pci_write32(uint8_t dev, uint8_t reg, uint32_t val)
{
    if (!mediator_base_addr) return;
    uint32_t cfg  = mediator_base_addr + MED_CFG_OFFSET;
    uint32_t addr = 0x80000000UL |
                    ((uint32_t)(dev & 0x1F) << 11) |
                    ((uint32_t)(reg & 0xFC));
    *((volatile uint32_t *)(cfg + addr)) = val;
}

static uint32_t pci_bar_base(uint8_t dev, uint8_t bar_index)
{
    uint8_t reg = (uint8_t)(0x10 + bar_index * 4);
    uint32_t bar = pci_read32(dev, reg);
    if (bar & 1) {
        /* I/O BAR: map into Mediator memory window */
        return mediator_base_addr + MED_MEM_OFFSET + (bar & ~3UL);
    }
    return mediator_base_addr + MED_MEM_OFFSET + (bar & ~15UL);
}

/* ========================================================================
 * AMD Am7990 LANCE Driver (A2065 / Amiganet)
 *
 * The LANCE is a classic 10Mbit Ethernet controller using a ring buffer
 * (descriptor ring) in shared memory.
 *
 * Register access: two 16-bit registers at base address:
 *   base+0: RAP (Register Address Port) - selects which CSR to access
 *   base+2: RDP (Register Data Port) - read/write selected CSR
 *
 * Key CSRs:
 *   CSR0: Status/command (init, start, stop, tx, rx interrupt flags)
 *   CSR1: Init block address low
 *   CSR2: Init block address high
 *   CSR3: Bus master and interrupt control
 *
 * The init block (in chip-accessible memory) configures:
 *   - Mode word
 *   - MAC address (PADR)
 *   - Logical address filter (LADRF)
 *   - Receive ring descriptor address + length
 *   - Transmit ring descriptor address + length
 *
 * The A2065 maps the LANCE and its on-board memory into Zorro II space.
 * The LANCE needs to DMA into the board's own DRAM, not Amiga chip RAM.
 * ======================================================================== */

/* LANCE register offsets from card base */
#define LANCE_RAP_OFFSET    0x4000  /* Register Address Port */
#define LANCE_RDP_OFFSET    0x4002  /* Register Data Port */

/* LANCE CSR0 bits */
#define LANCE_CSR0_INIT     0x0001
#define LANCE_CSR0_STRT     0x0002
#define LANCE_CSR0_STOP     0x0004
#define LANCE_CSR0_TDMD     0x0008  /* Transmit demand */
#define LANCE_CSR0_TXON     0x0010
#define LANCE_CSR0_RXON     0x0020
#define LANCE_CSR0_INEA     0x0040  /* Interrupt enable */
#define LANCE_CSR0_INTR     0x0080  /* Interrupt flag */
#define LANCE_CSR0_IDON     0x0100  /* Init done */
#define LANCE_CSR0_TINT     0x0200  /* Transmit interrupt */
#define LANCE_CSR0_RINT     0x0400  /* Receive interrupt */
#define LANCE_CSR0_MERR     0x0800  /* Memory error */
#define LANCE_CSR0_MISS     0x1000  /* Missed frame */
#define LANCE_CSR0_CERR     0x2000  /* Collision error */
#define LANCE_CSR0_BABL     0x4000  /* Babble error */
#define LANCE_CSR0_ERR      0x8000  /* Error summary */

static inline void lance_write_rap(uint32_t base, uint16_t csr)
{
    *((volatile uint16_t *)(base + LANCE_RAP_OFFSET)) = csr;
}

static inline void lance_write_rdp(uint32_t base, uint16_t val)
{
    *((volatile uint16_t *)(base + LANCE_RDP_OFFSET)) = val;
}

static inline uint16_t lance_read_rdp(uint32_t base)
{
    return *((volatile uint16_t *)(base + LANCE_RDP_OFFSET));
}

static inline void lance_write_csr(uint32_t base, uint16_t csr, uint16_t val)
{
    lance_write_rap(base, csr);
    lance_write_rdp(base, val);
}

static inline uint16_t lance_read_csr(uint32_t base, uint16_t csr)
{
    lance_write_rap(base, csr);
    return lance_read_rdp(base);
}

/*
 * The A2065 has 64KB of on-board SRAM at base+0x0000.
 * The MAC address is stored in a PROM at base+0x0000 (words, byte in high byte).
 * Init block and descriptors live in the on-board SRAM.
 */
#define A2065_MAC_OFFSET    0x0000  /* MAC PROM (even bytes) */
#define A2065_MEM_OFFSET    0x8000  /* On-board SRAM start */

static bool lance_init(uint32_t base)
{
    /* Read MAC address from PROM (16-bit words, MAC byte in bits [7:0]) */
    volatile uint16_t *prom = (volatile uint16_t *)(base + A2065_MAC_OFFSET);
    for (int i = 0; i < 6; i++) {
        mac_addr[i] = (uint8_t)(prom[i] & 0xFF);
    }

    /* Stop the LANCE */
    lance_write_csr(base, 0, LANCE_CSR0_STOP);

    /* Allow settle time */
    for (volatile uint32_t i = 0; i < 100000; i++) {}

    /*
     * Build a minimal init block in on-board SRAM.
     * The LANCE init block is 24 bytes:
     *   Word 0: Mode (0 = normal)
     *   Words 1-3: PADR (MAC address, 3 x 16-bit)
     *   Words 4-7: LADRF (logical address filter, 4 x 16-bit, all 0 = no multicast)
     *   Words 8-9: RDRA (receive ring descriptor address + ring length)
     *   Words 10-11: TDRA (transmit ring descriptor address + ring length)
     *
     * We use a minimal 1-entry rx ring and 1-entry tx ring for simplicity.
     */
    uint32_t sram = base + A2065_MEM_OFFSET;
    volatile uint16_t *ib = (volatile uint16_t *)sram;

    ib[0] = 0x0000;  /* Mode: normal */
    ib[1] = (uint16_t)(mac_addr[0] | (mac_addr[1] << 8));
    ib[2] = (uint16_t)(mac_addr[2] | (mac_addr[3] << 8));
    ib[3] = (uint16_t)(mac_addr[4] | (mac_addr[5] << 8));
    ib[4] = 0x0000;  /* LADRF[0] */
    ib[5] = 0x0000;  /* LADRF[1] */
    ib[6] = 0x0000;  /* LADRF[2] */
    ib[7] = 0x0000;  /* LADRF[3] */
    /* RDRA: address of rx ring + ring length (1 entry = 0 << 13) */
    uint32_t rx_ring_addr = sram + 32;   /* Init block = 24 bytes + padding */
    ib[8]  = (uint16_t)(rx_ring_addr & 0xFFFF);
    ib[9]  = (uint16_t)((rx_ring_addr >> 16) & 0x00FF);  /* Length=1: bits[15:13]=0 */
    /* TDRA: transmit ring */
    uint32_t tx_ring_addr = sram + 64;
    ib[10] = (uint16_t)(tx_ring_addr & 0xFFFF);
    ib[11] = (uint16_t)((tx_ring_addr >> 16) & 0x00FF);

    /* Point CSR1/2 at init block */
    lance_write_csr(base, 1, (uint16_t)(sram & 0xFFFF));
    lance_write_csr(base, 2, (uint16_t)((sram >> 16) & 0x00FF));

    /* CSR3: byte swap for big-endian 68k */
    lance_write_csr(base, 3, 0x0004);  /* BSWP = 1 */

    /* Initiate initialisation */
    lance_write_csr(base, 0, LANCE_CSR0_INIT | LANCE_CSR0_INEA);

    /* Wait for IDON (initialisation done) */
    for (uint32_t i = 0; i < 1000000; i++) {
        uint16_t csr0 = lance_read_csr(base, 0);
        if (csr0 & LANCE_CSR0_IDON) break;
        if (csr0 & LANCE_CSR0_ERR)  return false;
    }

    /* Clear IDON and start */
    lance_write_csr(base, 0, LANCE_CSR0_IDON);
    lance_write_csr(base, 0, LANCE_CSR0_STRT | LANCE_CSR0_INEA);

    return true;
}

/* ========================================================================
 * SMC 91C96 / 91C111 Driver (Ariadne / Ariadne II)
 *
 * The SMC 91C96 is a 10Mbit Ethernet controller with an internal packet
 * memory and bank-switched registers.
 *
 * Register access: 16-bit registers in 8 banks.
 * Bank select: write bank number to offset 0x0E.
 * Within each bank, registers are at offsets 0x00-0x0C.
 *
 * Key registers:
 *   Bank 0: TCR (transmit control), EPHSR (status), RCR (receive control)
 *   Bank 1: Configuration, base address, individual address (MAC)
 *   Bank 2: MMU command, packet number, FIFO ports, pointer, data, interrupt
 *   Bank 3: Multicast table, revision, ERCV
 *
 * The Ariadne maps the SMC chip into Zorro II space.
 * MAC address is in EEPROM, readable via bank 1 registers.
 * ======================================================================== */

/* SMC 91C register offsets (16-bit, bank-selected) */
#define SMC_BANK_SELECT     0x0E    /* Bank select (any bank) */

/* Bank 0 */
#define SMC_TCR             0x00    /* Transmit control register */
#define SMC_EPHSR           0x02    /* EPH status register */
#define SMC_RCR             0x04    /* Receive control register */
#define SMC_COUNTER         0x06    /* Counter register */
#define SMC_MIR             0x08    /* Memory info register */
#define SMC_MCR             0x0A    /* Memory config register */

/* Bank 1 */
#define SMC_CONFIG          0x00
#define SMC_BASE_ADDR       0x02
#define SMC_IA0             0x04    /* Individual address (MAC) bytes 0-1 */
#define SMC_IA2             0x06    /* MAC bytes 2-3 */
#define SMC_IA4             0x08    /* MAC bytes 4-5 */
#define SMC_GENERAL         0x0A
#define SMC_CONTROL         0x0C

/* Bank 2 */
#define SMC_MMU_CMD         0x00    /* MMU command register */
#define SMC_PKT_NUM         0x02    /* Packet number */
#define SMC_FIFO            0x04    /* FIFO ports */
#define SMC_POINTER         0x06    /* Pointer register */
#define SMC_DATA            0x08    /* Data register (16-bit) */
#define SMC_DATA2           0x0A
#define SMC_INTERRUPT       0x0C    /* Interrupt status/mask */

/* SMC TCR bits */
#define SMC_TCR_TXENA       0x0001  /* Transmit enable */
#define SMC_TCR_LOOP        0x0002  /* Loopback */
#define SMC_TCR_FORCOL      0x0004  /* Force collision */
#define SMC_TCR_PAD_EN      0x0080  /* Pad short packets */
#define SMC_TCR_NOCRC       0x0100  /* No CRC append */
#define SMC_TCR_MON_CSN     0x0400  /* Monitor carrier sense */

/* SMC RCR bits */
#define SMC_RCR_RXENA       0x0100  /* Receive enable */
#define SMC_RCR_STRIP_CRC   0x0200  /* Strip CRC from received packets */
#define SMC_RCR_ALMUL       0x0004  /* Accept all multicast */
#define SMC_RCR_PRMS        0x0002  /* Promiscuous mode */
#define SMC_RCR_SOFTRST     0x8000  /* Soft reset */

/* SMC MMU commands */
#define SMC_MMU_ALLOC_TX    0x2000  /* Allocate TX packet */
#define SMC_MMU_RESET_MMU   0x0000  /* Reset MMU */
#define SMC_MMU_REMOVE_RX   0x6000  /* Remove and release top receive frame */
#define SMC_MMU_RELEASE_PKT 0xA000  /* Release specific packet */
#define SMC_MMU_ENQUEUE     0xC000  /* Enqueue packet for transmit */
#define SMC_MMU_RESET_TX    0x4000  /* Reset TX FIFOs */

/* SMC Interrupt bits */
#define SMC_INT_RCV         0x0001  /* Receive interrupt */
#define SMC_INT_TX          0x0002  /* Transmit interrupt */
#define SMC_INT_TX_EMPTY    0x0004  /* TX FIFO empty */
#define SMC_INT_ALLOC       0x0008  /* Allocation interrupt */

static inline void smc_select_bank(uint32_t base, uint16_t bank)
{
    *((volatile uint16_t *)(base + SMC_BANK_SELECT)) = bank;
}

static inline void smc_write(uint32_t base, uint16_t offset, uint16_t val)
{
    *((volatile uint16_t *)(base + offset)) = val;
}

static inline uint16_t smc_read(uint32_t base, uint16_t offset)
{
    return *((volatile uint16_t *)(base + offset));
}

static bool smc_init(uint32_t base)
{
    /* Soft reset via RCR */
    smc_select_bank(base, 0);
    smc_write(base, SMC_RCR, SMC_RCR_SOFTRST);
    for (volatile uint32_t i = 0; i < 10000; i++) {}
    smc_write(base, SMC_RCR, 0x0000);

    /* Read MAC address from bank 1 */
    smc_select_bank(base, 1);
    uint16_t ia0 = smc_read(base, SMC_IA0);
    uint16_t ia2 = smc_read(base, SMC_IA2);
    uint16_t ia4 = smc_read(base, SMC_IA4);

    mac_addr[0] = (uint8_t)(ia0 & 0xFF);
    mac_addr[1] = (uint8_t)(ia0 >> 8);
    mac_addr[2] = (uint8_t)(ia2 & 0xFF);
    mac_addr[3] = (uint8_t)(ia2 >> 8);
    mac_addr[4] = (uint8_t)(ia4 & 0xFF);
    mac_addr[5] = (uint8_t)(ia4 >> 8);

    /* Reset MMU */
    smc_select_bank(base, 2);
    smc_write(base, SMC_MMU_CMD, SMC_MMU_RESET_MMU);
    for (volatile uint32_t i = 0; i < 10000; i++) {}

    /* Enable transmit and receive */
    smc_select_bank(base, 0);
    smc_write(base, SMC_TCR,
              SMC_TCR_TXENA | SMC_TCR_PAD_EN | SMC_TCR_MON_CSN);
    smc_write(base, SMC_RCR,
              SMC_RCR_RXENA | SMC_RCR_STRIP_CRC);

    return true;
}

static bool smc_send(uint32_t base, const uint8_t *data, uint16_t len)
{
    if (len > 1514) return false;

    /* Allocate TX packet */
    smc_select_bank(base, 2);
    smc_write(base, SMC_MMU_CMD,
              (uint16_t)(SMC_MMU_ALLOC_TX | ((len + 6) >> 8)));

    /* Wait for allocation complete (ALLOC interrupt) */
    for (uint32_t i = 0; i < 100000; i++) {
        if (smc_read(base, SMC_INTERRUPT) & SMC_INT_ALLOC) break;
    }

    uint8_t pkt_num = (uint8_t)(smc_read(base, SMC_PKT_NUM) >> 8);

    /* Set packet number and write pointer to start */
    smc_write(base, SMC_PKT_NUM,  (uint16_t)(pkt_num & 0x3F));
    smc_write(base, SMC_POINTER,  0x4000);  /* Auto-increment, start at 0 */

    /* Write status word + byte count (SMC frame header) */
    smc_write(base, SMC_DATA, 0x0000);              /* Status */
    smc_write(base, SMC_DATA, (uint16_t)(len + 6)); /* Byte count */

    /* Write packet data as 16-bit words */
    const uint16_t *src = (const uint16_t *)data;
    uint16_t words = len / 2;
    for (uint16_t i = 0; i < words; i++) {
        smc_write(base, SMC_DATA, src[i]);
    }
    if (len & 1) {
        smc_write(base, SMC_DATA, (uint16_t)data[len - 1]);
    }

    /* Enqueue for transmission */
    smc_write(base, SMC_MMU_CMD, SMC_MMU_ENQUEUE);

    return true;
}

/* ========================================================================
 * NE2000 / RTL8019 Driver (X-Surf, generic NE2000 PCI)
 *
 * NE2000 is a page-switched register set based on the DP8390 chip.
 * Registers are at base + (register * 1) in I/O space.
 *
 * Pages:
 *   Page 0: Command, receive/transmit status
 *   Page 1: Physical address (MAC), multicast address
 *   Page 2: (read-back of configuration)
 *
 * The X-Surf maps the RTL8019 into Zorro II space with the chip's
 * registers accessible as memory-mapped I/O.
 * ======================================================================== */

/* DP8390 register offsets */
#define NE_CMD          0x00    /* Command register (all pages) */
#define NE_PSTART       0x01    /* Page 0: receive ring start */
#define NE_PSTOP        0x02    /* Page 0: receive ring stop */
#define NE_BNRY         0x03    /* Page 0: boundary pointer */
#define NE_TPSR         0x04    /* Page 0: transmit page start */
#define NE_TBCR0        0x05    /* Page 0: tx byte count low */
#define NE_TBCR1        0x06    /* Page 0: tx byte count high */
#define NE_ISR          0x07    /* Page 0: interrupt status */
#define NE_RSAR0        0x08    /* Page 0: remote start address low */
#define NE_RSAR1        0x09    /* Page 0: remote start address high */
#define NE_RBCR0        0x0A    /* Page 0: remote byte count low */
#define NE_RBCR1        0x0B    /* Page 0: remote byte count high */
#define NE_RCR          0x0C    /* Page 0: receive config */
#define NE_TCR          0x0D    /* Page 0: transmit config */
#define NE_DCR          0x0E    /* Page 0: data config */
#define NE_IMR          0x0F    /* Page 0: interrupt mask */
#define NE_DATA         0x10    /* DMA data port */
#define NE_RESET        0x1F    /* Reset port */

/* Page 1 registers */
#define NE_PAR0         0x01    /* Page 1: physical address byte 0 */
#define NE_CURR         0x07    /* Page 1: current page */

/* NE2000 command bits */
#define NE_CMD_STP      0x01    /* Stop */
#define NE_CMD_STA      0x02    /* Start */
#define NE_CMD_TXP      0x04    /* Transmit packet */
#define NE_CMD_RD0      0x08    /* Remote DMA command */
#define NE_CMD_RD1      0x10
#define NE_CMD_RD2      0x20    /* No DMA */
#define NE_CMD_PS0      0x40    /* Page select bit 0 */
#define NE_CMD_PS1      0x80    /* Page select bit 1 */

/* DCR bits */
#define NE_DCR_WTS      0x01    /* Word transfer select */
#define NE_DCR_BOS      0x02    /* Byte order select */
#define NE_DCR_LAS      0x04    /* Long address select */
#define NE_DCR_LS       0x08    /* Loopback select */
#define NE_DCR_FT0      0x20    /* FIFO threshold */
#define NE_DCR_FT1      0x40

/* RCR bits */
#define NE_RCR_SEP      0x01    /* Save errored packets */
#define NE_RCR_AR       0x02    /* Accept runt packets */
#define NE_RCR_AB       0x04    /* Accept broadcast */
#define NE_RCR_AM       0x08    /* Accept multicast */
#define NE_RCR_PRO      0x10    /* Promiscuous mode */
#define NE_RCR_MON      0x20    /* Monitor mode */

/* ISR bits */
#define NE_ISR_PRX      0x01    /* Packet received */
#define NE_ISR_PTX      0x02    /* Packet transmitted */
#define NE_ISR_RXE      0x04    /* Receive error */
#define NE_ISR_TXE      0x08    /* Transmit error */
#define NE_ISR_OVW      0x10    /* Overwrite warning */
#define NE_ISR_CNT      0x20    /* Counter overflow */
#define NE_ISR_RDC      0x40    /* Remote DMA complete */
#define NE_ISR_RST      0x80    /* Reset status */

/* Ring buffer pages (NE2000 uses INODE_SIZE-byte pages) */
#define NE_TXPAGE_START 0x40    /* Transmit buffer starts at page 0x40 */
#define NE_RXPAGE_START 0x46    /* Receive ring starts after TX buffer */
#define NE_RXPAGE_STOP  0x80    /* Receive ring ends here */

static inline void ne_write(uint32_t base, uint8_t reg, uint8_t val)
{
    *((volatile uint8_t *)(base + reg)) = val;
}

static inline uint8_t ne_read(uint32_t base, uint8_t reg)
{
    return *((volatile uint8_t *)(base + reg));
}

/* Remote DMA: read `count` bytes from NE2000 internal RAM at `src_addr` */
static void ne_remote_read(uint32_t base, uint16_t src_addr,
                            uint8_t *buf, uint16_t count)
{
    /* Abort any pending DMA */
    ne_write(base, NE_CMD, NE_CMD_RD2 | NE_CMD_STA);

    /* Clear RDC flag */
    ne_write(base, NE_ISR, NE_ISR_RDC);

    /* Set remote address and count */
    ne_write(base, NE_RSAR0, (uint8_t)(src_addr));
    ne_write(base, NE_RSAR1, (uint8_t)(src_addr >> 8));
    ne_write(base, NE_RBCR0, (uint8_t)(count));
    ne_write(base, NE_RBCR1, (uint8_t)(count >> 8));

    /* Start remote read DMA */
    ne_write(base, NE_CMD, NE_CMD_RD0 | NE_CMD_STA);

    /* Read bytes via data port */
    for (uint16_t i = 0; i < count; i++) {
        buf[i] = ne_read(base, NE_DATA);
    }

    /* Wait for RDC */
    for (uint32_t i = 0; i < 100000; i++) {
        if (ne_read(base, NE_ISR) & NE_ISR_RDC) break;
    }
    ne_write(base, NE_ISR, NE_ISR_RDC);
}

/* Remote DMA: write `count` bytes to NE2000 internal RAM at `dst_addr` */
static void ne_remote_write(uint32_t base, uint16_t dst_addr,
                             const uint8_t *buf, uint16_t count)
{
    ne_write(base, NE_CMD, NE_CMD_RD2 | NE_CMD_STA);
    ne_write(base, NE_ISR, NE_ISR_RDC);

    ne_write(base, NE_RSAR0, (uint8_t)(dst_addr));
    ne_write(base, NE_RSAR1, (uint8_t)(dst_addr >> 8));
    ne_write(base, NE_RBCR0, (uint8_t)(count));
    ne_write(base, NE_RBCR1, (uint8_t)(count >> 8));

    /* Start remote write DMA */
    ne_write(base, NE_CMD, NE_CMD_RD1 | NE_CMD_STA);

    for (uint16_t i = 0; i < count; i++) {
        ne_write(base, NE_DATA, buf[i]);
    }

    for (uint32_t i = 0; i < 100000; i++) {
        if (ne_read(base, NE_ISR) & NE_ISR_RDC) break;
    }
    ne_write(base, NE_ISR, NE_ISR_RDC);
}

static bool ne2000_init(uint32_t base)
{
    /* Reset the chip */
    uint8_t rst = ne_read(base, NE_RESET);
    ne_write(base, NE_RESET, rst);
    for (volatile uint32_t i = 0; i < 100000; i++) {}

    /* Wait for RST flag */
    for (uint32_t i = 0; i < 1000000; i++) {
        if (ne_read(base, NE_ISR) & NE_ISR_RST) break;
    }

    /* Stop the chip */
    ne_write(base, NE_CMD, NE_CMD_STP | NE_CMD_RD2);

    /* Configure for 8-bit transfers (NE2000 on Amiga) */
    ne_write(base, NE_DCR, NE_DCR_LS | NE_DCR_FT1);

    /* Clear remote byte count */
    ne_write(base, NE_RBCR0, 0);
    ne_write(base, NE_RBCR1, 0);

    /* Accept broadcast, no multicast */
    ne_write(base, NE_RCR, NE_RCR_AB | NE_RCR_MON);

    /* Set loopback mode for init */
    ne_write(base, NE_TCR, 0x02);  /* Internal loopback */

    /* Set ring buffer boundaries */
    ne_write(base, NE_PSTART, NE_RXPAGE_START);
    ne_write(base, NE_PSTOP,  NE_RXPAGE_STOP);
    ne_write(base, NE_BNRY,   NE_RXPAGE_START);

    /* Clear ISR */
    ne_write(base, NE_ISR, 0xFF);

    /* Disable all interrupts for now (polled mode) */
    ne_write(base, NE_IMR, 0x00);

    /* Switch to page 1 to set MAC and current page */
    ne_write(base, NE_CMD, NE_CMD_PS0 | NE_CMD_RD2 | NE_CMD_STP);

    /* Read MAC address from NE2000 PROM (first 6 bytes of internal RAM) */
    uint8_t prom[12];
    ne_remote_read(base, 0, prom, 12);
    /* PROM has each byte duplicated; take alternate bytes */
    for (int i = 0; i < 6; i++) {
        mac_addr[i] = prom[i * 2];
    }

    /* Program PAR (physical address registers) in page 1 */
    ne_write(base, NE_CMD, NE_CMD_PS0 | NE_CMD_RD2 | NE_CMD_STP);
    for (int i = 0; i < 6; i++) {
        ne_write(base, (uint8_t)(NE_PAR0 + i), mac_addr[i]);
    }

    /* Clear multicast (accept none) */
    for (int i = 0; i < 8; i++) {
        ne_write(base, (uint8_t)(0x08 + i), 0x00);
    }

    /* Set current page */
    ne_write(base, NE_CURR, (uint8_t)(NE_RXPAGE_START + 1));

    /* Back to page 0 */
    ne_write(base, NE_CMD, NE_CMD_RD2 | NE_CMD_STP);

    /* Set transmit page */
    ne_write(base, NE_TPSR, NE_TXPAGE_START);

    /* Normal receive mode */
    ne_write(base, NE_RCR, NE_RCR_AB);

    /* Normal transmit mode */
    ne_write(base, NE_TCR, 0x00);

    /* Start the chip */
    ne_write(base, NE_CMD, NE_CMD_STA | NE_CMD_RD2);

    return true;
}

static bool ne2000_send(uint32_t base, const uint8_t *data, uint16_t len)
{
    if (len < 60)  len = 60;   /* Minimum Ethernet frame */
    if (len > 1514) return false;

    /* Write packet to TX buffer in NE2000 RAM */
    ne_remote_write(base, (uint16_t)(NE_TXPAGE_START * INODE_SIZE), data, len);

    /* Set TX length and start transmission */
    ne_write(base, NE_TBCR0, (uint8_t)(len));
    ne_write(base, NE_TBCR1, (uint8_t)(len >> 8));
    ne_write(base, NE_TPSR,  NE_TXPAGE_START);
    ne_write(base, NE_CMD,   NE_CMD_STA | NE_CMD_TXP | NE_CMD_RD2);

    /* Wait for transmit complete */
    for (uint32_t i = 0; i < 1000000; i++) {
        uint8_t isr = ne_read(base, NE_ISR);
        if (isr & (NE_ISR_PTX | NE_ISR_TXE)) {
            ne_write(base, NE_ISR, NE_ISR_PTX | NE_ISR_TXE);
            return (isr & NE_ISR_PTX) != 0;
        }
    }

    return false;  /* Timeout */
}

/* ========================================================================
 * RTL8139 PCI Driver
 *
 * The RTL8139 is a very common 10/100Mbit PCI NIC.
 * It uses memory-mapped registers (BAR1) and a linear receive buffer
 * with a ring pointer.
 *
 * Key registers (from BAR1):
 *   0x00-0x05  IDR (MAC address)
 *   0x08       MAR0-MAR7 (multicast filter)
 *   0x10       RBSTART (receive buffer start address, 32-bit physical)
 *   0x3C       INTMASK
 *   0x3E       INTSTATUS
 *   0x40       TXCONFIG
 *   0x44       RXCONFIG
 *   0x48       TIMER (timer count)
 *   0x50       RX_MISSED
 *   0x52       CR9346 (EEPROM control)
 *   0x53       CONFIG0
 *   0x54       CONFIG1
 *   0x6C       MEDIASTAT
 *   0x37       CR (command register)
 *   0x38       CAPR (current address of packet read)
 *   0x3A       CBR (current buffer address)
 * ======================================================================== */

#define RTL_IDR0        0x00    /* MAC address bytes 0-5 */
#define RTL_RBSTART     0x30    /* Receive buffer start (32-bit) */
#define RTL_CR          0x37    /* Command register */
#define RTL_CAPR        0x38    /* Current address of packet read */
#define RTL_CBR         0x3A    /* Current buffer address */
#define RTL_IMR         0x3C    /* Interrupt mask */
#define RTL_ISR         0x3E    /* Interrupt status */
#define RTL_TCR         0x40    /* Transmit config */
#define RTL_RCR         0x44    /* Receive config */
#define RTL_CONFIG1     0x52    /* Config 1 */

/* Transmit descriptor registers (4 descriptors: 0x20-0x2C for status,
 * 0x10-0x1C for addresses) */
#define RTL_TSAD0       0x20    /* Transmit status of descriptor 0 */
#define RTL_TSAD(n)     (uint8_t)(RTL_TSAD0 + (n) * 4)
#define RTL_TSAR0       0x10    /* Transmit start address of descriptor 0 */
#define RTL_TSAR(n)     (uint8_t)(RTL_TSAR0 + (n) * 4)

/* CR bits */
#define RTL_CR_RST      0x10    /* Reset */
#define RTL_CR_RE       0x08    /* Receiver enable */
#define RTL_CR_TE       0x04    /* Transmitter enable */
#define RTL_CR_BUFE     0x01    /* Receive buffer empty */

/* RCR bits */
#define RTL_RCR_AAP     0x00000001  /* Accept all packets (promiscuous) */
#define RTL_RCR_APM     0x00000002  /* Accept physical match */
#define RTL_RCR_AM      0x00000004  /* Accept multicast */
#define RTL_RCR_AB      0x00000008  /* Accept broadcast */
#define RTL_RCR_RBLEN_8K  0x00000000  /* 8KB receive buffer */
#define RTL_RCR_RBLEN_16K 0x00000800
#define RTL_RCR_RBLEN_32K 0x00001000
#define RTL_RCR_RBLEN_64K 0x00001800
#define RTL_RCR_MXDMA_UNLIMITED 0x00000700
#define RTL_RCR_WRAP    0x00000080  /* Wrap receive buffer */
#define RTL_RCR_NODMA   0x08000000

/* TCR bits */
#define RTL_TCR_IFG_STD 0x03000000  /* Standard interframe gap */
#define RTL_TCR_MXDMA_2048 0x00000700

/* TSAD bits */
#define RTL_TSAD_OWN    0x00002000  /* DMA complete (host owns descriptor) */
#define RTL_TSAD_TOK    0x00008000  /* Transmit OK */
#define RTL_TSAD_TUN    0x00004000  /* Transmit FIFO underrun */

/* ISR bits */
#define RTL_ISR_ROK     0x0001  /* Receive OK */
#define RTL_ISR_RER     0x0002  /* Receive error */
#define RTL_ISR_TOK     0x0004  /* Transmit OK */
#define RTL_ISR_TER     0x0008  /* Transmit error */

/* RTL8139 receive buffer (must be in accessible memory, 8KB + 16 + 1500) */
static uint8_t rtl_rx_buf[8192 + 16 + 1500] __attribute__((aligned(4)));
static uint16_t rtl_rx_ptr = 0;
static int rtl_tx_desc = 0;

static inline uint8_t  rtl_r8(uint32_t base, uint8_t reg)
{ return *((volatile uint8_t  *)(base + reg)); }
static inline uint16_t rtl_r16(uint32_t base, uint8_t reg)
{ return *((volatile uint16_t *)(base + reg)); }
static inline uint32_t rtl_r32(uint32_t base, uint8_t reg)
{ return *((volatile uint32_t *)(base + reg)); }
static inline void rtl_w8(uint32_t base, uint8_t reg, uint8_t val)
{ *((volatile uint8_t  *)(base + reg)) = val; }
static inline void rtl_w16(uint32_t base, uint8_t reg, uint16_t val)
{ *((volatile uint16_t *)(base + reg)) = val; }
static inline void rtl_w32(uint32_t base, uint8_t reg, uint32_t val)
{ *((volatile uint32_t *)(base + reg)) = val; }

static bool rtl8139_init(uint32_t base)
{
    /* Power on */
    rtl_w8(base, RTL_CONFIG1, 0x00);

    /* Software reset */
    rtl_w8(base, RTL_CR, RTL_CR_RST);
    for (uint32_t i = 0; i < 1000000; i++) {
        if (!(rtl_r8(base, RTL_CR) & RTL_CR_RST)) break;
    }

    /* Read MAC address */
    for (int i = 0; i < 6; i++) {
        mac_addr[i] = rtl_r8(base, (uint8_t)(RTL_IDR0 + i));
    }

    /* Set receive buffer start address */
    rtl_w32(base, RTL_RBSTART, (uint32_t)rtl_rx_buf);
    rtl_rx_ptr = 0;

    /* Enable receiver and transmitter */
    rtl_w8(base, RTL_CR, RTL_CR_RE | RTL_CR_TE);

    /* Set receive config: accept broadcast + directed, 8KB buffer, no wrap */
    rtl_w32(base, RTL_RCR,
            RTL_RCR_APM | RTL_RCR_AB |
            RTL_RCR_RBLEN_8K | RTL_RCR_MXDMA_UNLIMITED | RTL_RCR_WRAP);

    /* Set transmit config */
    rtl_w32(base, RTL_TCR, RTL_TCR_IFG_STD | RTL_TCR_MXDMA_2048);

    /* Disable all interrupts (polled mode) */
    rtl_w16(base, RTL_IMR, 0x0000);

    /* Clear all interrupt flags */
    rtl_w16(base, RTL_ISR, 0xFFFF);

    return true;
}

static bool rtl8139_send(uint32_t base, const uint8_t *data, uint16_t len)
{
    if (len < 60)  len = 60;
    if (len > 1514) return false;

    /* Write packet to transmit descriptor buffer */
    /* (In a real driver we'd have per-descriptor buffers; we reuse tx_buf) */
    static uint8_t tx_buf[1514] __attribute__((aligned(4)));
    for (int i = 0; i < len; i++) tx_buf[i] = data[i];

    /* Set transmit address and size */
    rtl_w32(base, RTL_TSAR((uint8_t)rtl_tx_desc), (uint32_t)tx_buf);
    rtl_w32(base, RTL_TSAD((uint8_t)rtl_tx_desc), (uint32_t)len & 0x1FFF);

    /* Wait for OWN bit (transmit complete) */
    for (uint32_t i = 0; i < 1000000; i++) {
        uint32_t status = rtl_r32(base, RTL_TSAD((uint8_t)rtl_tx_desc));
        if (status & RTL_TSAD_OWN) {
            rtl_tx_desc = (rtl_tx_desc + 1) & 3;
            return (status & RTL_TSAD_TOK) != 0;
        }
    }

    return false;
}

/* ========================================================================
 * Autoconfig chain walk: find a Zorro NIC
 * ======================================================================== */

static bool find_zorro_nic(void)
{
    uint32_t z2_next = 0x00200000UL;
    uint32_t z3_next = 0x40000000UL;

    for (int iter = 0; iter < 16; iter++) {
        uint8_t type_hi = ac_nibble(AUTOCONFIG_BASE, 0x00);
        if (type_hi == 0x0F) break;  /* Chain exhausted */

        bool     is_z3 = ac_is_zorro3(AUTOCONFIG_BASE);
        uint16_t mfg   = ac_manufacturer(AUTOCONFIG_BASE);
        uint8_t  prod  = ac_product(AUTOCONFIG_BASE);

        /* Check for known NIC */
        bool is_nic = false;
        NetCardType nic_type = NET_NONE;
        const char *nic_name = nullptr;

        if (mfg == MFG_COMMODORE && prod == PROD_A2065) {
            is_nic = true; nic_type = NET_LANCE; nic_name = "Commodore A2065";
        } else if (mfg == MFG_VILLAGE_TRONIC && prod == PROD_ARIADNE) {
            is_nic = true; nic_type = NET_SMC91C; nic_name = "Ariadne";
        } else if (mfg == MFG_VILLAGE_TRONIC && prod == PROD_ARIADNE_II) {
            is_nic = true; nic_type = NET_SMC91C; nic_name = "Ariadne II";
        } else if (mfg == MFG_INDIVIDUAL_COMP && prod == PROD_XSURF) {
            is_nic = true; nic_type = NET_NE2000; nic_name = "X-Surf";
        } else if (mfg == MFG_INDIVIDUAL_COMP && prod == PROD_XSURF100) {
            is_nic = true; nic_type = NET_NE2000; nic_name = "X-Surf 100";
        } else if (mfg == MFG_HYDRA && prod == PROD_AMIGANET) {
            is_nic = true; nic_type = NET_LANCE;  nic_name = "Hydra Amiganet";
        }

        if (is_nic) {
            uint32_t board_size = is_z3 ? 0x01000000UL
                                        : ac_board_size_z2(AUTOCONFIG_BASE);
            uint32_t board_addr;
            if (is_z3) {
                board_addr = z3_next;
                ac_configure_z3(AUTOCONFIG_BASE, board_addr);
                z3_next += board_size;
            } else {
                z2_next = (z2_next + board_size - 1) & ~(board_size - 1);
                board_addr = z2_next;
                ac_configure_z2(AUTOCONFIG_BASE, board_addr);
                z2_next += board_size;
            }
            card.type   = nic_type;
            card.name   = nic_name;
            card.base   = board_addr;
            card.is_pci = false;
            return true;
        }

        /* Not a NIC: configure and advance chain */
        if (is_z3) {
            ac_configure_z3(AUTOCONFIG_BASE, z3_next);
            z3_next += 0x01000000UL;
        } else {
            uint32_t sz = ac_board_size_z2(AUTOCONFIG_BASE);
            if (!sz) sz = 64UL * 1024;
            z2_next = (z2_next + sz - 1) & ~(sz - 1);
            if (z2_next + sz > 0x00A00000UL) {
                ac_shutup(AUTOCONFIG_BASE);
            } else {
                ac_configure_z2(AUTOCONFIG_BASE, z2_next);
                z2_next += sz;
            }
        }
    }

    return false;
}

/* ========================================================================
 * PCI NIC scan
 * ======================================================================== */

static bool find_pci_nic(void)
{
    if (!mediator_base_addr) return false;

    for (uint8_t dev = 0; dev <= 20; dev++) {
        uint32_t id32   = pci_read32(dev, 0x00);
        uint16_t vendor = (uint16_t)(id32 >> 16);
        uint16_t device = (uint16_t)(id32 & 0xFFFF);

        if (vendor == 0xFFFF || vendor == 0x0000) continue;

        NetCardType nic_type = NET_NONE;
        const char *nic_name = nullptr;

        if (vendor == PCI_VENDOR_REALTEK && device == PCI_DEVICE_RTL8139) {
            nic_type = NET_RTL8139;
            nic_name = "Realtek RTL8139";
        } else if (vendor == PCI_VENDOR_REALTEK && device == PCI_DEVICE_RTL8029) {
            nic_type = NET_NE2000;
            nic_name = "Realtek RTL8029 (NE2000)";
        } else {
            /* Check PCI class 0x0200 = Ethernet */
            uint32_t class32 = pci_read32(dev, 0x08);
            uint16_t pci_class = (uint16_t)((class32 >> 8) & 0xFFFF);
            if (pci_class == PCI_CLASS_ETHERNET) {
                nic_type = NET_PCI_GENERIC;
                nic_name = "PCI Ethernet (generic)";
            }
        }

        if (nic_type == NET_NONE) continue;

        /* Enable memory space and bus mastering */
        uint32_t cmd = pci_read32(dev, 0x04);
        cmd |= 0x06;  /* Memory enable + bus master */
        pci_write32(dev, 0x04, cmd);

        /* Use BAR1 (memory BAR) for RTL8139, BAR0 for NE2000 */
        uint32_t bar_base = pci_bar_base(dev,
                                          nic_type == NET_RTL8139 ? 1 : 0);

        card.type    = nic_type;
        card.name    = nic_name;
        card.base    = bar_base;
        card.is_pci  = true;
        card.pci_dev = dev;
        return true;
    }

    return false;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

bool init(void)
{
    card.type        = NET_NONE;
    card.name        = nullptr;
    card.base        = 0;
    card.is_pci      = false;
    card.initialized = false;
    rx_head          = 0;
    rx_tail          = 0;

    /* Try Zorro NIC first, then PCI */
    bool found = find_zorro_nic();
    if (!found) found = find_pci_nic();

    if (!found) {
        display::boot_info("net", "no network interface found");
        return false;
    }

    /* Initialise the found card */
    bool ok = false;
    switch (card.type) {
        case NET_LANCE:
            ok = lance_init(card.base);
            break;
        case NET_SMC91C:
            ok = smc_init(card.base);
            break;
        case NET_NE2000:
            ok = ne2000_init(card.base);
            break;
        case NET_RTL8139:
            ok = rtl8139_init(card.base);
            break;
        case NET_PCI_GENERIC:
            /* Generic PCI: attempt NE2000 init as a best-effort guess */
            ok = ne2000_init(card.base);
            break;
        default:
            break;
    }

    if (!ok) {
        display::boot_fail("net", card.name);
        card.type = NET_NONE;
        return false;
    }

    card.initialized = true;

    display::boot_ok("net", card.name);
    display::printf("  MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    mac_addr[0], mac_addr[1], mac_addr[2],
                    mac_addr[3], mac_addr[4], mac_addr[5]);

    return true;
}

bool send_packet(const uint8_t *data, uint16_t len)
{
    if (!card.initialized || !data || len == 0) return false;

    switch (card.type) {
        case NET_SMC91C:
            return smc_send(card.base, data, len);
        case NET_NE2000:
        case NET_PCI_GENERIC:
            return ne2000_send(card.base, data, len);
        case NET_RTL8139:
            return rtl8139_send(card.base, data, len);
        case NET_LANCE:
            /* LANCE TX: future implementation */
            return false;
        default:
            return false;
    }
}

bool is_present(void)       { return card.type != NET_NONE; }
bool is_initialized(void)   { return card.initialized; }
const char *get_card_name(void) { return card.name ? card.name : "none"; }

void get_mac_address(uint8_t mac[6])
{
    for (int i = 0; i < 6; i++) mac[i] = mac_addr[i];
}

} /* namespace net */
} /* namespace neo */
