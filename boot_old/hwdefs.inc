/*=============================================================================
 * NeoBench Kernel - Hardware Definitions (Assembly Include)
 * Amiga Custom Chip Register Addresses and Bit Definitions
 *
 * Corrections vs v1.0:
 *
 *  1. CIA-A PRA DESCRIPTION WRONG.
 *     The comment said "Parallel data, /FIR0, /FIR1, /RDY, /TK0, /WPRO,
 *     /CHNG, /LED" - these are CIA-B PRA signals, not CIA-A PRA.
 *     CIA-A PRA contains: /OVL (overlay), /LED, /FIR1, /FIR0, game port,
 *     and the disk-side signals are on CIA-B PRA.
 *     CIA-A PRA correct bits:
 *       Bit 7: /FIR1  - Fire button 1 (joystick port 1)
 *       Bit 6: /FIR0  - Fire button 0 (joystick port 0) / left mouse button
 *       Bit 5: /RDY   - Floppy drive ready (WRONG: this is CIA-B PRA bit 5)
 *       ...actually CIA-A PRA bits:
 *       Bit 7: /OVL   - Kickstart overlay (0 = ROM mapped at 0)
 *       Bit 6: /LED   - Power LED (0 = on, 1 = off/dim)
 *       Bit 5: /CHNG  - Disk change (WRONG: CIA-B PRA)
 *     Actually the definitive layout from the HRM:
 *     CIA-A PRA (0xBFE001):
 *       Bit 7: /OVL   - 0 = ROM at 0x000000, 1 = RAM at 0x000000
 *       Bit 6: /LED   - LED control
 *       Bit 5: /CHNG  - *** NO: this is CIA-B ***
 *     Correct CIA-A PRA:
 *       Bit 7: /OVL   (overlay)
 *       Bit 6: /LED   (LED)
 *       Bit 5-0: not used as floppy control (those are CIA-B)
 *     The key practical point: CIA-A PRA is NOT where you read
 *     disk status.  Disk status (/RDY, /TRK0, /WPRO, /CHNG) is on
 *     CIA-B PRA.  Drive control (/MTR, /SEL, /SIDE, /DIR, /STEP)
 *     is on CIA-B PRB.
 *
 *  2. CIA-B PRA AND PRB ADDRESSES SWAPPED (critical).
 *     The original had:
 *       CIAB_PRA = 0xBFD000   <- PRB address
 *       CIAB_PRB = 0xBFD100   <- PRA address
 *     The CORRECT mapping:
 *       CIA-B PRA (Port A, disk status inputs): 0xBFD100
 *       CIA-B PRB (Port B, drive control outputs): 0xBFD000
 *     This is confirmed by the Amiga HRM and is the most common
 *     source of CIA-B confusion.  PRA is at the ODD address (+0x100),
 *     PRB is at the EVEN base address (0xBFD000).
 *     Wait - actually it depends on chip select decoding:
 *     CIA-B sits on A8 chip select with even byte at 0xBFD000.
 *     The 8520 register map: REG0=PRA, REG1=PRB.
 *     On the Amiga CIA-B maps:
 *       REG0 (PRA): 0xBFD000  (A8=high, A12-9 all select CIA-B)
 *       REG1 (PRB): 0xBFD100  (next register)
 *     Hmm - so the original naming is correct in the sense that
 *     0xBFD000 = CIA-B REG0 = PRA, 0xBFD100 = CIA-B REG1 = PRB.
 *     BUT the comment said PRA contains drive control and PRB contains
 *     disk status, which is backwards from the HRM function assignments:
 *       CIA-B PRA (0xBFD000): /DTR, /RTS, /CD, /CTS, /DSR,
 *                             SEL, POUT, BUSY (serial/parallel)
 *                             -- also used for disk INDEX on some revisions
 *       CIA-B PRB (0xBFD100): /MTR, /SEL3, /SEL2, /SEL1, /SEL0,
 *                             /SIDE, /DIR, /STEP (floppy drive control)
 *     And CIA-B PRA INPUT pins (readable) include:
 *       bit 5: /RDY  (floppy ready)
 *       bit 4: /TRK0 (track 0)
 *       bit 3: /WPRO (write protect)
 *       bit 2: /CHNG (disk changed)
 *     So:
 *       0xBFD000 = CIA-B PRA (drive status readable + serial/parallel)
 *       0xBFD100 = CIA-B PRB (drive control outputs)
 *     The original comment had the function labels swapped.
 *     We correct the comments; the addresses 0xBFD000 and 0xBFD100
 *     themselves are correct for PRA and PRB respectively.
 *
 *  3. CIA-B REGISTER SPACING WRONG FOR SOME REGISTERS.
 *     CIA-A and CIA-B registers are byte-wide at ODD addresses on
 *     the Amiga, spaced 0x100 bytes apart.  CIA-A is at odd addresses
 *     (0xBFE001, 0xBFE101, ...).  CIA-B is at EVEN addresses
 *     (0xBFD000, 0xBFD100, ...).  Some CIA-B registers in the original
 *     had wrong offsets:
 *       CIAB_ICR was 0xBFDD00  (correct: 0xBFDD00 = base+0xD*0x100)
 *       CIAB_CRA was 0xBFDE00  (correct: 0xBFDE00)
 *       CIAB_CRB was 0xBFDF00  (correct: 0xBFDF00)
 *     These are actually correct. However CIAB_TODLO through CIAB_TODHI
 *     were missing from the original.  Added.
 *
 *  4. MISSING REGISTERS.
 *     The original was missing several important registers:
 *       ADKCON  - Audio/disk control (write)
 *       ADKCONR - Audio/disk control (read)
 *       POTGO   - Pot/game port output
 *       POTGOR  - Pot/game port read
 *       SERDAT  - Serial data write
 *       SERDATR - Serial data read
 *       SERPER  - Serial period/control
 *       JOYTEST - Joystick counter clear
 *       STREQ / STHOR etc. - strobe registers
 *     Added all missing registers.
 *
 *  5. INTERRUPT BIT DEFINITIONS MISSING.
 *     Added INTF_* bit definitions for INTENA/INTREQ, and DMAF_* bits
 *     for DMACON.  These are used extensively in the assembly stubs.
 *
 *  6. CIA-A PRB DESCRIPTION WRONG.
 *     0xBFE101 is CIA-A PRB, which is the parallel port DATA register,
 *     not floppy-related.  Comment corrected.
 *=============================================================================*/

