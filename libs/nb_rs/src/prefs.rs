/*
 * Preference parsing -- phase 1, the first module that does work.
 *
 * Config/screen.cfg, Config/pointer.cfg, Config/sound.cfg and
 * Config/boot.cfg are four files of "key = value" that decide what the
 * desktop is before it is composited, and boot/rom/prefs.c has parsed
 * them since there were files to read.  This file parses them too,
 * byte for byte the way it does -- the same keys, the same clamps, the
 * same quirks, the same refusal to be upset by anything -- because the
 * point of phase 1 is not to replace the C but to hold the Rust
 * against it.  At boot the kernel hands this half the same four files
 * the C half was given and the struct it produced, and reads back a
 * mask of the fields where the two disagreed (nb_rs_prefs_check,
 * called from kernel/init/kernel_main.c); on the host the same bytes
 * go through both halves in tools/tests/test_prefs.c, beside the real
 * prefs.c.
 *
 * The quirks are the contract, not accidents to be tidied away:
 *
 *   - parsing never fails.  An unknown key, a value that will not
 *     convert, a line with no '=' in it: each costs its own effect and
 *     nothing else, and the default survives.  A broken file cannot
 *     stop the machine booting.
 *
 *   - which default a bad value falls back to depends on the key.
 *     Some assign afresh -- width = 800 followed by width = naff gives
 *     640, not 800 -- while others leave whatever was there -- bg_top
 *     = naff keeps the colour it had.  prefs.c decides by which helper
 *     it calls, and so does this file, line for line.
 *
 *   - arithmetic wraps exactly as the C's `unsigned` does, so a
 *     thirteen-digit hold count lands in the same wrong number here as
 *     there instead of this half panicking on its own side of the wire.
 *
 * And one habit the machine asks for: no index is ever sent for a byte
 * that might not be there.  This target treats a bounds panic as a
 * hang -- there is no unwinder under a boot -- so every position is
 * either found by an iterator or guarded the way the C guards it.
 *
 * The struct below is repr(C) and mirrors struct nb_prefs field for
 * field, which is what lets nb_rs_prefs_check() read the C's own
 * global as this type.  repr(C) follows the ABI of whichever target
 * compiled it, and the two that matter here disagree on purpose:
 * m68k-linux-gnu aligns an int at two bytes, the host at four, so
 * start_x sits at 82 in the ROM and 84 in a test binary.  Both numbers
 * are asserted on both sides of the boundary -- rustc against itself
 * below, gcc in kernel/init/kernel_main.c for the ROM and
 * tools/tests/test_prefs.c for the host -- so a layout that moved
 * stops a build rather than quietly comparing the wrong bytes.
 */

/// The bit table `diff` and `nb_rs_prefs_check` hand back: bit i is
/// field i in struct order, and 0 means the two halves agreed.
///
///   0 w        1 h        2 depth    3 lace     4 grid     5 glow
///   6 taskbar  7 bar_style 8 bar_glass 9 backdrop 10 bg_top 11 bg_bot
///  12 font    13 shape   14 scale   15 speed   16 shadow  17 visible
///  18 colour  19 start_x 20 start_y 21 snd_startup 22 snd_volume
///  23 snd_file 24 hold   25 hwscan  26 failsafe
///
/// and bit 31 alone means the call itself was unusable.
pub const BAD_CALL: u32 = 1 << 31;

/// The taskbar's Aero glass, and the flat Workbench field that was
/// there before it (BAR_AERO / BAR_CLASSIC in prefs.h).
pub const BAR_AERO: i32 = 0;
pub const BAR_CLASSIC: i32 = 1;

const BD_WASH: i32 = 0;
const BD_NAMES: [&[u8]; 5] = [b"wash", b"paper", b"azure", b"dusk", b"slate"];

/* 5-6-5 packed colour, the way gfx.h's NB_RGB packs it: five bits of
 * red, six of green, five of blue. */
const fn rgb(r: u16, g: u16, b: u16) -> u16 {
    ((r & 31) << 11) | ((g & 63) << 5) | (b & 31)
}

/*
 * struct nb_prefs, field for field, in the order prefs.h spells them.
 * The layout asserts at the bottom are the other half of the
 * equivalence this module rests on.
 */
#[repr(C)]
pub struct Prefs {
    pub w: u32,
    pub h: u32,
    pub depth: u32,
    pub lace: i32,
    pub grid: i32,
    pub glow: i32,
    pub taskbar: i32,
    pub bar_style: i32,
    pub bar_glass: u32,
    pub backdrop: i32,
    pub bg_top: u16,
    pub bg_bot: u16,
    pub font: [u8; 16],
    pub shape: i32,
    pub scale: i32,
    pub speed: i32,
    pub shadow: i32,
    pub visible: i32,
    pub colour: u16,
    pub start_x: i32,
    pub start_y: i32,
    pub snd_startup: i32,
    pub snd_volume: u32,
    pub snd_file: [u8; 40],
    pub hold: u32,
    pub hwscan: i32,
    pub failsafe: i32,
}

