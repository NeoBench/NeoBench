/*
 * NeoBench Bare-Metal Amiga Kernel
 * Custom Chip Register Helpers
 *
 * Hardware register helpers, Copper macros, display mode constants,
 * and color helpers for OCS/ECS/AGA chipsets.
 */

#ifndef NEOBENCH_CUSTOM_H
#define NEOBENCH_CUSTOM_H

#include "../include/neobench.h"
#include "../include/types.h"

/* ========================================================================
 * Custom Chip Register Offsets
 * ======================================================================== */

#define CUSTOM_BASE         0xDFF000
#define BLTCON0             0x040
#define BLTCON1             0x042
#define BLTAFWM             0x044
#define BLTALWM             0x046
#define BLTCPTH             0x048
#define BLTCPTL             0x04A
#define BLTBPTH             0x04C
#define BLTBPTL             0x04E
#define BLTAPTH             0x050
#define BLTAPTL             0x052
#define BLTDPTH             0x054
#define BLTDPTL             0x056
#define BLTSIZE             0x058
#define BLTCON0L            0x05A
#define BLTSIZV             0x05C
#define BLTSIZH             0x05E
#define BLTCMOD             0x060
#define BLTBMOD             0x062
#define BLTAMOD             0x064
#define BLTDMOD             0x066
#define BLTCDAT             0x070
#define BLTBDAT             0x072
#define BLTADAT             0x074
#define COP1LCH             0x080
#define COP1LCL             0x082
#define COP2LCH             0x084
#define COP2LCL             0x086
#define COPJMP1             0x088
#define COPJMP2             0x08A
#define COPINS              0x08C
#define DIWSTRT             0x08E
#define DIWSTOP             0x090
#define DDFSTRT             0x092
#define DDFSTOP             0x094
#define DMACON              0x096
#define CLXCON              0x098
#define INTENA              0x09A
#define INTREQ              0x09C
#define BPL1PTH             0x0E0
#define BPL1PTL             0x0E2
#define BPL2PTH             0x0E4
#define BPL2PTL             0x0E6
#define BPL3PTH             0x0E8
#define BPL3PTL             0x0EA
#define BPL4PTH             0x0EC
#define BPL4PTL             0x0EE
#define BPL5PTH             0x0F0
#define BPL5PTL             0x0F2
#define BPL6PTH             0x0F4
#define BPL6PTL             0x0F6
#define BPL7PTH             0x0F8
#define BPL7PTL             0x0FA
#define BPL8PTH             0x0FC
#define BPL8PTL             0x0FE
#define BPLCON0             0x100
#define BPLCON1             0x102
#define BPLCON2             0x104
#define BPLCON3             0x106
#define BPLCON4             0x10C
#define BPL1MOD             0x108
#define BPL2MOD             0x10A
#define BPL1DAT             0x110
#define BPL2DAT             0x112
#define BPL3DAT             0x114
#define BPL4DAT             0x116
#define BPL5DAT             0x118
#define BPL6DAT             0x11A
#define BPL7DAT             0x11C
#define BPL8DAT             0x11E
#define SPR0PTH             0x120
#define SPR0PTL             0x122
#define SPR1PTH             0x124
#define SPR1PTL             0x126
#define SPR2PTH             0x128
#define SPR2PTL             0x12A
#define SPR3PTH             0x12C
#define SPR3PTL             0x12E
#define SPR4PTH             0x130
#define SPR4PTL             0x132
#define SPR5PTH             0x134
#define SPR5PTL             0x136
#define SPR6PTH             0x138
#define SPR6PTL             0x13A
#define SPR7PTH             0x13C
#define SPR7PTL             0x13E
#define COLOR00             0x180
#define SPR0POS             0x140
#define SPR0CTL             0x142
#define SPR0DATA            0x144
#define SPR0DATB            0x146
#define AUD0LCH             0x0A0
#define AUD0LCL             0x0A2
#define AUD0LEN             0x0A4
#define AUD0PER             0x0A6
#define AUD0VOL             0x0A8
#define AUD0DAT             0x0AA
#define DSKPTH              0x020
#define DSKPTL              0x022
#define DSKLEN              0x024
#define DSKDAT              0x026
#define DSKSYNC             0x07E
#define SERDAT              0x030
#define SERDATR             0x018
#define SERPER              0x032
#define VPOSR               0x004
#define VHPOSR              0x006
#define DMACONR             0x002
#define INTENAR             0x01C
#define INTREQR             0x01E
#define ADKCONR             0x010
#define DENISEID            0x07C
#define JOY0DAT             0x00A
#define JOY1DAT             0x00C
#define POTGOR              0x016
#define FMODE               0x1FC
#define HTOTAL              0x1C0
#define HSSTOP              0x1C2
#define HBSTRT              0x1C4
#define HBSTOP              0x1C6
#define VTOTAL              0x1C8
#define VSSTOP              0x1CA
#define VBSTRT              0x1CC
#define VBSTOP              0x1CE
#define BEAMCON0            0x1DC
#define HSSTRT              0x1DE
#define VSSTRT              0x1E0
#define HCENTER             0x1E2
#define DIWHIGH             0x1E4
#define ADKCON              0x09E

/* DMA Control Bits */
#define DMAF_SETCLR         0x8000
#define DMAF_AUD0           0x0001
#define DMAF_AUDIO          0x000F
#define DMAF_DISK           0x0010
#define DMAF_SPRITE         0x0020
#define DMAF_BLITTER        0x0040
#define DMAF_COPPER         0x0080
#define DMAF_RASTER         0x0100
#define DMAF_MASTER         0x0200
#define DMAF_BLITHOG        0x0400
#define DMAF_ALL            0x03FF

/* Interrupt Bits */
#define INTF_SETCLR         0x8000
#define INTF_TBE            0x0001
#define INTF_DSKBLK         0x0002
#define INTF_SOFTINT        0x0004
#define INTF_PORTS          0x0008
#define INTF_COPER          0x0010
#define INTF_VERTB          0x0020
#define INTF_BLIT           0x0040
#define INTF_AUD0           0x0080
#define INTF_RBF            0x0800
#define INTF_DSKSYN         0x1000
#define INTF_EXTER          0x2000
#define INTF_INTEN          0x4000

/* Chipset Detection */
typedef enum { CHIPSET_OCS = 0, CHIPSET_ECS = 1, CHIPSET_AGA = 2 } ChipsetType;

static inline void custom_write(uint16_t reg, uint16_t value) {
    *((volatile uint16_t *)(CUSTOM_BASE + reg)) = value;
}

static inline uint16_t custom_read(uint16_t reg) {
    return *((volatile uint16_t *)(CUSTOM_BASE + reg));
}

#endif /* NEOBENCH_CUSTOM_H */