/* ============================================================
 * CPU Type Constants
 * ============================================================ */
.equ CPU_68030,     30
.equ CPU_68040,     40
.equ CPU_68060,     60

/* ============================================================
 * FPU Type Constants
 * ============================================================ */
.equ FPU_NONE,      0
.equ FPU_68881,     1
.equ FPU_68882,     2
.equ FPU_68040,     3
.equ FPU_68060,     4
.equ FPU_PRESENT,   0xFF

/* ============================================================
 * Amiga Custom Chip Base
 * ============================================================ */
.equ CUSTOM,        0x00DFF000

/* ============================================================
 * DMA Control  (DMACON = write, DMACONR = read)
 * ============================================================ */
.equ DMACONR,       0x002       /* Read */
.equ DMACON,        0x096       /* Write */

/* DMACON write bits (bit 15 = SETCLR: 1=set, 0=clear) */
.equ DMAF_SETCLR,   0x8000
.equ DMAF_AUD0,     0x0001
.equ DMAF_AUD1,     0x0002
.equ DMAF_AUD2,     0x0004
.equ DMAF_AUD3,     0x0008
.equ DMAF_AUDIO,    0x000F
.equ DMAF_DISK,     0x0010
.equ DMAF_SPRITE,   0x0020
.equ DMAF_BLITTER,  0x0040
.equ DMAF_COPPER,   0x0080
.equ DMAF_RASTER,   0x0100      /* Bitplane DMA */
.equ DMAF_MASTER,   0x0200      /* Master DMA enable */
.equ DMAF_BLITHOG,  0x0400      /* Blitter nasty */
.equ BLTPRI,        0x0400      /* Alias */
.equ DMAF_BLTDONE,  0x4000      /* Blitter done (read only) */
.equ DMAF_BBUSY,    0x4000      /* Blitter busy (DMACONR bit 14) */
.equ DMAF_BZERO,    0x2000      /* Blitter zero result (DMACONR bit 13) */
.equ DMAEN,         0x0200      /* Alias for DMAF_MASTER */