/*
 * What gcc computed for the same struct on each side.  An int aligns
 * at two bytes on m68k-linux-gnu and four on the host, so everything
 * after `colour` -- where a 16-bit field meets a 32-bit one -- lands
 * differently, and both are right.
 */
#[cfg(target_arch = "m68k")]
const _: () = {
    use core::mem::{offset_of, size_of};
    assert!(offset_of!(Prefs, font) == 44);
    assert!(offset_of!(Prefs, colour) == 80);
    assert!(offset_of!(Prefs, start_x) == 82);
    assert!(offset_of!(Prefs, snd_file) == 98);
    assert!(offset_of!(Prefs, hold) == 138);
    assert!(offset_of!(Prefs, failsafe) == 146);
    assert!(size_of::<Prefs>() == 150);
};

#[cfg(not(target_arch = "m68k"))]
const _: () = {
    use core::mem::{offset_of, size_of};
    assert!(offset_of!(Prefs, font) == 44);
    assert!(offset_of!(Prefs, colour) == 80);
    assert!(offset_of!(Prefs, start_x) == 84);
    assert!(offset_of!(Prefs, snd_file) == 100);
    assert!(offset_of!(Prefs, hold) == 140);
    assert!(offset_of!(Prefs, failsafe) == 148);
    assert!(size_of::<Prefs>() == 152);
};

/* ------------------------------------------------------------------ *
 * Bytes, treated the way the C treats them
 * ------------------------------------------------------------------ */

fn is_space(b: u8) -> bool {
    b == b' ' || b == b'\t' || b == b'\r'
}

fn fold(c: u8) -> u8 {
    if c >= b'A' && c <= b'Z' {
        c + (b'a' - b'A')
    } else {
        c
    }
}

/* Everything up to the first NUL -- how C reads a `char' buffer. */
fn cstr(buf: &[u8]) -> &[u8] {
    match buf.iter().position(|&b| b == 0) {
        Some(i) => &buf[..i],
        None => buf,
    }
}

/* Equal as C strings: the bytes past the terminator are not the
 * value, they are whatever the last value left there. */
fn str_eq(a: &[u8], b: &[u8]) -> bool {
    cstr(a) == cstr(b)
}

/* strcpy into a fixed field: cap-1 characters and a terminator. */
fn copy_str(dst: &mut [u8], src: &[u8]) {
    let cap = dst.len();
    let mut i = 0;

    if cap == 0 {
        return;
    }
    while i + 1 < cap && i < src.len() && src[i] != 0 {
        dst[i] = src[i];
        i += 1;
    }
    dst[i] = 0;
}

/* The parser's keys are compared exactly -- case matters, length
 * matters -- which is what key_is() in prefs.c settles for C. */
fn key_is(k: &[u8], name: &[u8]) -> bool {
    k == name
}

/* ------------------------------------------------------------------ *
 * Value conversions
 * ------------------------------------------------------------------ */

/* Digits, then the default if there were none.  Wraps as unsigned
 * does, so ten digits overflow identically on both sides. */
fn to_u(v: &[u8], def: u32) -> u32 {
    let mut i = 0;
    let mut n: u32 = 0;
    let mut seen = false;

    while i < v.len() && (v[i] == b' ' || v[i] == b'\t') {
        i += 1;
    }
    while i < v.len() && v[i] >= b'0' && v[i] <= b'9' {
        n = n.wrapping_mul(10).wrapping_add((v[i] - b'0') as u32);
        i += 1;
        seen = true;
    }
    if seen {
        n
    } else {
        def
    }
}

/* y/t/1 yes, n/f/0 no, and "on"/"off" told apart by the second
 * letter alone -- so "o" and "orange" are both off, which is the C's
 * reading and therefore this one's. */
fn on_off(v: &[u8], def: i32) -> i32 {
    let c0 = v.first().copied().unwrap_or(0);
    let c = if c0 >= b'A' && c0 <= b'Z' {
        c0 + (b'a' - b'A')
    } else {
        c0
    };

    match c {
        b'y' | b't' | b'1' => 1,
        b'n' | b'f' | b'0' => 0,
        b'o' => {
            if v.get(1) == Some(&b'n') || v.get(1) == Some(&b'N') {
                1
            } else {
                0
            }
        }
        _ => def,
    }
}

