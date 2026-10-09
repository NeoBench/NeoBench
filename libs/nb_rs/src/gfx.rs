/*
 * The Vista chrome -- phase 3, the desktop end of the move.
 *
 * wallpaper(), the bar's glass and the start orb are what the boot
 * leaves on screen before anything else draws: the wash with the
 * aurora laid across it, the mark lit against the night, the wordmark
 * under it, the pane of tinted glass along the bottom edge and the
 * lit blue sphere at its left end.  All of it was C in
 * user/gui/desktop/main.c; all of it is this file now, drawn through
 * the same gfx primitives the rest of the desktop still calls.
 *
 * Phase 1 and phase 2 held Rust against C.  Phase 3 inverts that: the
 * Rust is what paints, and the C is what holds it to account.  Three
 * parties stand over the numbers this file draws with:
 *
 *   - the boot battery (nb_rs_gfx_check, called from
 *     kernel/init/kernel_main.c after the store answers).  The kernel
 *     passes this half the Config's own glass and the backdrop's two
 *     colours -- values no compiler has seen before boot -- beside
 *     its own reference for every table and every formula, and reads
 *     back a mask saying where the two came apart.  One bit a family:
 *
 *       0 bd_dark          1 the bar's row alphas
 *       2 the glow ladder  3 the aurora / mark / wordmark
 *       4 the palette      5 the orb, three states
 *      31 the call itself was unusable
 *
 *   - tools/tests/test_gfx on the host, which sweeps the formulas
 *     exhaustively -- every 16-bit colour through bd_dark, the row
 *     alphas over ranges no boot would stand on -- beside its own
 *     copy of the reference, and then falsifies the reference one
 *     family at a time to prove the mask names what moved.
 *
 *   - the screen itself: this is paint, so the boot is the oracle
 *     the same way it is for everything else, and a screenshot of the
 *     scene before and after the port is the comparison that counts.
 *
 * The drawing half is cfg'd to the m68k: it calls the C primitives
 * (gfx_vgrad, gfx_alpha, gfx_disc, gfx_text_s, logo_mark) through
 * extern, and on a host those symbols do not exist.  The formulas and
 * the tables are compiled on both sides so both batteries can reach
 * them.
 *
 * The safety of the reads in nb_rs_gfx_check is this side's contract,
 * the same one store.rs keeps: every pointer is one of the kernel's
 * own static arrays and every length is checked against the table it
 * is supposed to match before a word is read, so a wrong pair fails
 * with bit 31 rather than walking off into the ROM.
 */

/* 5-6-5 packed colour, the way gfx.h's NB_RGB packs it: r 5, g 6, b 5. */
const fn rgb(r: u16, g: u16, b: u16) -> u16 {
    ((r & 31) << 11) | ((g & 63) << 5) | (b & 31)
}

/* ------------------------------------------------------------------ *
 * The palette -- every colour this file is allowed to name.
 *
 * Indices are the protocol: pal_ref[] in kernel_main.c and in
 * tools/tests/test_gfx.c lists NB_RGB() in exactly this order, so a
 * colour retyped on either side fails bit 4 with the index that
 * moved.  The C half keeps its own #defines for the chrome that did
 * not move (the menu, the window captions); these two lists are
 * deliberately the same numbers.
 * ------------------------------------------------------------------ */
pub const P_AERO: usize = 0; /* the glass itself            */
pub const P_AERO_RIM: usize = 1; /* the rim and the button edge */
pub const P_AERO_BTN: usize = 2; /* a pinned launcher's field   */
pub const P_AERO_ACT: usize = 3; /* the task button that is up  */
pub const P_INK: usize = 4; /* white                       */
pub const P_SHADOW: usize = 5; /* drop shadow                 */
pub const P_ACC: usize = 6; /* brand teal                  */
pub const P_ORB_T: usize = 7; /* the orb's lit top           */
pub const P_ORB_B: usize = 8; /* the orb's body blue         */
pub const P_ORB_S: usize = 9; /* the shade at its foot       */
pub const P_GLOW_A: usize = 10; /* mint horizon glow           */
pub const P_GLOW_B: usize = 11; /* sky glow                    */
pub const P_GLOW_C: usize = 12; /* aqua glow                   */
pub const P_GRID: usize = 13; /* hairline grid               */
pub const P_AUR_A: usize = 14; /* the ribbon's blue           */
pub const P_AUR_B: usize = 15; /* its pale ice                */
pub const P_AUR_C: usize = 16; /* the brighter blue           */
pub const P_GRID_L: usize = 17; /* the hairline, lit           */
pub const P_WB_SHADE: usize = 18; /* Workbench's shaded bevel    */
pub const P_WB_GREY: usize = 19; /* Workbench's panel body      */
pub const P_LOGO_BG: usize = 20; /* the pearl the mark is cut in */
pub const P_LOGO_TEAL: usize = 21; /* the wordmark's shadow       */
pub const P_LOGO_NAVY: usize = 22; /* the wordmark on a pale field */

const PAL: [u16; 23] = [
    rgb(1, 3, 6),      /*  0 AERO     */
    rgb(11, 26, 15),   /*  1 AERO_RIM */
    rgb(7, 16, 12),    /*  2 AERO_BTN */
    rgb(11, 25, 17),   /*  3 AERO_ACT */
    rgb(31, 63, 31),   /*  4 INK      */
    rgb(0, 1, 1),      /*  5 SHADOW   */
    rgb(6, 41, 22),    /*  6 ACC      */
    rgb(9, 44, 31),    /*  7 ORB_T    */
    rgb(1, 20, 30),    /*  8 ORB_B    */
    rgb(0, 8, 16),     /*  9 ORB_S    */
    rgb(19, 53, 25),   /* 10 GLOW_A   */
    rgb(20, 53, 28),   /* 11 GLOW_B   */
    rgb(19, 56, 25),   /* 12 GLOW_C   */
    rgb(21, 58, 28),   /* 13 GRID     */
    rgb(8, 44, 30),    /* 14 AUR_A    */
    rgb(20, 58, 31),   /* 15 AUR_B    */
    rgb(14, 52, 31),   /* 16 AUR_C    */
    rgb(16, 44, 30),   /* 17 GRID_L   */
    rgb(11, 21, 11),   /* 18 WB_SHADE */
    rgb(21, 42, 21),   /* 19 WB_GREY  */
    rgb(30, 61, 30),   /* 20 LOGO_BG  */
    rgb(6, 41, 22),    /* 21 LOGO_TEAL*/
    rgb(2, 15, 10),    /* 22 LOGO_NAVY*/
];