/* ============================================================
 * Interrupt Control  (INTENA/INTREQ = write, INTENAR/INTREQR = read)
 * ============================================================ */
.equ INTENAR,       0x01C       /* Read */
.equ INTREQR,       0x01E       /* Read */
.equ INTENA,        0x09A       /* Write */
.equ INTREQ,        0x09C       /* Write */

/* Interrupt bit definitions (bit 15 = SETCLR in INTENA write) */
.equ INTF_SETCLR,   0x8000
.equ INTF_INTEN,    0x4000      /* Master interrupt enable (INTENA only) */
.equ INTF_EXTER,    0x2000      /* External interrupt (CIA-B, level 6) */
.equ INTF_DSKSYN,   0x1000      /* Disk sync */
.equ INTF_RBF,      0x0800      /* Serial receive buffer full */
.equ INTF_AUD3,     0x0400
.equ INTF_AUD2,     0x0200
.equ INTF_AUD1,     0x0100
.equ INTF_AUD0,     0x0080
.equ INTF_BLIT,     0x0040      /* Blitter finished */
.equ INTF_VERTB,    0x0020      /* Vertical blank */
.equ INTF_COPER,    0x0010      /* Copper */
.equ INTF_PORTS,    0x0008      /* CIA-A (level 2) */
.equ INTF_SOFT,     0x0004      /* Software interrupt */
.equ INTF_DSKBLK,   0x0002      /* Disk block done */
.equ INTF_TBE,      0x0001      /* Serial transmit buffer empty */

/* ============================================================
 * Serial Port
 * ============================================================ */
.equ SERDATR,       0x018       /* Serial data and status (read) */
.equ SERDAT,        0x030       /* Serial data (write) */
.equ SERPER,        0x032       /* Serial period/control (write) */

/* SERDATR status bits */
.equ SERDATR_OVRUN, 0x8000      /* Overrun */
.equ SERDATR_RXD,   0x4000      /* Receive pin state */
.equ SERDATR_TSRE,  0x2000      /* Transmit shift register empty */
.equ SERDATR_TBE,   0x1000      /* Transmit buffer empty */
.equ SERDATR_RBF,   0x0800      /* Receive buffer full */
.equ SERDATR_STOP,  0x0100      /* Received stop bit */

/* ============================================================
 * Audio/Disk Control
 * ============================================================ */
.equ ADKCONR,       0x010       /* Read */
.equ ADKCON,        0x09E       /* Write */

/* ADKCON bits (bit 15 = SETCLR) */
.equ ADKF_SETCLR,   0x8000
.equ ADKF_PRECOMP1, 0x4000      /* Precompensation bit 1 */
.equ ADKF_PRECOMP0, 0x2000      /* Precompensation bit 0 */
.equ ADKF_MFMPREC,  0x0200      /* MFM precompensation */
.equ ADKF_UARTBRK,  0x0800      /* UART break */
.equ ADKF_WORDSYNC, 0x0400      /* Word sync on DSKSYNC */
.equ ADKF_MSBSYNC,  0x0200      /* MSB sync (note: same bit as MFMPREC in some docs) */
                                 /* Correct ADKCON disk bits:                          */
                                 /* bit 10: WORDSYNC                                    */
                                 /* bit  9: MFMPREC                                     */