fn hexv(c: u8) -> i32 {
    /* Straight-out returns rather than a chain of alternatives with -1
     * merged at the end: a merged default is a value the scheduler may
     * set down anywhere it dominates, and on the m68k it set one down
     * between a compare and its branch -- a MOVE, which writes the
     * condition codes, so the branch read the wrong flags.  Each arm
     * computes its digit only on the far side of the branch that chose
     * it, and the -1 is what is left over at the bottom. */
    if c >= b'0' && c <= b'9' {
        return (c - b'0') as i32;
    }

    if c >= b'a' && c <= b'f' {
        return (c - b'a' + 10) as i32;
    }

    if c >= b'A' && c <= b'F' {
        return (c - b'A' + 10) as i32;
    }

    -1
}

/* The jth byte after base as a hex digit, -1 where there is none --
 * which is the same -1 the C's hexv() hands back for the terminator.
 * The bounds failure returns of its own accord for the same reason
 * hexv() above does: the -1 never becomes a value waiting to be
 * merged across the length compare. */
fn hex_at(v: &[u8], base: usize, j: usize) -> i32 {
    let i = base + j;

    if i >= v.len() {
        return -1;
    }

    hexv(v[i])
}

/* "#RRGGBB", with or without the hash, into RGB565; six hex digits
 * or the colour stays as it was. */
fn to_rgb(v: &[u8], def: u16) -> u16 {
    let mut i = 0;

    while i < v.len() && v[i] == b' ' {
        i += 1;
    }
    if i < v.len() && v[i] == b'#' {
        i += 1;
    }

    let h0 = hex_at(v, i, 0);
    let h1 = hex_at(v, i, 1);
    let h2 = hex_at(v, i, 2);
    let h3 = hex_at(v, i, 3);
    let h4 = hex_at(v, i, 4);
    let h5 = hex_at(v, i, 5);
    if h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0 || h5 < 0 {
        return def;
    }
    let r = (h0 * 16 + h1) as u16;
    let g = (h2 * 16 + h3) as u16;
    let b = (h4 * 16 + h5) as u16;
    ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
}

/* arrow unless the first letter says otherwise */
fn to_shape(v: &[u8], p: &mut Prefs) {
    p.shape = match v.first().copied().unwrap_or(0) {
        b'c' | b'C' => 1,
        b'i' | b'I' => 2,
        b'd' | b'D' => 3,
        _ => 0,
    };
}

/* "x,y" for the pointer's parking spot.  No comma, no change; a half
 * keeps whichever half did not convert. */
fn to_pair(v: &[u8], p: &mut Prefs) {
    let comma = match v.iter().position(|&b| b == b',') {
        Some(i) => i,
        None => return,
    };
    let x = to_u(v, p.start_x as u32);
    let y = to_u(&v[comma + 1..], p.start_y as u32);

    p.start_x = x as i32;
    p.start_y = y as i32;
}

/* A preference value compared without regard to case, so "Xen" and
 * "xen" name the same face. */
fn name_is(v: &[u8], want: &[u8]) -> bool {
    let mut i = 0;

    while i < want.len() {
        if i >= v.len() || v[i] == 0 || fold(v[i]) != fold(want[i]) {
            return false;
        }
        i += 1;
    }
    i == v.len()
}

/* -1 when it names no backdrop: unknown names leave the choice alone */
fn bd_index(v: &[u8]) -> i32 {
    let mut i = 0;

    while i < BD_NAMES.len() {
        if name_is(v, BD_NAMES[i]) {
            return i as i32;
        }
        i += 1;
    }
    -1
}

/* ------------------------------------------------------------------ *
 * The files
 * ------------------------------------------------------------------ */

/* One "key = value" line's worth of dispatch, with prefs.c's order
 * and prefs.c's defaults -- note which helpers take the current value
 * as their default (bg_top keeps what it had) and which are handed a
 * fresh one (width does not). */