/* ------------------------------------------------------------------ *
 * Colour slots.  The aurora cannot carry literals: whether the ribbon
 * wears the aurora's blues or the older glows' greens is asked of the
 * top colour (bd_dark below), because wash is not a fixed pair --
 * Config/screen.cfg carries its two colours.  So each paint entry
 * names a slot, and resolve() picks the colour at draw time.  The
 * battery compares slots, which keeps the reference tables static;
 * bd_dark's own answer is held separately, so a wrong night-field
 * test still fails on bit 0 where it belongs.
 * ------------------------------------------------------------------ */
pub const SA: u8 = 0; /* dark ? aurora blue : mint glow  */
pub const SB: u8 = 1; /* dark ? pale ice   : sky glow    */
pub const SC: u8 = 2; /* dark ? bright blue: aqua glow   */
pub const SINK: u8 = 3; /* white, whatever the field      */
pub const SWORD: u8 = 4; /* wordmark: ink on night, navy on pale */
pub const STEAL: u8 = 5; /* the shadow under the wordmark  */
pub const SGRID: u8 = 6; /* grid: lit hairline or dark one */

pub const fn resolve(slot: u8, dark: bool) -> u16 {
    match slot {
        SA => {
            if dark {
                PAL[P_AUR_A]
            } else {
                PAL[P_GLOW_A]
            }
        }
        SB => {
            if dark {
                PAL[P_AUR_B]
            } else {
                PAL[P_GLOW_B]
            }
        }
        SC => {
            if dark {
                PAL[P_AUR_C]
            } else {
                PAL[P_GLOW_C]
            }
        }
        SWORD => {
            if dark {
                PAL[P_INK]
            } else {
                PAL[P_LOGO_NAVY]
            }
        }
        STEAL => PAL[P_LOGO_TEAL],
        SGRID => {
            if dark {
                PAL[P_GRID_L]
            } else {
                PAL[P_GRID]
            }
        }
        _ => PAL[P_INK],
    }
}

/* ------------------------------------------------------------------ *
 * The scene's paint, in drawing order.  kind says which pass owns the
 * entry and therefore which flags gate it:
 *
 *   0  the aurora -- five blooms end to end and the two white cores,
 *      drawn only when the backdrop is wash and glow is on;
 *   1  the older three glows, drawn when glow is on and wash is not;
 *   2  the bloom that lights the mark, drawn on a night field alone
 *      -- glow or no glow, because on a night field the mark cannot
 *      be inked and needs the light behind it;
 *   3  the branding: the mark itself, then the wordmark's teal
 *      shadow one pixel down-right, then the wordmark.  Always.
 *
 * word0 of the battery's view packs (kind<<30 | x<<20 | y<<10 | r),
 * word1 (slot<<8 | alpha); r carries the text's scale on kind 3.
 * ------------------------------------------------------------------ */
struct Paint {
    kind: u8,
    x: u16,
    y: u16,
    r: u16,
    slot: u8,
    alpha: u8,
}

const PAINTS: [Paint; 14] = [
    /* kind 0 -- the aurora: laid to cross the mark's top edge on its
     * way down the screen, and clear of the wordmark by the time it
     * reaches it */
    Paint { kind: 0, x: 40, y: 58, r: 150, slot: SA, alpha: 104 },
    Paint { kind: 0, x: 192, y: 138, r: 150, slot: SB, alpha: 112 },
    Paint { kind: 0, x: 342, y: 240, r: 150, slot: SC, alpha: 118 },
    Paint { kind: 0, x: 472, y: 356, r: 150, slot: SB, alpha: 112 },
    Paint { kind: 0, x: 604, y: 482, r: 150, slot: SA, alpha: 104 },
    Paint { kind: 0, x: 340, y: 238, r: 74, slot: SINK, alpha: 76 },
    Paint { kind: 0, x: 470, y: 354, r: 62, slot: SINK, alpha: 64 },
    /* kind 1 -- the older wash's three glows */
    Paint { kind: 1, x: 300, y: 452, r: 250, slot: SA, alpha: 165 },
    Paint { kind: 1, x: 556, y: 476, r: 210, slot: SB, alpha: 175 },
    Paint { kind: 1, x: 596, y: 58, r: 170, slot: SC, alpha: 185 },
    /* kind 2 -- the bloom behind the mark, night fields only */
    Paint { kind: 2, x: 215, y: 315, r: 165, slot: SB, alpha: 96 },
    /* kind 3 -- the branding; the mark's own colours are its
     * artwork's, so its word1 is zero, and the text rows carry their
     * scale in r */
    Paint { kind: 3, x: 110, y: 210, r: 210, slot: 0, alpha: 0 },
    Paint { kind: 3, x: 111, y: 429, r: 3, slot: STEAL, alpha: 0 },
    Paint { kind: 3, x: 110, y: 428, r: 3, slot: SWORD, alpha: 0 },
];

/* ------------------------------------------------------------------ *
 * The orb.  One table draws both launchers: the aero sphere with its
 * two-state pearl, and the Workbench bevel the classic bar keeps.
 * op says which primitive, when says which states the entry exists in
 * (always, menu open, menu closed, classic bar only), and the draw
 * order is the table order for every state -- filtering a state
 * preserves the order the C drew in.
 *
 * The orb's colours are literals rather than slots: none of them
 * turns with the field.  word0 packs
 * (op<<30 | when<<27 | x<<17 | y<<7 | r), word1 (colour<<8 | alpha),
 * with alpha zero meaning the opaque disc.
 * ------------------------------------------------------------------ */
const OP_DISC: u8 = 0;
const OP_DISC_A: u8 = 1;
const OP_LOGO: u8 = 2;

const W_ALWAYS: u8 = 0;
const W_OPEN: u8 = 1;
const W_CLOSED: u8 = 2;
const W_CLASSIC: u8 = 3;