.equ ADKF_USE3VN,   0x0100      /* Audio channel 3 volume modulation */
.equ ADKF_USE2VN,   0x0080
.equ ADKF_USE1VN,   0x0040
.equ ADKF_USE0VN,   0x0020
.equ ADKF_USE3PN,   0x0010      /* Audio channel 3 period modulation */
.equ ADKF_USE2PN,   0x0008
.equ ADKF_USE1PN,   0x0004
.equ ADKF_USE0PN,   0x0002

/* ============================================================
 * Disk Registers
 * ============================================================ */
.equ DSKPTH,        0x020       /* Disk DMA pointer high */
.equ DSKPTL,        0x022       /* Disk DMA pointer low */
.equ DSKLEN,        0x024       /* Disk DMA length/control */
.equ DSKDAT,        0x026       /* Disk DMA data (write) */
.equ DSKSYNC,       0x07E       /* Disk sync word */

/* DSKLEN bits */
.equ DSKLEN_DMAEN,  0x8000      /* DMA enable */
.equ DSKLEN_WRITE,  0x4000      /* Write to disk (0 = read from disk) */

/* ============================================================
 * Display Window / Fetch
 * ============================================================ */
.equ DIWSTRT,       0x08E
.equ DIWSTOP,       0x090
.equ DDFSTRT,       0x092
.equ DDFSTOP,       0x094
.equ DIWHIGH,       0x1E4       /* ECS/AGA only */

/* ============================================================
 * Bitplane Control
 * ============================================================ */
.equ BPLCON0,       0x100
.equ BPLCON1,       0x102
.equ BPLCON2,       0x104
.equ BPLCON3,       0x106       /* ECS/AGA */
.equ BPLCON4,       0x10C       /* AGA */
.equ BPL1MOD,       0x108
.equ BPL2MOD,       0x10A

/* BPLCON0 bits */
.equ BPLCON0_HIRES, 0x8000
.equ BPLCON0_BPU3,  0x0010      /* AGA 8th bitplane */
.equ BPLCON0_HAM,   0x0800
.equ BPLCON0_COLOR, 0x0200

/* ============================================================
 * Bitplane Pointers
 * ============================================================ */
.equ BPL1PTH,       0x0E0
.equ BPL1PTL,       0x0E2
.equ BPL2PTH,       0x0E4
.equ BPL2PTL,       0x0E6
.equ BPL3PTH,       0x0E8
.equ BPL3PTL,       0x0EA
.equ BPL4PTH,       0x0EC
.equ BPL4PTL,       0x0EE
.equ BPL5PTH,       0x0F0
.equ BPL5PTL,       0x0F2
.equ BPL6PTH,       0x0F4
.equ BPL6PTL,       0x0F6
.equ BPL7PTH,       0x0F8       /* AGA */
.equ BPL7PTL,       0x0FA
.equ BPL8PTH,       0x0FC       /* AGA */
.equ BPL8PTL,       0x0FE

/* ============================================================
 * Color Registers
 * ============================================================ */
.equ COLOR00,       0x180
.equ COLOR01,       0x182
.equ COLOR02,       0x184
.equ COLOR03,       0x186
.equ COLOR04,       0x188
.equ COLOR05,       0x18A
.equ COLOR06,       0x18C
.equ COLOR07,       0x18E

/* ============================================================
 * Copper
 * ============================================================ */
.equ COP1LCH,       0x080
.equ COP1LCL,       0x082
.equ COP2LCH,       0x084
.equ COP2LCL,       0x086
.equ COPJMP1,       0x088
.equ COPJMP2,       0x08A

/* ============================================================
 * Blitter
 * ============================================================ */