fn set_key(key: &[u8], v: &[u8], p: &mut Prefs) {
    if key_is(key, b"width") {
        p.w = to_u(v, 640);
    } else if key_is(key, b"height") {
        p.h = to_u(v, 512);
    } else if key_is(key, b"depth") {
        p.depth = to_u(v, 8);
    } else if key_is(key, b"mode") {
        /* "hires-lace" carries the interlace flag in the name */
        p.lace = 0;
        let mut i = 0;
        while i + 1 < v.len() {
            if (v[i] == b'l' || v[i] == b'L') && (v[i + 1] == b'a' || v[i + 1] == b'A') {
                p.lace = 1;
                break;
            }
            i += 1;
        }
    } else if key_is(key, b"grid") {
        p.grid = on_off(v, 1);
    } else if key_is(key, b"glow") {
        p.glow = on_off(v, 1);
    } else if key_is(key, b"taskbar") {
        p.taskbar = on_off(v, 1);
    } else if key_is(key, b"bar") {
        /* "aero" is the default and anything carrying a c is the flat
         * field the desktop drew with before the glass existed */
        p.bar_style = BAR_AERO;
        if v.iter().any(|&b| b == b'c' || b == b'C') {
            p.bar_style = BAR_CLASSIC;
        }
    } else if key_is(key, b"glass") {
        let u = to_u(v, 20);
        p.bar_glass = if u > 100 { 100 } else { u };
    } else if key_is(key, b"bg_top") {
        p.bg_top = to_rgb(v, p.bg_top);
    } else if key_is(key, b"bg_bot") {
        p.bg_bot = to_rgb(v, p.bg_bot);
    } else if key_is(key, b"backdrop") {
        let i = bd_index(v);
        if i >= 0 {
            p.backdrop = i;
        }
    } else if key_is(key, b"font") {
        copy_str(&mut p.font, v);
    } else if key_is(key, b"shape") {
        to_shape(v, p);
    } else if key_is(key, b"scale") {
        let s = to_u(v, 1);
        p.scale = if s >= 2 { 2 } else { 1 };
    } else if key_is(key, b"speed") {
        let s = to_u(v, 2);

        /* Clamped as two min/max steps rather than one three-way
         * selection: the selection kept s live across its own
         * compare, and that is what invited the reshuffle the note
         * in hexv() describes -- the m68k answered by setting a MOVE
         * down over the condition codes, leaving the branch to read
         * codes the compare never set. */
        p.speed = s.min(8).max(1) as i32;
    } else if key_is(key, b"shadow") {
        p.shadow = on_off(v, 1);
    } else if key_is(key, b"visible") {
        p.visible = on_off(v, 1);
    } else if key_is(key, b"colour") || key_is(key, b"color") {
        p.colour = to_rgb(v, p.colour);
    } else if key_is(key, b"start") {
        to_pair(v, p);
    } else if key_is(key, b"startup") {
        p.snd_startup = on_off(v, 1);
    } else if key_is(key, b"volume") {
        let u = to_u(v, 48);
        p.snd_volume = if u > 64 { 64 } else { u };
    } else if key_is(key, b"file") {
        copy_str(&mut p.snd_file, v);
    } else if key_is(key, b"hold") {
        let u = to_u(v, 3);
        p.hold = if u > 15 { 15 } else { u };
    } else if key_is(key, b"scan") {
        p.hwscan = on_off(v, 1);
    } else if key_is(key, b"failsafe") {
        p.failsafe = on_off(v, 0);
    }
}

fn rtrim(mut s: &[u8]) -> &[u8] {
    /* Not `while let Some(&last) = s.last()': the option's merged form
     * put the saved byte and the shortened slice through registers
     * the break compared against, and one of those moves landed
     * where the codes were still the loop's to read -- see the note
     * in hexv().  Naming the byte inside an explicit non-empty guard
     * keeps every compare next to the branch that takes it. */
    while !s.is_empty() {
        let last = s[s.len() - 1];

        if !is_space(last) {
            break;
        }

        s = &s[..s.len() - 1];
    }

    s
}

fn first_nonspace(s: &[u8]) -> usize {
    /* A plain walk instead of position() with s.len() as the merged
     * default: same answer -- the index of the first byte that is not
     * space, or the length when they all are -- and no length to set
     * down beside the compare that might reject it. */
    let mut i = 0;

    while i < s.len() && is_space(s[i]) {
        i += 1;
    }

    i
}

/* One file: lines to '\n', '#' comments, "key = value" with both
 * sides trimmed, and a value cut to 39 characters because that is all
 * the C hands its converters.  A line that is not that is ignored,
 * which is the whole of the parser's error handling.
 *
 * Never inlined: the moment this body folds into its caller, `p'
 * becomes that caller's stack struct and copy_str()'s `dst[i]' walks
 * it as a frame slot with a variable index -- the shape the m68k
 * backend lowers to a fixed displacement and drops the index from
 * (the boot-hang shape; the IR gate counts those stores).  As a call,
 * `p' is the caller's pointer, held in a register, and the same walk
 * keeps its index (the frame rule below). */
