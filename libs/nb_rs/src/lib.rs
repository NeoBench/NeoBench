/*
 * nb_rs -- the Rust half of NeoBench.
 *
 * This first file proves one thing and one thing only: that rustc can
 * emit 68000 objects for this ROM, that m68k-linux-gnu-ld takes them
 * beside the C it has always taken, and that they answer correctly on
 * the machine.  Nothing here reaches the screen; the C half calls
 * nb_rs_selftest() at boot and writes what it says to serial alone,
 * the development line nb_prefs_dump() already writes.
 *
 * Two habits are worth stating while the file is small enough to read:
 *
 *   - 32-bit arithmetic only.  lib32.c already owns __mulsi3, __divsi3
 *     and their fellows, and every one of those is a plain object on
 *     the link line rather than a member of an archive; a Rust object
 *     asking for a 64-bit division would pull compiler_builtins in
 *     behind it and define the same names a second time.  Phase 1
 *     decides what to do about that; this file stays where it cannot
 *     trip it.
 *
 *   - nothing is a constant.  Every leg is an identity checked
 *     against a value the compiler did not know when it compiled the
 *     code, so LLVM cannot fold the whole test into `return 0' and
 *     hand back a selftest that never ran.
 */

#![no_std]

/* What the seed is stirred with, so no leg ever sees a bare input. */
const STIR: u32 = 0xA5A5_5A5A;

/*
 * Six legs, one bit each.  Zero means every one of them held.
 */
#[no_mangle]
pub extern "C" fn nb_rs_selftest(seed: u32) -> u32 {
    let mut bad: u32 = 0;
    let x = seed ^ STIR;
    let v = x as i32;

    /* Unsigned division with its remainder: q*d + r is x, always. */
    let d: u32 = 65537;
    if x / d * d + x % d != x {
        bad |= 1;
    }

    /* The same identity signed -- the C half's __divsi3/__modsi3. */
    let ds: i32 = 7;
    if v / ds * ds + v % ds != v {
        bad |= 2;
    }

    /* Multiplication: one mulu and thirty-one additions must agree. */
    let mut sum: u32 = 0;
    let mut i: u32 = 0;
    while i < 31 {
        sum = sum.wrapping_add(x);
        i += 1;
    }
    if sum != x.wrapping_mul(31) {
        bad |= 4;
    }

    /* Rotation is a shift and an or, whichever of the two the target
     * happens to be given. */
    if x.rotate_left(4) != (x << 4) | (x >> 28) {
        bad |= 8;
    }

    /* Byte order in memory: the first byte of a big-endian u32 is the
     * top one, which is a fact about the target rather than about
     * this code -- a little-endian data layout fails it. */
    let first = unsafe { *(&x as *const u32 as *const u8) };
    if first as u32 != x >> 24 {
        bad |= 16;
    }

    /* A loop both halves can count: 1..=100 is 5050. */
    let mut total: u32 = 0;
    let mut n: u32 = 1;
    while n <= 100 {
        total = total.wrapping_add(n);
        n += 1;
    }
    if total != 5050 {
        bad |= 32;
    }

    bad
}

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo<'_>) -> ! {
    loop {}
}