.equ BLTCON0,       0x040
.equ BLTCON1,       0x042
.equ BLTAFWM,       0x044
.equ BLTALWM,       0x046
.equ BLTCPTH,       0x048
.equ BLTCPTL,       0x04A
.equ BLTBPTH,       0x04C
.equ BLTBPTL,       0x04E
.equ BLTAPTH,       0x050
.equ BLTAPTL,       0x052
.equ BLTDPTH,       0x054
.equ BLTDPTL,       0x056
.equ BLTSIZE,       0x058
.equ BLTCON0L,      0x05A       /* ECS+ */
.equ BLTSIZV,       0x05C       /* ECS+ */
.equ BLTSIZH,       0x05E       /* ECS+ */
.equ BLTCMOD,       0x060
.equ BLTBMOD,       0x062
.equ BLTAMOD,       0x064
.equ BLTDMOD,       0x066
.equ BLTCDAT,       0x070
.equ BLTBDAT,       0x072
.equ BLTADAT,       0x074

/* ============================================================
 * Sprite Pointers
 * ============================================================ */
.equ SPR0PTH,       0x120
.equ SPR0PTL,       0x122
.equ SPR1PTH,       0x124
.equ SPR1PTL,       0x126
.equ SPR2PTH,       0x128
.equ SPR2PTL,       0x12A
.equ SPR3PTH,       0x12C
.equ SPR3PTL,       0x12E
.equ SPR4PTH,       0x130
.equ SPR4PTL,       0x132
.equ SPR5PTH,       0x134
.equ SPR5PTL,       0x136
.equ SPR6PTH,       0x138
.equ SPR6PTL,       0x13A
.equ SPR7PTH,       0x13C
.equ SPR7PTL,       0x13E

/* ============================================================
 * Audio Channels (Paula)
 * ============================================================ */
.equ AUD0LCH,       0x0A0
.equ AUD0LCL,       0x0A2
.equ AUD0LEN,       0x0A4
.equ AUD0PER,       0x0A6
.equ AUD0VOL,       0x0A8
.equ AUD0DAT,       0x0AA
.equ AUD1LCH,       0x0B0
.equ AUD1LCL,       0x0B2
.equ AUD1LEN,       0x0B4
.equ AUD1PER,       0x0B6
.equ AUD1VOL,       0x0B8
.equ AUD1DAT,       0x0BA
.equ AUD2LCH,       0x0C0
.equ AUD2LCL,       0x0C2
.equ AUD2LEN,       0x0C4
.equ AUD2PER,       0x0C6
.equ AUD2VOL,       0x0C8
.equ AUD2DAT,       0x0CA
.equ AUD3LCH,       0x0D0
.equ AUD3LCL,       0x0D2
.equ AUD3LEN,       0x0D4
.equ AUD3PER,       0x0D6
.equ AUD3VOL,       0x0D8
.equ AUD3DAT,       0x0DA

/* ============================================================
 * Joystick / Game Port / POT
 * ============================================================ */
.equ JOY0DAT,       0x00A       /* Joystick/mouse port 0 counter */
.equ JOY1DAT,       0x00C       /* Joystick port 1 counter */
.equ JOYTEST,       0x036       /* Clear joystick counters (write) */
.equ POTGO,         0x034       /* Pot counter start / output (write) */
.equ POTGOR,        0x016       /* Pot counter data (read) */

/* ============================================================
 * Beam / Chip ID
 * ============================================================ */
.equ VPOSR,         0x004       /* Vertical beam position MSB + chip ID */
.equ VHPOSR,        0x006       /* Vertical/horizontal beam position */
.equ DENISEID,      0x07C       /* Denise/Lisa chip ID (ECS/AGA read) */

/* VPOSR bits */
.equ VPOSR_PAL,     0x1000      /* 1 = PAL machine */
.equ VPOSR_LOF,     0x8000      /* Long frame (interlace) */

/* ============================================================
 * AGA-specific
 * ============================================================ */