#[inline(never)]
pub fn apply(p: &mut Prefs, file: &[u8]) {
    let mut pos = 0;
    let len = file.len();

    /* file.len() taken once at the top: both loops test against the
     * same length, and hoisting it keeps the tests plain compares
     * whose result nothing else needs -- the shape that cannot leave
     * a MOVE sitting over the condition codes (see the note in
     * hexv()). */
    while pos < len {
        let mut eol = pos;
        let mut rest = &file[pos..];

        /* The bound walked as a shrinking sub-slice rather than an
         * index against file.len(): emptiness is one zero test whose
         * result is the whole of the branch, where the index form had
         * the compiler subtracting to get the remaining count, throw
         * the count away and put the length back in a MOVE over the
         * codes the branch was about to read (the note in hexv()).
         * The byte test stands on its own so neither condition can be
         * folded back into the other. */
        while !rest.is_empty() {
            if rest[0] == b'\n' {
                break;
            }

            rest = &rest[1..];
            eol += 1;
        }
        let line = &file[pos..eol];
        let k = first_nonspace(line);

        if k < line.len() && line[k] != b'#' {
            if let Some(eq) = line[k..].iter().position(|&b| b == b'=') {
                let eq = k + eq;
                let key = rtrim(&line[k..eq]);
                let rest = &line[eq + 1..];
                let val = rtrim(&rest[first_nonspace(rest)..]);

                if !val.is_empty() {
                    /* The C copies the value into `char buf[40]',
                     * caps it at 39 and terminates it, and the
                     * converters read to the terminator (prefs.c).
                     * A sub-slice of `file' says the same three
                     * things -- cap, terminator, converters -- and
                     * keeps the read where this backend keeps its
                     * index: a stack array indexed by a variable is
                     * the shape it folds into a fixed displacement
                     * and drops the variable (the boot hung on it;
                     * see the note in nb_rs_prefs_check), while a
                     * slice of the caller's own pointer lowers as a
                     * register and the index rides along in the
                     * addressing mode the way to_u() shows.  The
                     * NUL stands in for the byte the C appends, so
                     * a value with one inside it is cut where
                     * set_key() on that side stops reading.  (Not
                     * even a hand-rolled copy to `core' territory:
                     * copy_from_slice resolves to an instantiation
                     * already living in `core', and any `core'
                     * member at all drags the rest of `core' and
                     * then compiler_builtins in behind it -- the
                     * better part of 400 KiB, in a region with
                     * 256.  A slice copies nothing.) */
                    let mut n = val.len().min(39);

                    if let Some(z) = val[..n].iter().position(|&b| b == 0) {
                        n = z;
                    }

                    set_key(key, &val[..n], p);
                }
            }
        }
        pos = if eol < file.len() { eol + 1 } else { eol };
    }
}

/* The values the desktop had before there were files to read, word
 * for word prefs.c's defaults(). */
pub fn defaults() -> Prefs {
    let mut p = Prefs {
        w: 640,
        h: 512,
        depth: 8,
        lace: 1,
        grid: 1,
        glow: 1,
        taskbar: 1,
        bar_style: BAR_AERO,
        bar_glass: 20,
        backdrop: BD_WASH,
        bg_top: rgb(1, 7, 8),
        bg_bot: rgb(1, 19, 16),
        font: [0; 16],
        shape: 0,
        scale: 1,
        speed: 2,
        shadow: 1,
        visible: 1,
        colour: rgb(31, 63, 31),
        start_x: 320,
        start_y: 256,
        snd_startup: 1,
        snd_volume: 48,
        snd_file: [0; 40],
        hold: 3,
        hwscan: 1,
        failsafe: 0,
    };

    /* Xen at nine pixels is the standard face */
    copy_str(&mut p.font, b"Xen");
    copy_str(&mut p.snd_file, b"Core/Media/startup.snd");
    p
}

/* Defaults, then the four files in the order nb_prefs_load() reads
 * them: screen, pointer, sound, boot.  An empty slice is a file that
 * is not there, which costs nothing. */
pub fn load(screen: &[u8], pointer: &[u8], sound: &[u8], boot: &[u8]) -> Prefs {
    let mut p = defaults();

    apply(&mut p, screen);
    apply(&mut p, pointer);
    apply(&mut p, sound);
    apply(&mut p, boot);
    p
}

/* Field by field, not byte by byte: the padding between fields holds
 * whatever the last run left there on one side and zeros on the
 * other, and neither is the value. */
pub fn diff(a: &Prefs, b: &Prefs) -> u32 {
    let mut m: u32 = 0;

    if a.w != b.w {
        m |= 1 << 0;
    }
    if a.h != b.h {
        m |= 1 << 1;
    }
    if a.depth != b.depth {
        m |= 1 << 2;
    }
    if a.lace != b.lace {
        m |= 1 << 3;
    }
    if a.grid != b.grid {
        m |= 1 << 4;
    }
    if a.glow != b.glow {
        m |= 1 << 5;
    }
    if a.taskbar != b.taskbar {
        m |= 1 << 6;
    }
    if a.bar_style != b.bar_style {
        m |= 1 << 7;
    }
    if a.bar_glass != b.bar_glass {
        m |= 1 << 8;
    }
    if a.backdrop != b.backdrop {
        m |= 1 << 9;
    }
    if a.bg_top != b.bg_top {
        m |= 1 << 10;
    }
    if a.bg_bot != b.bg_bot {
        m |= 1 << 11;
    }
    if !str_eq(&a.font, &b.font) {
        m |= 1 << 12;
    }
    if a.shape != b.shape {
        m |= 1 << 13;
    }
    if a.scale != b.scale {
        m |= 1 << 14;
    }
    if a.speed != b.speed {
        m |= 1 << 15;
    }
    if a.shadow != b.shadow {
        m |= 1 << 16;
    }
    if a.visible != b.visible {
        m |= 1 << 17;
    }
    if a.colour != b.colour {
        m |= 1 << 18;
    }
    if a.start_x != b.start_x {
        m |= 1 << 19;
    }
    if a.start_y != b.start_y {
        m |= 1 << 20;
    }
    if a.snd_startup != b.snd_startup {
        m |= 1 << 21;
    }
    if a.snd_volume != b.snd_volume {
        m |= 1 << 22;
    }
    if !str_eq(&a.snd_file, &b.snd_file) {
        m |= 1 << 23;
    }
    if a.hold != b.hold {
        m |= 1 << 24;
    }
    if a.hwscan != b.hwscan {
        m |= 1 << 25;
    }
    if a.failsafe != b.failsafe {
        m |= 1 << 26;
    }
    m
}