struct Orb {
    op: u8,
    when: u8,
    x: u16,
    y: u16,
    r: u8,
    colour: u16,
    alpha: u8,
}

const ORB: [Orb; 17] = [
    /* the aero sphere: halo first when the menu asks for it, then
     * shade, rim, body, the lit top, the foot, the pearl, the mark */
    Orb { op: OP_DISC_A, when: W_OPEN, x: 28, y: 501, r: 15, colour: rgb(6, 41, 22), alpha: 70 },
    Orb { op: OP_DISC_A, when: W_OPEN, x: 28, y: 501, r: 12, colour: rgb(6, 41, 22), alpha: 96 },
    Orb { op: OP_DISC, when: W_ALWAYS, x: 29, y: 503, r: 11, colour: rgb(0, 1, 1), alpha: 0 },
    Orb { op: OP_DISC, when: W_ALWAYS, x: 28, y: 501, r: 11, colour: rgb(31, 63, 31), alpha: 0 },
    Orb { op: OP_DISC, when: W_ALWAYS, x: 28, y: 501, r: 10, colour: rgb(1, 20, 30), alpha: 0 },
    Orb { op: OP_DISC_A, when: W_CLOSED, x: 28, y: 498, r: 8, colour: rgb(9, 44, 31), alpha: 140 },
    Orb { op: OP_DISC_A, when: W_OPEN, x: 28, y: 498, r: 8, colour: rgb(9, 44, 31), alpha: 164 },
    Orb { op: OP_DISC_A, when: W_ALWAYS, x: 28, y: 505, r: 7, colour: rgb(0, 8, 16), alpha: 96 },
    Orb { op: OP_DISC, when: W_CLOSED, x: 28, y: 501, r: 6, colour: rgb(30, 61, 30), alpha: 0 },
    Orb { op: OP_DISC, when: W_OPEN, x: 28, y: 501, r: 6, colour: rgb(6, 41, 22), alpha: 0 },
    Orb { op: OP_LOGO, when: W_ALWAYS, x: 23, y: 496, r: 10, colour: 0, alpha: 0 },
    /* the Workbench bevel, classic bar only */
    Orb { op: OP_DISC, when: W_CLASSIC, x: 29, y: 503, r: 11, colour: rgb(11, 21, 11), alpha: 0 },
    Orb { op: OP_DISC, when: W_CLASSIC, x: 28, y: 501, r: 11, colour: rgb(31, 63, 31), alpha: 0 },
    Orb { op: OP_DISC, when: W_CLASSIC, x: 28, y: 501, r: 10, colour: rgb(21, 42, 21), alpha: 0 },
    Orb { op: OP_DISC_A, when: W_CLASSIC, x: 28, y: 498, r: 8, colour: rgb(31, 63, 31), alpha: 70 },
    Orb { op: OP_DISC, when: W_CLASSIC, x: 28, y: 501, r: 6, colour: rgb(30, 61, 30), alpha: 0 },
    Orb { op: OP_LOGO, when: W_CLASSIC, x: 22, y: 495, r: 12, colour: 0, alpha: 0 },
];

fn orb_applies(when: u8, aero: bool, open: bool) -> bool {
    match when {
        W_ALWAYS => true,
        W_OPEN => aero && open,
        W_CLOSED => aero && !open,
        _ => !aero, /* W_CLASSIC, and anything a bad table could hold */
    }
}

/* ------------------------------------------------------------------ *
 * The formulas the battery holds, each with the C it came from
 * spelled beside it (bd_dark and aero_field in main.c, glow()'s
 * ladder here).
 * ------------------------------------------------------------------ */

/// Is the field this artwork stands on a night one?  Asked of the
/// gradient's top colour, weights in the five-six-five the palette is
/// packed in, threshold well clear of both a night blue and a cream:
/// `(r * 10 + g * 10 + b * 4) < 600`.
pub fn bd_dark(c: u16) -> bool {
    let r = ((c >> 11) & 31) as u32;
    let g = ((c >> 5) & 63) as u32;
    let b = (c & 31) as u32;

    r * 10 + g * 10 + b * 4 < 600
}

/// The pane's base opacity for this Config's glass: `255 - glass * 2`
/// clamped into 40..255, the C's own clamps, as two min/max steps
/// (the note on prefs.rs's speed clamp vouches for that shape).
#[inline(never)]
pub fn aero_base(glass: u32) -> i32 {
    let op = 255i32.wrapping_sub((glass as i32).wrapping_mul(2));

    /* Clamped by returning rather than by merging a value back into
     * the caller: the note in hexv() is this exact lesson -- a
     * compare whose arm wants to become the answer invites the
     * reshuffle, and the m68k answers a value merge with a MOVE set
     * down over the condition codes (hazard.py's class).  Each
     * bound returns its own answer, so there is nothing to merge. */
    if op < 40 {
        return 40;
    }
    if op > 255 {
        return 255;
    }
    op
}

/// One row of the pane: the base less the top edge's cut, plus the
/// foot's lift, clamped into 16..255.  No condition of its own --
/// which rows carry the cut and which the lift is the caller's
/// business, split into runs of rows rather than asked per row.
/// That is what keeps a value select (and with it a branch reading
/// flags a MOVE has since overwritten, hazard.py's class) out of
/// the draw loop and out of the battery, and the clamp answers by
/// returning for the reason `aero_base` does.
///
/// Both clamps stay in their own frame (`inline(never)`) for the
/// reason prefs.rs's own clamp survives the gate: the answer has to
/// leave in `%d0`, so the value is computed into `%d0` beside it,
/// the compare runs on a copy in a scratch register, and the branch
/// goes straight over the arm -- the shape `apply`'s `u > 100`
/// takes.  Inlined into the draw loop the same two clamps wanted
/// their copy made between the compare and the branch, which is
/// exactly the window hazard.py reads.
#[inline(never)]
pub fn aero_row(op: i32, cut: i32, ease: i32) -> i32 {
    let a = op.wrapping_sub(cut).wrapping_add(ease);

    if a < 16 {
        return 16;
    }
    if a > 255 {
        return 255;
    }
    a
}