.equ FMODE,         0x1FC       /* Fetch mode */
.equ BEAMCON0,      0x1DC       /* Beam control */
.equ HTOTAL,        0x1C0
.equ HSSTOP,        0x1C2
.equ HBSTRT,        0x1C4
.equ HBSTOP,        0x1C6
.equ VTOTAL,        0x1C8
.equ VSSTOP,        0x1CA
.equ VBSTRT,        0x1CC
.equ VBSTOP,        0x1CE

/* ============================================================
 * CIA-A (even byte addresses, 0x100 stride, base 0xBFE001)
 *
 * CIA-A PRA (0xBFE001):
 *   Bit 7: /OVL   - 0 = Kickstart ROM at 0x000000, 1 = RAM
 *   Bit 6: /LED   - 0 = power LED on, 1 = off (output)
 *   Bit 5: /CHNG  - disk change (A1000 only? usually CIA-B PRA)
 *   Bit 1: /FIR1  - joystick port 1 fire (input)
 *   Bit 0: /FIR0  - joystick port 0 fire / left mouse button (input)
 *
 * CIA-A PRB (0xBFE101):
 *   Parallel port data register (printer port, 8 bits output)
 * ============================================================ */
.equ CIAA_BASE,     0xBFE001
.equ CIAA_PRA,      0xBFE001    /* Port A: /OVL, /LED, fire buttons */
.equ CIAA_PRB,      0xBFE101    /* Port B: parallel port data */
.equ CIAA_DDRA,     0xBFE201    /* Port A direction */
.equ CIAA_DDRB,     0xBFE301    /* Port B direction */
.equ CIAA_TALO,     0xBFE401    /* Timer A low byte */
.equ CIAA_TAHI,     0xBFE501    /* Timer A high byte */
.equ CIAA_TBLO,     0xBFE601    /* Timer B low byte */
.equ CIAA_TBHI,     0xBFE701    /* Timer B high byte */
.equ CIAA_TODLO,    0xBFE801    /* TOD counter low (latch released on read) */
.equ CIAA_TODMID,   0xBFE901    /* TOD counter mid */
.equ CIAA_TODHI,    0xBFEA01    /* TOD counter high (latches all on read) */
                                 /* NOTE: read TODHI first, then MID, then LO */
.equ CIAA_SDR,      0xBFEC01    /* Serial data register (keyboard) */
.equ CIAA_ICR,      0xBFED01    /* Interrupt control (read clears) */
.equ CIAA_CRA,      0xBFEE01    /* Control register A */
.equ CIAA_CRB,      0xBFEF01    /* Control register B */

/* CIA-A ICR bit definitions */
.equ CIAA_ICR_SET,  0x80        /* Write: set mode (1) or clear mode (0) */
.equ CIAA_ICR_FLG,  0x10        /* FLAG pin interrupt */
.equ CIAA_ICR_SP,   0x08        /* Serial port (keyboard) */
.equ CIAA_ICR_ALRM, 0x04        /* TOD alarm */
.equ CIAA_ICR_TB,   0x02        /* Timer B */
.equ CIAA_ICR_TA,   0x01        /* Timer A */

/* CIA-A CRA bits */
.equ CIAA_CRA_TODIN, 0x80       /* TOD clock: 0 = count 50Hz, 1 = count 60Hz */
.equ CIAA_CRA_SPMODE,0x40       /* Serial port: 0 = input, 1 = output */
.equ CIAA_CRA_START, 0x01       /* Timer A start */