/*
 * The door the kernel calls at boot: the four files as they stand in
 * the store, and the struct prefs.c built out of them.  What comes
 * back is diff()'s mask -- 0 on every boot any NeoBench has shipped.
 *
 * `files` and `lens` are four entries each, screen, pointer, sound,
 * boot; a NULL entry or a zero length is a file that is not there.
 * The safety of the reads is the caller's contract (kernel_main.c
 * passes its own stack arrays and its own global), and the safety of
 * reading `have` as this type is the layout asserts above plus the
 * ones gcc holds on its side.
 */
#[no_mangle]
pub extern "C" fn nb_rs_prefs_check(
    files: *const *const u8,
    lens: *const u32,
    have: *const Prefs,
) -> u32 {
    if files.is_null() || lens.is_null() || have.is_null() {
        return BAD_CALL;
    }

    unsafe {
        /* Four guarded apply() calls, not four slices built and
         * joined into one value.  Three attempts at the join all
         * collapsed to the same `select': a returned pair, two early
         * returns, and locals that meet after the test -- SimplifyCFG
         * re-forms the select from any diamond whose arms are both
         * cheap.  m68k has no conditional move, so the select lowers
         * to `cmp; movel; beq' with the taken arm's value
         * materialised between the compare and the branch that reads
         * it, and MOVE has already overwritten the codes -- the
         * scanner called both sites out, on hardware they happened
         * to land where they stayed harmless, but "happened to" is
         * not a gate.  A call cannot be sunk into a diamond, so
         * guarding apply() itself leaves a plain `compare, branch,
         * call' with nothing speculatable to hoist into the gap --
         * the same shape the old array fill had (a store is not
         * speculatable either) before its frame index broke the
         * other way, and the shape C's own guard has.  `files' and
         * `lens' are the caller's pointers: registers, lowered with
         * their variable index intact (see the frame rule in
         * apply()).  Skipping an absent file is exactly what
         * applying an empty slice did -- apply() reads nothing and
         * changes nothing when there is nothing there. */
        let mut mine = defaults();

        for i in 0..4 {
            let p = *files.add(i);
            let n = *lens.add(i) as usize;

            if !p.is_null() && n > 0 {
                apply(&mut mine, core::slice::from_raw_parts(p, n));
            }
        }

        diff(&mine, &*have)
    }
}

/*
 * The struct's size as this half sees it, so the host test can hold
 * rustc's repr(C) against gcc's sizeof at run time as well as both
 * against their own constants at compile time.
 */
#[no_mangle]
pub extern "C" fn nb_rs_prefs_size() -> u32 {
    core::mem::size_of::<Prefs>() as u32
}

/* ------------------------------------------------------------------ *
 * The tables: the C's semantics, pinned value by value
 * ------------------------------------------------------------------ */

#[cfg(test)]
mod tests {
    use super::*;

    /* Parse one file over the defaults -- screen.cfg's slot is as
     * good as any, the parser does not know or care which file it is
     * reading. */
    fn one(text: &[u8]) -> Prefs {
        let mut p = defaults();

        apply(&mut p, text);
        p
    }