/// The whole formula, one row at a time -- the shape main.c carried
/// and the unit tests pin it by.  Test-only on purpose: its per-row
/// tests lower to selects, and selects are exactly what the gate
/// reads on the m68k, so this never reaches the image; the draws and
/// the battery take `aero_base` over the three runs of rows instead,
/// and agree with this for the twenty-two rows the pane has.
#[cfg(test)]
pub fn aero_alpha(i: i32, h: i32, glass: u32) -> i32 {
    let op = aero_base(glass);
    let cut = if i < 4 { (4 - i) * 7 } else { 0 };
    let ease = if i >= h - 4 { 14 } else { 0 };

    aero_row(op, cut, ease)
}

/// One layer of a glow: five stacked passes of a fifth of the
/// strength, smallest last, so the disc falls away in soft steps.
/// Packed as radius<<8 | alpha so one word is one pass.
pub fn glow_layer(i: u32, r: u32, a: u32) -> u32 {
    ((r * i) / 5) << 8 | (a / 5)
}

/* The battery's word forms, so the reference arrays and this file's
 * tables are the same packing twice. */
const fn paint_word0(p: &Paint) -> u32 {
    ((p.kind as u32) << 30) | ((p.x as u32) << 20) | ((p.y as u32) << 10) | (p.r as u32)
}

const fn paint_word1(p: &Paint) -> u32 {
    ((p.slot as u32) << 8) | (p.alpha as u32)
}

const fn orb_word0(e: &Orb) -> u32 {
    ((e.op as u32) << 30)
        | ((e.when as u32) << 27)
        | ((e.x as u32) << 17)
        | ((e.y as u32) << 7)
        | (e.r as u32)
}

const fn orb_word1(e: &Orb) -> u32 {
    ((e.colour as u32) << 8) | (e.alpha as u32)
}

/* ------------------------------------------------------------------ *
 * The boot battery.  The kernel hands over the Config's glass and the
 * backdrop's colours -- values nothing has seen before this boot --
 * beside its own reference for every formula and table, and the mask
 * that comes back names the family where the two halves came apart,
 * with `at` pointing inside it (0/1 for the night-field pair, the row
 * index, the layer, the word, the palette slot, and state<<8|entry
 * for the orb).  A pass writes one serial line, like the two before
 * it, and never appears in the log.
 * ------------------------------------------------------------------ */
const F_DARK: u32 = 1 << 0;
const F_AERO: u32 = 1 << 1;
const F_LAYER: u32 = 1 << 2;
const F_BLOOM: u32 = 1 << 3;
const F_PAL: u32 = 1 << 4;
const F_ORB: u32 = 1 << 5;
const F_BAD: u32 = 1 << 31;

const AERO_N: u32 = 22;
const PAINT_N: u32 = 14;
const PAINT_WORDS: u32 = PAINT_N * 2;
const PAL_N: u32 = 23;
const LAYER_N: u32 = 5;
const ORB_N: u32 = 17;
const ORB_WORDS: u32 = ORB_N * 2;