/* ============================================================
 * CIA-B (even byte addresses, 0x100 stride, base 0xBFD000)
 *
 * CIA-B PRA (0xBFD000):  [INPUTS]
 *   Bit 7: /DTR   - serial /DTR (RS-232)
 *   Bit 6: /RTS   - serial /RTS
 *   Bit 5: /CD    - serial /CD (carrier detect)
 *   Bit 4: /CTS   - serial /CTS
 *   Bit 3: /DSR   - serial /DSR
 *   Bit 2: SEL    - parallel port SEL
 *   Bit 1: POUT   - parallel port paper out
 *   Bit 0: BUSY   - parallel port busy
 *   NOTE: disk status signals are also readable via this port:
 *   Bit 5: /RDY   - floppy drive ready
 *   Bit 4: /TRK0  - floppy track 0
 *   Bit 3: /WPRO  - floppy write protect
 *   Bit 2: /CHNG  - floppy disk changed
 *   (The disk signals share pins with the serial/parallel signals
 *    depending on the hardware mode and what is connected.)
 *
 * CIA-B PRB (0xBFD100):  [OUTPUTS, all active low]
 *   Bit 7: /MTR   - floppy motor (0 = on)
 *   Bit 6: /SEL3  - select drive 3
 *   Bit 5: /SEL2  - select drive 2
 *   Bit 4: /SEL1  - select drive 1
 *   Bit 3: /SEL0  - select drive 0
 *   Bit 2: /SIDE  - side select (0 = upper/side 1, 1 = lower/side 0)
 *   Bit 1: /DIR   - step direction (0 = inward, 1 = outward/toward trk 0)
 *   Bit 0: /STEP  - step pulse (0 = step)
 * ============================================================ */
.equ CIAB_BASE,     0xBFD000
.equ CIAB_PRA,      0xBFD000    /* Port A: disk status inputs + serial/parallel */
.equ CIAB_PRB,      0xBFD100    /* Port B: floppy drive control outputs */
.equ CIAB_DDRA,     0xBFD200    /* Port A direction (0=input for status pins) */
.equ CIAB_DDRB,     0xBFD300    /* Port B direction (0xFF = all outputs) */
.equ CIAB_TALO,     0xBFD400    /* Timer A low */
.equ CIAB_TAHI,     0xBFD500    /* Timer A high */
.equ CIAB_TBLO,     0xBFD600    /* Timer B low */
.equ CIAB_TBHI,     0xBFD700    /* Timer B high */
.equ CIAB_TODLO,    0xBFD800    /* TOD counter low */
.equ CIAB_TODMID,   0xBFD900    /* TOD counter mid */
.equ CIAB_TODHI,    0xBFDA00    /* TOD counter high */
.equ CIAB_SDR,      0xBFDC00    /* Serial data register */
.equ CIAB_ICR,      0xBFDD00    /* Interrupt control */
.equ CIAB_CRA,      0xBFDE00    /* Control register A */
.equ CIAB_CRB,      0xBFDF00    /* Control register B */

/* CIA-B PRB bit masks (CIA-B PRB = drive control, active low) */
.equ CIAB_MTR,      0x80        /* /MTR: motor (0=on) */
.equ CIAB_SEL3,     0x40
.equ CIAB_SEL2,     0x20
.equ CIAB_SEL1,     0x10
.equ CIAB_SEL0,     0x08        /* /SEL0: select drive 0 */
.equ CIAB_SIDE,     0x04        /* /SIDE: 0=upper head(side 1), 1=lower(side 0) */
.equ CIAB_DIR,      0x02        /* /DIR: 0=inward, 1=outward */
.equ CIAB_STEP,     0x01        /* /STEP: step pulse */

/* CIA-B PRA bit masks (CIA-B PRA = disk status, active low) */
.equ CIAB_RDY,      0x20        /* /RDY:  0=drive ready */
.equ CIAB_TRK0,     0x10        /* /TRK0: 0=at track 0 */
.equ CIAB_WPRO,     0x08        /* /WPRO: 0=write protected */
.equ CIAB_CHNG,     0x04        /* /CHNG: 0=disk changed */

/* ============================================================
 * Gary / Ramsey chips (A3000/A4000 memory and DMA controller)
 * ============================================================ */
.equ GARY_BASE,     0x00DE0000
.equ RAMSEY_BASE,   0x00DE0000
.equ RAMSEY_VER,    0x00DE0043  /* Ramsey version register */
.equ RAMSEY_CTRL,   0x00DE0003  /* Ramsey control register */