    #[test]
    fn defaults_match_prefs_c() {
        let p = defaults();

        assert_eq!(p.w, 640);
        assert_eq!(p.h, 512);
        assert_eq!(p.depth, 8);
        assert_eq!(p.lace, 1);
        assert_eq!(p.grid, 1);
        assert_eq!(p.glow, 1);
        assert_eq!(p.taskbar, 1);
        assert_eq!(p.bar_style, BAR_AERO);
        assert_eq!(p.bar_glass, 20);
        assert_eq!(p.backdrop, BD_WASH);
        assert_eq!(p.bg_top, rgb(1, 7, 8));
        assert_eq!(p.bg_bot, rgb(1, 19, 16));
        assert_eq!(cstr(&p.font), b"Xen");
        assert_eq!(p.shape, 0);
        assert_eq!(p.scale, 1);
        assert_eq!(p.speed, 2);
        assert_eq!(p.shadow, 1);
        assert_eq!(p.visible, 1);
        assert_eq!(p.colour, rgb(31, 63, 31));
        assert_eq!(p.start_x, 320);
        assert_eq!(p.start_y, 256);
        assert_eq!(p.snd_startup, 1);
        assert_eq!(p.snd_volume, 48);
        assert_eq!(cstr(&p.snd_file), b"Core/Media/startup.snd");
        assert_eq!(p.hold, 3);
        assert_eq!(p.hwscan, 1);
        assert_eq!(p.failsafe, 0);
    }

    #[test]
    fn clamps_bound_what_runs_away() {
        assert_eq!(one(b"hold = 99\n").hold, 15);
        assert_eq!(one(b"hold = 15\n").hold, 15);
        assert_eq!(one(b"hold = 0\n").hold, 0);
        assert_eq!(one(b"volume = 999\n").snd_volume, 64);
        assert_eq!(one(b"glass = 500\n").bar_glass, 100);
        assert_eq!(one(b"speed = 99\n").speed, 8);
        assert_eq!(one(b"speed = 0\n").speed, 1);
        assert_eq!(one(b"scale = 9\n").scale, 2);
    }

    #[test]
    fn a_bad_value_falls_back_to_whose_default_the_key_asks_for() {
        /* assign afresh: the fresh default, not the value before it */
        assert_eq!(one(b"hold = banana\n").hold, 3);
        assert_eq!(one(b"hold = 7\nhold = banana\n").hold, 3);
        assert_eq!(one(b"width = 800\nwidth = naff\n").w, 640);
        assert_eq!(one(b"volume = naff\n").snd_volume, 48);
        assert_eq!(one(b"glass = naff\n").bar_glass, 20);

        /* leave it alone: the value before it */
        let p = one(b"bg_top = #ffffff\nbg_top = naff\n");
        assert_eq!(p.bg_top, 0xffff);
        let p = one(b"backdrop = azure\nbackdrop = porcelain\n");
        assert_eq!(p.backdrop, 2);
        let p = one(b"start = 100,200\nstart = naff\n");
        assert_eq!((p.start_x, p.start_y), (100, 200));
    }

    #[test]
    fn on_and_off_are_told_apart_by_one_letter() {
        assert_eq!(one(b"grid = OFF\n").grid, 0);
        assert_eq!(one(b"glow = True\n").glow, 1);
        assert_eq!(one(b"taskbar = banana\n").taskbar, 1);
        assert_eq!(one(b"shadow = 0\n").shadow, 0);
        assert_eq!(one(b"visible = f\n").visible, 0);
        assert_eq!(one(b"startup = no\n").snd_startup, 0);

        /* per-key defaults: scan keeps the 1, failsafe the 0 */
        assert_eq!(one(b"scan = banana\n").hwscan, 1);
        assert_eq!(one(b"failsafe = banana\n").failsafe, 0);
        assert_eq!(one(b"failsafe = YES\n").failsafe, 1);

        /* the "o" reading: only "on" is on */
        assert_eq!(one(b"grid = on\n").grid, 1);
        assert_eq!(one(b"grid = off\n").grid, 0);
        assert_eq!(one(b"grid = o\n").grid, 0);
        assert_eq!(one(b"grid = orange\n").grid, 0);
    }

    #[test]
    fn mode_and_bar_and_backdrop_read_the_name() {
        assert_eq!(one(b"mode = hires-lace\n").lace, 1);
        assert_eq!(one(b"mode = hires\n").lace, 0);
        assert_eq!(one(b"mode = LACE\n").lace, 1);
        assert_eq!(one(b"mode = banana\n").lace, 0);

        assert_eq!(one(b"bar = classic\n").bar_style, BAR_CLASSIC);
        assert_eq!(one(b"bar = aero\n").bar_style, BAR_AERO);
        /* any c at all, which is the C's reading of the word */
        assert_eq!(one(b"bar = black\n").bar_style, BAR_CLASSIC);

        assert_eq!(one(b"backdrop = PAPER\n").backdrop, 1);
        assert_eq!(one(b"backdrop = SLATE\n").backdrop, 4);
        assert_eq!(one(b"backdrop = Wash\n").backdrop, 0);
    }

    #[test]
    fn colours_convert_and_refuse() {
        assert_eq!(one(b"bg_top = #ffffff\n").bg_top, 0xffff);
        assert_eq!(one(b"bg_top = ffffff\n").bg_top, 0xffff);
        /* #0A1E45 reduced to 565, which is the default's own value */
        assert_eq!(one(b"bg_top = #0A1E45\n").bg_top, 0x08e8);
        assert_eq!(one(b"bg_top = #zzzzzz\n").bg_top, defaults().bg_top);
        assert_eq!(one(b"bg_top = #ff80\n").bg_top, defaults().bg_top);
        assert_eq!(one(b"colour = #F4FAFF\n").colour, 0xf7df);
        assert_eq!(one(b"color = #000000\n").colour, 0);
    }