#[no_mangle]
pub extern "C" fn nb_rs_gfx_check(
    glass: u32,
    top: u32,
    bot: u32,
    dark_top: u32,
    dark_bot: u32,
    aero: *const u32,
    naero: u32,
    bloom: *const u32,
    nbloom: u32,
    pal: *const u32,
    npal: u32,
    layer: *const u32,
    nlayer: u32,
    orb: *const u32,
    norb: u32,
    at: *mut u32,
) -> u32 {
    if at.is_null() {
        return F_BAD;
    }
    unsafe { *at = 0 };

    if aero.is_null() || bloom.is_null() || pal.is_null() || layer.is_null() || orb.is_null()
    {
        unsafe { *at = 0x0bad_0000 };
        return F_BAD;
    }
    if naero != AERO_N {
        unsafe { *at = 0x0bad_0001 };
        return F_BAD;
    }
    if nbloom != PAINT_WORDS {
        unsafe { *at = 0x0bad_0002 };
        return F_BAD;
    }
    if npal != PAL_N {
        unsafe { *at = 0x0bad_0003 };
        return F_BAD;
    }
    if nlayer != LAYER_N {
        unsafe { *at = 0x0bad_0004 };
        return F_BAD;
    }
    if norb != ORB_WORDS {
        unsafe { *at = 0x0bad_0005 };
        return F_BAD;
    }

    let aero_s = unsafe { core::slice::from_raw_parts(aero, AERO_N as usize) };
    let bloom_s = unsafe { core::slice::from_raw_parts(bloom, PAINT_WORDS as usize) };
    let pal_s = unsafe { core::slice::from_raw_parts(pal, PAL_N as usize) };
    let layer_s = unsafe { core::slice::from_raw_parts(layer, LAYER_N as usize) };
    let orb_s = unsafe { core::slice::from_raw_parts(orb, ORB_WORDS as usize) };

    let mut mask = 0u32;

    /* Record the first disagreement; every family may keep failing
     * after it, but `at` only ever points at where it started.
     * Written through the out-pointer itself, the way store.rs
     * writes its own: kept as a local instead, the copy came out as
     * a register move sitting between the `mask == 0` compare and
     * its branch, which hazard.py reads as branching on condition
     * codes that MOVE has already overwritten -- a store cannot be
     * speculated so; a register can. */
    macro_rules! flag {
        ($cond:expr, $bit:expr, $val:expr) => {
            if $cond {
                if mask == 0 {
                    unsafe { *at = $val };
                }
                mask |= $bit;
            }
        };
    }

    /* bit 0: the night-field test, over the Config's own colours */
    flag!(bd_dark(top as u16) as u32 != dark_top, F_DARK, 0u32);
    flag!(bd_dark(bot as u16) as u32 != dark_bot, F_DARK, 1u32);

    /* bit 1: every row of the bar, at the glass this boot was given.
     * Three runs of rows rather than one loop with a test in it: the
     * test would be a value selected per row, and a value selection
     * is a branch on the m68k -- the class the gate reads.  The pane
     * is twenty-two rows (AERO_N), and the runs -- the four eased at
     * the top edge, the plain middle, the four lifted at the foot --
     * are written for exactly that, agreeing with the C's per-row
     * formula row for row. */
    let op = aero_base(glass);
    let mut i = 0usize;
    while i < 4 {
        flag!(
            aero_s[i] != aero_row(op, (4 - i as i32) * 7, 0) as u32,
            F_AERO,
            i as u32
        );
        i += 1;
    }
    while i < (AERO_N - 4) as usize {
        flag!(aero_s[i] != aero_row(op, 0, 0) as u32, F_AERO, i as u32);
        i += 1;
    }
    while i < AERO_N as usize {
        flag!(aero_s[i] != aero_row(op, 0, 14) as u32, F_AERO, i as u32);
        i += 1;
    }

    /* bit 2: the glow ladder, seeded from the same two colours so
     * the samples move with the file rather than with this table */
    let r0 = 3 + (top % 251);
    let a0 = 5 + (bot & 0xff);
    let mut k = 0usize;
    while k < LAYER_N as usize {
        flag!(
            layer_s[k] != glow_layer(k as u32 + 1, r0, a0),
            F_LAYER,
            k as u32
        );
        k += 1;
    }

    /* bits 3 and 4: the scene's tables, word by word */
    let mut j = 0usize;
    while j < PAINT_N as usize {
        flag!(bloom_s[j * 2] != paint_word0(&PAINTS[j]), F_BLOOM, (j * 2) as u32);
        flag!(
            bloom_s[j * 2 + 1] != paint_word1(&PAINTS[j]),
            F_BLOOM,
            (j * 2 + 1) as u32
        );
        j += 1;
    }
    let mut p = 0usize;
    while p < PAL_N as usize {
        flag!(pal_s[p] != PAL[p] as u32, F_PAL, p as u32);
        p += 1;
    }

    /* bit 5: the orb, once per state it draws in -- menu closed,
     * menu open, and the classic bar -- filtered both ways through
     * the same `when` so a state's count is part of the answer.
     * The three states come as two tests of the counter, not as an
     * array of them: an array indexed by the loop counter is a
     * stack slot reached by a variable offset, which framefold.py
     * flags, and a slot folded that way once hung a real boot.
     * Over s = 0, 1, 2 the three pairs are exactly `s < 2` and
     * `s == 1`. */
    let mut s = 0usize;
    while s < 3 {
        let ae = s < 2;
        let op = s == 1;
        let mut mine = 0u32;
        let mut their = 0u32;
        let mut e = 0usize;
        while e < ORB_N as usize {
            if orb_applies(ORB[e].when, ae, op) {
                mine += 1;
            }
            let w = ((orb_s[e * 2] >> 27) & 7) as u8;
            if orb_applies(w, ae, op) {
                their += 1;
            }
            e += 1;
        }
        flag!(
            mine != their,
            F_ORB,
            ((s as u32) << 8) | 0x80
        );

        let mut ri = 0usize;
        let mut ci = 0usize;
        while ri < ORB_N as usize && ci < ORB_N as usize {
            while ri < ORB_N as usize && !orb_applies(ORB[ri].when, ae, op) {
                ri += 1;
            }
            while ci < ORB_N as usize {
                let w = ((orb_s[ci * 2] >> 27) & 7) as u8;
                if orb_applies(w, ae, op) {
                    break;
                }
                ci += 1;
            }
            if ri >= ORB_N as usize || ci >= ORB_N as usize {
                break;
            }
            let spot = ((s as u32) << 8) | (ri as u32);
            flag!(orb_s[ci * 2] != orb_word0(&ORB[ri]), F_ORB, spot);
            flag!(orb_s[ci * 2 + 1] != orb_word1(&ORB[ri]), F_ORB, spot);
            ri += 1;
            ci += 1;
        }
        s += 1;
    }

    mask
}

/* ------------------------------------------------------------------ *
 * The paint itself -- m68k only, because the primitives it calls are
 * the C compositor's (boot/rom/gfx.c), which the host does not link.
 * Every colour comes from the tables above, so the battery the boot
 * runs is over exactly these numbers.
 * ------------------------------------------------------------------ */
#[cfg(target_arch = "m68k")]
mod chrome {
    use super::*;

    extern "C" {
        fn gfx_vgrad(x: i32, y: i32, w: i32, h: i32, c0: u16, c1: u16);
        fn gfx_alpha(x: i32, y: i32, w: i32, h: i32, c: u16, a: u8);
        fn gfx_alpha_r(x: i32, y: i32, w: i32, h: i32, r: i32, c: u16, a: u8);
        fn gfx_disc(cx: i32, cy: i32, r: i32, c: u16);
        fn gfx_disc_a(cx: i32, cy: i32, r: i32, c: u16, a: u8);
        fn gfx_text_s(x: i32, y: i32, s: *const u8, c: u16, scale: i32);
        fn logo_mark(x: i32, y: i32, size: i32);
    }

    /* One glow, five passes through the ladder the battery checks. */
    unsafe fn glow(x: i32, y: i32, r: i32, c: u16, a: u8) {
        let mut i = 5;
        while i >= 1 {
            let packed = glow_layer(i as u32, r as u32, a as u32);
            gfx_disc_a(x, y, (packed >> 8) as i32, c, (packed & 0xff) as u8);
            i -= 1;
        }
    }

    /// The scene, exactly as wallpaper() drew it: the gradient, the
    /// glow pass the flags ask for, the grid, the night field's bloom
    /// behind the mark, then the mark and the wordmark.
    #[no_mangle]
    pub extern "C" fn nb_rs_wallpaper(top: u32, bot: u32, vista: i32, glow_f: i32, grid: i32) {
        let (t, b) = (top as u16, bot as u16);
        let dark = bd_dark(t);
        unsafe {
            gfx_vgrad(0, 0, 640, 512, t, b);
        }

        if glow_f != 0 {
            for p in PAINTS.iter() {
                if (p.kind == 0 && vista != 0) || (p.kind == 1 && vista == 0) {
                    unsafe {
                        glow(
                            p.x as i32,
                            p.y as i32,
                            p.r as i32,
                            resolve(p.slot, dark),
                            p.alpha,
                        );
                    }
                }
            }
        }

        if grid != 0 {
            let gc = resolve(SGRID, dark);
            let mut i: i32 = 0;
            while i < 512 {
                unsafe { gfx_alpha(0, i, 640, 1, gc, 26) };
                i += 32;
            }
            let mut x: i32 = 16;
            while x < 640 {
                unsafe { gfx_alpha(x, 0, 1, 512, gc, 15) };
                x += 32;
            }
        }

        for p in PAINTS.iter() {
            if p.kind == 2 && dark {
                unsafe {
                    glow(
                        p.x as i32,
                        p.y as i32,
                        p.r as i32,
                        resolve(p.slot, dark),
                        p.alpha,
                    );
                }
            }
        }

        /* The branding: mark, then shadow, then wordmark. */
        let mut sub = 0;
        for p in PAINTS.iter() {
            if p.kind == 3 {
                unsafe {
                    if sub == 0 {
                        logo_mark(p.x as i32, p.y as i32, p.r as i32);
                    } else {
                        gfx_text_s(
                            p.x as i32,
                            p.y as i32,
                            b"NEOBENCH\0".as_ptr(),
                            resolve(p.slot, dark),
                            p.r as i32,
                        );
                    }
                }
                sub += 1;
            }
        }
    }

    /// The bar's field: the rows at the glass this Config asks for,
    /// then the lit rim along the top and the bead that closes the
    /// pane at its foot.  Three runs of rows -- the four eased at the
    /// top edge, the plain middle, the four lifted where the pane
    /// closes -- so no row has a test of its own; the split is the
    /// battery's, written for the twenty-two rows the pane has.
    #[no_mangle]
    pub extern "C" fn nb_rs_aero_field(y: i32, h: i32, glass: u32) {
        let op = aero_base(glass);
        let mut i: i32 = 0;
        /* the four eased rows at the pane's top edge -- a constant
         * four, because the pane is twenty-two rows (AERO_N, the
         * battery's runs, the bar's own height) and `i < h && i < 4`
         * comes back from the optimiser as `i < min(h, 4)`, which is
         * a value selected and so a branch the gate has to judge */
        while i < 4 {
            unsafe {
                gfx_alpha(0, y + i, 640, 1, PAL[P_AERO], aero_row(op, (4 - i) * 7, 0) as u8)
            };
            i += 1;
        }
        /* the plain middle */
        while i < h - 4 {
            unsafe { gfx_alpha(0, y + i, 640, 1, PAL[P_AERO], aero_row(op, 0, 0) as u8) };
            i += 1;
        }
        /* the four lifted where the pane closes against the desktop */
        while i < h {
            unsafe { gfx_alpha(0, y + i, 640, 1, PAL[P_AERO], aero_row(op, 0, 14) as u8) };
            i += 1;
        }
        unsafe {
            gfx_alpha(0, y, 640, 1, PAL[P_INK], 165); /* the lit rim */
            gfx_alpha(0, y + 1, 640, 1, PAL[P_INK], 74);
            gfx_alpha(0, y + 2, 640, 1, PAL[P_INK], 34);
            gfx_alpha(0, y + h - 2, 640, 1, PAL[P_AERO_RIM], 78); /* closing bead */
            gfx_alpha(0, y + h - 1, 640, 1, PAL[P_SHADOW], 205);
        }
    }

    /// A button on the glass: rounded field, hairline rim, a line of
    /// light along its own top edge; the pressed state is the lit one.
    ///
    /// One test, branched once, with a call inside each arm.  The C's
    /// five ternaries on the one condition came out here as five
    /// branches wanting the same condition codes, and the m68k backend
    /// answers that by saving the CCR before the first and restoring
    /// it for each later branch -- which is precisely the shape
    /// hazard.py reads as branching on flags somebody else set.  Arms
    /// holding calls cannot be folded into the selects that invite
    /// it, so one test buys one branch and no saves at all.
    #[no_mangle]
    pub extern "C" fn nb_rs_aero_btn(x: i32, y: i32, w: i32, h: i32, on: i32) {
        unsafe {
            if on != 0 {
                btn_paint(
                    x,
                    y,
                    w,
                    h,
                    PAL[P_INK],
                    150,
                    PAL[P_AERO_ACT],
                    238,
                    130,
                );
            } else {
                btn_paint(
                    x,
                    y,
                    w,
                    h,
                    PAL[P_AERO_RIM],
                    120,
                    PAL[P_AERO_BTN],
                    195,
                    78,
                );
            }
        }
    }

    /// The three strokes every button is made of: the rim (lit or
    /// not), the field behind it, and the line of light along the
    /// top edge.
    unsafe fn btn_paint(
        x: i32,
        y: i32,
        w: i32,
        h: i32,
        edge: u16,
        edge_a: u8,
        field: u16,
        field_a: u8,
        light_a: u8,
    ) {
        gfx_alpha_r(x, y, w, h, 4, edge, edge_a);
        gfx_alpha_r(x + 1, y + 1, w - 2, h - 2, 3, field, field_a);
        gfx_alpha(x + 2, y + 1, w - 4, 1, PAL[P_INK], light_a);
    }