    #[test]
    fn shapes_convert_on_the_first_letter_alone() {
        assert_eq!(one(b"shape = cross\n").shape, 1);
        assert_eq!(one(b"shape = ibeam\n").shape, 2);
        assert_eq!(one(b"shape = dot\n").shape, 3);
        assert_eq!(one(b"shape = D\n").shape, 3);
        assert_eq!(one(b"shape = garlic\n").shape, 0);
        assert_eq!(one(b"shape = \n").shape, 0);
    }

    #[test]
    fn pairs_need_the_comma() {
        let p = one(b"start = 100,200\n");
        assert_eq!((p.start_x, p.start_y), (100, 200));

        let p = one(b"start = 100\n");
        assert_eq!((p.start_x, p.start_y), (320, 256));

        let p = one(b"start = naff,44\n");
        assert_eq!((p.start_x, p.start_y), (320, 44));

        let p = one(b"start = 100,\n");
        assert_eq!((p.start_x, p.start_y), (100, 256));
    }

    #[test]
    fn lines_that_are_not_lines_cost_their_own_effect_only() {
        assert_eq!(one(b"# hold = 9\n").hold, 3);
        assert_eq!(one(b"   # hold = 9\n").hold, 3);
        assert_eq!(one(b"nonsense\n").hold, 3);
        assert_eq!(one(b"hold =\n").hold, 3);
        assert_eq!(one(b"= 9\n").hold, 3);
        assert_eq!(one(b"holdx = 9\n").hold, 3);

        /* keys are case-sensitive; values mostly are not */
        assert_eq!(one(b"Hold = 9\n").hold, 3);

        /* the shapes a real file arrives in */
        assert_eq!(one(b"hold=5\n").hold, 5);
        assert_eq!(one(b"hold   =   5  \n").hold, 5);
        assert_eq!(one(b"hold = 5\r\n").hold, 5);
        assert_eq!(one(b"\n\nhold = 7\n\n").hold, 7);
        assert_eq!(one(b"hold = 5 # seconds\n").hold, 5);

        /* and the shape it must survive: no trailing newline */
        assert_eq!(one(b"hold = 9").hold, 9);
    }

    #[test]
    fn long_values_are_cut_where_the_c_cuts_them() {
        let long_font = b"font = 0123456789012345678901234567890123456789\n";
        assert_eq!(cstr(&one(long_font).font), b"012345678901234");

        let long_file = b"file = 0123456789012345678901234567890123456789\n";
        assert_eq!(
            cstr(&one(long_file).snd_file),
            b"012345678901234567890123456789012345678"
        );
    }

    #[test]
    fn a_value_ends_at_its_nul_the_way_cs_does() {
        let mut p = defaults();
        let file = [b'h', b'o', b'l', b'd', b'=', b'9', 0, b'x', b'y', b'z'];

        apply(&mut p, &file);
        assert_eq!(p.hold, 9);
    }

    #[test]
    fn files_stack_in_the_order_the_kernel_reads_them() {
        let screen = b"width = 800\nbg_top = #ffffff\n";
        let boot = b"width = 320\nhold = 6\n";
        let p = load(screen, b"", b"", boot);

        assert_eq!(p.w, 320);
        assert_eq!(p.bg_top, 0xffff);
        assert_eq!(p.hold, 6);
    }

    #[test]
    fn diff_names_the_field_that_moved_and_ignores_the_padding() {
        let a = defaults();
        let mut b = defaults();

        assert_eq!(diff(&a, &b), 0);

        b.hold = 6;
        assert_eq!(diff(&a, &b), 1 << 24);
        b.hold = a.hold;

        b.font[15] = 0x7f; /* past the terminator: not the value */
        assert_eq!(diff(&a, &b), 0);
        /* b's tail still differs from a's; the strings do not */
        b.font[0] = b'Z';
        assert_eq!(diff(&a, &b), 1 << 12);

        b = defaults();
        b.start_x = 1;
        b.failsafe = 1;
        assert_eq!(diff(&a, &b), (1 << 19) | (1 << 26));
    }

    #[test]
    fn a_runaway_count_wraps_the_way_unsigned_does() {
        /* ten nines run past 2^32 to 1410065407, and width takes the
         * wrapped number whole while hold is still clamped to 15 */
        assert_eq!(one(b"width = 9999999999\n").w, 1410065407);
        assert_eq!(one(b"hold = 9999999999\n").hold, 15);
    }
}