    /// The launcher: Vista's lit blue sphere with NeoBench's pearl on
    /// the glass, Workbench's bevelled disc off it -- one table, the
    /// state deciding which rows exist.
    #[no_mangle]
    pub extern "C" fn nb_rs_start_orb(aero: i32, menu: i32) {
        let ae = aero != 0;
        let open = menu != 0;

        for e in ORB.iter() {
            if !orb_applies(e.when, ae, open) {
                continue;
            }
            unsafe {
                match e.op {
                    OP_DISC => gfx_disc(e.x as i32, e.y as i32, e.r as i32, e.colour),
                    OP_DISC_A => gfx_disc_a(e.x as i32, e.y as i32, e.r as i32, e.colour, e.alpha),
                    _ => logo_mark(e.x as i32, e.y as i32, e.r as i32),
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ *
 * Unit tests.  The pins are the numbers the C drew with, written
 * out here independently of the tables where a table entry could be
 * wrong in both copies of this file at once; the sweep cases cover
 * the clamps the formulas exist for.
 * ------------------------------------------------------------------ */
#[cfg(test)]
mod tests {
    use super::*;

    /* The palette's first and last words, and the two colours the
     * whole scene turns on. */
    #[test]
    fn palette_pins() {
        assert_eq!(PAL.len(), 23);
        assert_eq!(PAL[P_AERO], 2150); /* NB_RGB(1, 3, 6)   */
        assert_eq!(PAL[P_INK], 65535); /* NB_RGB(31, 63, 31) */
        assert_eq!(PAL[P_AUR_B], (20u16 << 11) | (58u16 << 5) | 31);
        assert_eq!(PAL[P_LOGO_TEAL], PAL[P_ACC]); /* the halo is the brand teal */
    }

    /* bd_dark separates the five fields this desktop can show; the
     * weights are the eye's, and the boundary is a hard < 600. */
    #[test]
    fn dark_pins() {
        assert!(bd_dark(rgb(1, 3, 6))); /* the glass, a night blue */
        assert!(!bd_dark(rgb(31, 63, 31))); /* paper            */
        assert!(!bd_dark(rgb(31, 62, 30))); /* #FDFBF4 cream    */
        assert!(bd_dark((59u16 << 5) | 0)); /* 590: under the line   */
        assert!(bd_dark((59u16 << 5) | 2)); /* 590 + 8: still under  */
        assert!(!bd_dark((59u16 << 5) | 4)); /* 590 + 16: 606, over  */
        assert!(!bd_dark(60u16 << 5)); /* exactly 600, not < */
    }

    /* The bar's row alphas: glass 20 (the shipping default) eases
     * down the twenty-two rows with light where the pane needs it,
     * and a solid or an impossible glass lands in the clamps. */
    #[test]
    fn aero_pins() {
        assert_eq!(aero_alpha(0, 22, 20), 187);
        assert_eq!(aero_alpha(3, 22, 20), 208);
        assert_eq!(aero_alpha(4, 22, 20), 215);
        assert_eq!(aero_alpha(17, 22, 20), 215);
        assert_eq!(aero_alpha(18, 22, 20), 229);
        assert_eq!(aero_alpha(21, 22, 20), 229);
        assert_eq!(aero_alpha(0, 22, 0), 227); /* op 255            */
        assert_eq!(aero_alpha(0, 22, 100), 27); /* op 55, row one 55 - 28 */
        assert_eq!(aero_alpha(21, 22, 100), 69); /* op 55 + 14      */
        assert_eq!(aero_alpha(0, 22, 108), 16); /* op 39 clamps to 40, row to 16 */
    }

    /* One layer of the five-pass ladder. */
    #[test]
    fn layer_pins() {
        assert_eq!(glow_layer(5, 150, 104), (150u32 << 8) | 20);
        assert_eq!(glow_layer(1, 100, 100), (20u32 << 8) | 20);
        assert_eq!(glow_layer(5, 250, 255), (250u32 << 8) | 51);
        assert_eq!(glow_layer(3, 74, 76), (44u32 << 8) | 15);
    }

    /* The slots turn with the field, which is the whole reason the
     * tables carry slots rather than colours. */
    #[test]
    fn slot_resolution() {
        assert_eq!(resolve(SA, true), PAL[P_AUR_A]);
        assert_eq!(resolve(SA, false), PAL[P_GLOW_A]);
        assert_eq!(resolve(SWORD, true), PAL[P_INK]);
        assert_eq!(resolve(SWORD, false), PAL[P_LOGO_NAVY]);
        assert_eq!(resolve(SGRID, true), PAL[P_GRID_L]);
        assert_eq!(resolve(STEAL, false), PAL[P_LOGO_TEAL]);
        assert_eq!(resolve(200, true), PAL[P_INK]); /* unknown falls to ink */
    }

    /* The tables are the scene: sizes, order and the coordinates the
     * C drew the branding at. */
    #[test]
    fn table_pins() {
        assert_eq!(PAINTS.len(), 14);
        assert_eq!(ORB.len(), 17);
        assert_eq!(PAINTS[0].x, 40);
        assert_eq!(PAINTS[0].y, 58);
        assert_eq!(PAINTS[6].r, 62); /* the second white core */
        assert_eq!(PAINTS[11].x, 110); /* the mark   */
        assert_eq!(PAINTS[12].y, 429); /* the shadow */
        assert_eq!(PAINTS[13].y, 428); /* the wordmark */
        assert_eq!(ORB[10].op, OP_LOGO); /* the pearl's mark */
        assert_eq!(ORB[16].x, 22); /* the classic mark sits at 22,495 */
        assert_eq!(ORB[16].y, 495);
    }

    /* One packing of each form, computed by hand from the layout the
     * reference arrays use. */
    #[test]
    fn word_forms() {
        let p = &PAINTS[0];
        assert_eq!(paint_word0(p), (40u32 << 20) | (58u32 << 10) | 150);
        assert_eq!(paint_word1(p), 104); /* slot SA is zero */
        let e = &ORB[0];
        assert_eq!(
            orb_word0(e),
            (1u32 << 30) | (1u32 << 27) | (28u32 << 17) | (501u32 << 7) | 15
        );
        assert_eq!(orb_word1(e), (13622u32 << 8) | 70); /* teal halo */
    }

    /* Which rows exist in which state -- the filter both the drawing
     * and the battery walk. */
    #[test]
    fn state_filter() {
        assert!(orb_applies(W_ALWAYS, true, false));
        assert!(orb_applies(W_OPEN, true, true));
        assert!(!orb_applies(W_OPEN, true, false));
        assert!(orb_applies(W_CLOSED, true, false));
        assert!(!orb_applies(W_CLASSIC, true, false));
        assert!(orb_applies(W_CLASSIC, false, false));
        assert!(!orb_applies(W_OPEN, false, true));
    }

    /* The battery's guards: a null or a wrong length is the call
     * being unusable (bit 31), never a walk over memory that is not
     * ours. */
    #[test]
    fn guards() {
        let good = [0u32; 0];
        let mut at = 0u32;

        /* a null reference array: the call is unusable */
        assert_eq!(
            nb_rs_gfx_check(20, 0, 0, 1, 1, core::ptr::null(), 22,
                            good.as_ptr(), 28, good.as_ptr(), 23, good.as_ptr(), 5, good.as_ptr(), 34,
                            &mut at),
            F_BAD
        );
        assert_eq!(at, 0x0bad_0000);

        /* a length that does not match its table, named per family */
        assert_eq!(
            nb_rs_gfx_check(20, 0, 0, 1, 1, good.as_ptr(), 21, good.as_ptr(), 28,
                            good.as_ptr(), 23, good.as_ptr(), 5, good.as_ptr(), 34,
                            &mut at),
            F_BAD
        );
        assert_eq!(at, 0x0bad_0001);

        assert_eq!(
            nb_rs_gfx_check(20, 0, 0, 1, 1, good.as_ptr(), 22, good.as_ptr(), 27,
                            good.as_ptr(), 23, good.as_ptr(), 5, good.as_ptr(), 34,
                            &mut at),
            F_BAD
        );
        assert_eq!(at, 0x0bad_0002);

        assert_eq!(
            nb_rs_gfx_check(20, 0, 0, 1, 1, good.as_ptr(), 22, good.as_ptr(), 28,
                            good.as_ptr(), 22, good.as_ptr(), 5, good.as_ptr(), 34,
                            &mut at),
            F_BAD
        );
        assert_eq!(at, 0x0bad_0003);
    }

    /* A check that cannot fail is not a check: the reference arrays
     * are built here (packed the way the C packs them, then have one
     * word moved), and the mask has to name the family that moved. */
    fn build_refs(top: u32, bot: u32, glass: u32) -> (Vec<u32>, Vec<u32>, Vec<u32>, Vec<u32>, Vec<u32>) {
        let mut aero = Vec::new();
        let mut i = 0;
        while i < AERO_N {
            aero.push(aero_alpha(i as i32, 22, glass) as u32);
            i += 1;
        }
        let mut bloom = Vec::new();
        for p in PAINTS.iter() {
            bloom.push(paint_word0(p));
            bloom.push(paint_word1(p));
        }
        let pal: Vec<u32> = PAL.iter().map(|c| *c as u32).collect();
        let r0 = 3 + (top % 251);
        let a0 = 5 + (bot & 0xff);
        let mut layer = Vec::new();
        let mut k = 1;
        while k <= LAYER_N {
            layer.push(((r0 * k) / 5) << 8 | (a0 / 5));
            k += 1;
        }
        let mut orb = Vec::new();
        for e in ORB.iter() {
            orb.push(orb_word0(e));
            orb.push(orb_word1(e));
        }
        (aero, bloom, pal, layer, orb)
    }

    #[test]
    fn battery_agrees_with_its_own_reference() {
        let (a, b, p, l, o) = build_refs(777, 12345, 20);
        assert_eq!(
            nb_rs_gfx_check(20, 777, 12345, bd_dark(777 as u16) as u32,
                            bd_dark(12345 as u16) as u32,
                            a.as_ptr(), AERO_N, b.as_ptr(), PAINT_WORDS,
                            p.as_ptr(), PAL_N, l.as_ptr(), LAYER_N,
                            o.as_ptr(), ORB_WORDS, &mut 0),
            0
        );
    }

    #[test]
    fn battery_names_what_moved() {
        let (mut a, mut b, mut p, mut l, mut o) = build_refs(777, 12345, 20);
        let dt = bd_dark(777 as u16) as u32;
        let db = bd_dark(12345 as u16) as u32;
        let args = |a: &Vec<u32>, b: &Vec<u32>, p: &Vec<u32>, l: &Vec<u32>,
                    o: &Vec<u32>, at: &mut u32| {
            nb_rs_gfx_check(20, 777, 12345, dt, db,
                            a.as_ptr(), AERO_N, b.as_ptr(), PAINT_WORDS,
                            p.as_ptr(), PAL_N, l.as_ptr(), LAYER_N,
                            o.as_ptr(), ORB_WORDS, at)
        };

        /* one row of the bar moves */
        a[7] ^= 1;
        let mut at = 0u32;
        assert_eq!(args(&a, &b, &p, &l, &o, &mut at), F_AERO);
        assert_eq!(at, 7);
        a[7] ^= 1;

        /* one bloom word moves */
        b[5] ^= 0x400;
        let mut at = 0u32;
        assert_eq!(args(&a, &b, &p, &l, &o, &mut at), F_BLOOM);
        assert_eq!(at, 5);
        b[5] ^= 0x400;

        /* one palette entry moves */
        p[10] ^= 1;
        let mut at = 0u32;
        assert_eq!(args(&a, &b, &p, &l, &o, &mut at), F_PAL);
        assert_eq!(at, 10);
        p[10] ^= 1;

        /* one layer moves */
        l[2] ^= 1;
        let mut at = 0u32;
        assert_eq!(args(&a, &b, &p, &l, &o, &mut at), F_LAYER);
        assert_eq!(at, 2);
        l[2] ^= 1;

        /* one orb word moves: state 0 (menu closed), entry 3 */
        o[7] ^= 1;
        let mut at = 0u32;
        assert_eq!(args(&a, &b, &p, &l, &o, &mut at), F_ORB);
        assert_eq!(at, 3);
        o[7] ^= 1;

        /* the night-field answer lies */
        let mut at = 0u32;
        let lie = if dt == 0 { 1 } else { 0 };
        assert_eq!(
            nb_rs_gfx_check(20, 777, 12345, lie, db,
                            a.as_ptr(), AERO_N, b.as_ptr(), PAINT_WORDS,
                            p.as_ptr(), PAL_N, l.as_ptr(), LAYER_N,
                            o.as_ptr(), ORB_WORDS, &mut at),
            F_DARK
        );
        assert_eq!(at, 0);

        /* both at once: the mask carries both, `at` the first */
        a[1] ^= 1;
        p[4] ^= 1;
        let mut at = 0u32;
        assert_eq!(
            nb_rs_gfx_check(20, 777, 12345, dt, db,
                            a.as_ptr(), AERO_N, b.as_ptr(), PAINT_WORDS,
                            p.as_ptr(), PAL_N, l.as_ptr(), LAYER_N,
                            o.as_ptr(), ORB_WORDS, &mut at),
            F_AERO | F_PAL
        );
        assert_eq!(at, 1);
    }
}
