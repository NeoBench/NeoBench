/*
 * nb_rs -- the Rust half of NeoBench.
 *
 * This first file proves one thing and one thing only: that rustc can
 * emit 68000 objects for this ROM, that m68k-linux-gnu-ld takes them
 * beside the C it has always taken, and that they answer correctly on
 * the machine.  The C half calls nb_rs_selftest() at boot and writes
 * what it says to serial alone, the development line nb_prefs_dump()
 * already writes.  The work itself now stands beside it: prefs.rs is
 * the first module of phase 1, and it is held against the C it
 * mirrors both on the host (tools/tests/test_prefs) and at boot
 * (nb_rs_prefs_check, called from kernel/init/kernel_main.c).
 *
 * Two habits are worth stating while the file is small enough to read:
 *
 *   - 32-bit arithmetic only.  lib32.c already owns __mulsi3, __divsi3
 *     and their fellows, and every one of those is a plain object on
 *     the link line rather than a member of an archive; a Rust object
 *     asking for a 64-bit division would pull compiler_builtins in
 *     behind it and define the same names a second time.  Phase 1 was
 *     the test of that and kept to it -- prefs.rs parses in wrapping
 *     32-bit arithmetic and asks for no division of anything wider --
 *     so the archive still closes nothing behind it.
 *
 *   - nothing folds, and nothing floats.  The first version of this
 *     file wrote each leg as an identity between two expressions of
 *     the same value -- x/d*d + x%d is x for every x, whether or not
 *     a 68000 ever divides anything -- and LLVM, which only has to be
 *     right, not busy, proved all six and folded the whole function
 *     to `return 0'.  Phase 1's disassembly of the member is what
 *     caught it; rebuilding the phase-0 source showed the fold was
 *     there from the start.  Each leg now keeps one of its two sides
 *     behind a volatile read of the stack slot it was written to, so
 *     the comparison has to be emitted and both sides have to run.
 *     The same read is why the checks survive the machine: a stack
 *     slot is addressed off the SP inside the load itself, and a load
 *     whose result feeds the compare cannot be walked past the
 *     operands it feeds -- where core::hint::black_box had its operand
 *     address placed in a register the scheduler was free to lay
 *     between a compare and its branch, and on the m68k that MOVE set
 *     the condition codes under the branch's feet.  The divisors and
 *     trip counts come out of the seed as well, so the machine ends up
 *     calling lib32.c's __udivsi3, __divsi3 and __mulsi3 underneath,
 *     exactly as the C half does.
 */

/* no_std everywhere except under the test harness, where the host
 * brings std along for the assertions. */
#![cfg_attr(not(test), no_std)]

pub mod prefs;

/* What the seed is stirred with, so no leg ever sees a bare input. */
const STIR: u32 = 0xA5A5_5A5A;

/* Used once, inside nb_rs_opaque below; the selftest's own legs
 * deliberately do not come near it -- see the note at the top about
 * what its operand-address habit costs on the m68k. */
use core::hint::black_box;

/*
 * Six legs, one bit each.  Zero means every one of them held.
 *
 * Each leg keeps one of its two sides behind a volatile read so the
 * optimiser cannot take the check on the machine's behalf, and stirs
 * its divisor or trip count out of the seed so the arithmetic
 * underneath is lib32.c's and not a constant folded in place.  Both
 * halves run; a 68000 that divided wrong would say so.
 */
#[no_mangle]
pub extern "C" fn nb_rs_selftest(seed: u32) -> u32 {
    let mut bad: u32 = 0;
    let x = seed ^ STIR;
    let v = x as i32;

    /* Unsigned division with its remainder: q*d + r is x, always.
     * d comes from the seed and is never zero, so this is a real
     * __udivsi3/__umodsi3 pair and not a shift sequence whose answer
     * the compiler had already worked out. */
    let d: u32 = 65537 + (x & 0x3ff);
    let q0 = x / d;
    let q = unsafe { core::ptr::read_volatile(&q0) };
    if q * d + x % d != x {
        bad |= 1;
    }

    /* The same identity signed -- through the C half's __divsi3 --
     * with ds never zero and never -1, so no division can trap. */
    let ds: i32 = 1 + (v & 0x7f);
    let qs0 = v / ds;
    let qs = unsafe { core::ptr::read_volatile(&qs0) };
    if qs * ds + v % ds != v {
        bad |= 2;
    }

    /* Multiplication: one mulu and n additions must agree, for an n
     * the compiler never sees.  The count is a runtime modulo too --
     * a constant one is a magic multiply, which on this target means
     * a 64-bit helper the 32-bit-only rule does not allow. */
    let n: u32 = 1 + (x % (23 + (x & 7)));
    let mut sum: u32 = 0;
    let mut i: u32 = 0;
    while i < n {
        sum = sum.wrapping_add(x);
        i += 1;
    }
    let sum_v = unsafe { core::ptr::read_volatile(&sum) };
    if sum_v != x.wrapping_mul(n) {
        bad |= 4;
    }

    /* Rotation is a shift and an or, whichever of the two the target
     * happens to be given, by an amount likewise unknown.
     *
     * Both sides come back through volatile reads rather than
     * black_box: black_box's operand is the address of a stack slot,
     * an address the scheduler may compute anywhere it likes, and on
     * the m68k it walked that address setup between this leg's
     * compare and its branch -- a MOVE, which sets the condition
     * codes, so the branch tested the wrong flags and the leg failed
     * every time.  A read feeds the compare through the register the
     * data came in, and an instruction with its operands in the way
     * cannot be moved past them. */
    let amount: u32 = 4 + (x & 3);
    let rotated = x.rotate_left(amount);
    let expect = (x << amount) | (x >> (32 - amount));
    let l = unsafe { core::ptr::read_volatile(&rotated) };
    let r = unsafe { core::ptr::read_volatile(&expect) };
    if l != r {
        bad |= 8;
    }

    /* Byte order in memory: the first byte of a u32 is the one the
     * target's own endianness says it is -- the top byte for this
     * machine, big-endian as the ROM has always been -- read through
     * volatile so the answer cannot come from the value just stored.
     * The expectation is stated per target so the host can run it
     * too, but on the m68k it still fails a little-endian data
     * layout, since cfg! is what the compiler was told and the read
     * is what memory did.
     *
     * Volatile turns out not to be enough by itself here, and the
     * disassembly is what said so: five masks in the tail and not
     * six.  It stops the load being moved or removed, but LLVM
     * watched us write the slot and answered the compare from that
     * write, so `first as u32 != expect' simplified to an identity
     * and bad |= 16 left the listing altogether -- it even rewrote x
     * later in the function as the byte it had stored and read back,
     * which is the tell that it had decided the two agreed.  The
     * other legs keep their checks because their identities want a
     * combine that had already run while the value was still
     * opaque; this one is a two-instruction identity and had no such
     * luck.  So one side of the compare goes through nb_rs_opaque
     * below: a value equal to the right one on the machine and
     * knowable by nothing in this compiler. */
    let first = unsafe { core::ptr::read_volatile(&x as *const u32 as *const u8) };
    let expect: u32 = if cfg!(target_endian = "big") { x >> 24 } else { x & 0xff };
    if first as u32 != nb_rs_opaque(expect) {
        bad |= 16;
    }

    /* A loop with a trip count from the seed: the squares of 1..k,
     * xor-ed together, counted up and counted down the other way.
     * Plain additions were tried first and LLVM closed each loop into
     * k(k+1)/2 behind our backs -- one 64-bit multiply for the exact
     * division, and the one helper lib32.c does not have.  XOR of
     * squares has no such form, holds in either order, and keeps a
     * mulu running in the body. */
    let k: u32 = 1 + (x % (37 + (x & 7)));
    let mut total: u32 = 0;
    let mut steps: u32 = 1;
    while steps <= k {
        total = total ^ steps.wrapping_mul(steps);
        steps += 1;
    }
    let mut down: u32 = 0;
    let mut left: u32 = k;
    while left > 0 {
        down = down ^ left.wrapping_mul(left);
        left -= 1;
    }
    let total_v = unsafe { core::ptr::read_volatile(&total) };
    if total_v != down {
        bad |= 32;
    }

    bad
}

/* The fold-catcher the byte-order leg above leans on: one call whose
 * answer this compiler may not take for its body.  External linkage
 * and never inlined turn out not to be enough on their own -- the
 * first version returned its argument outright, and with the body in
 * the same module the optimiser proved the call equal to its own
 * argument and deleted it, taking bad |= 16 with it.  So the body
 * hands its work to core::hint::black_box: at runtime the same
 * register comes back and the answer is right, while the returned
 * value is an opaque thing no analysis here can relate to the
 * argument.  The address black_box needs to do that sits inside this
 * tiny body -- a load and a return, no compare to walk between --
 * where the scheduler's habit of moving it costs nothing. */
#[inline(never)]
#[no_mangle]
pub extern "C" fn nb_rs_opaque(v: u32) -> u32 {
    black_box(v)
}

/* Under the test harness std owns the panic path; everywhere else
 * this is the only one there is.  On the m68k it hangs rather than
 * unwinds -- there is nothing to unwind to under a boot.  On the
 * host, where the archive links into a C test that would otherwise be
 * left waiting, it says what happened and stops. */
#[cfg(not(test))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo<'_>) -> ! {
    #[cfg(target_arch = "m68k")]
    loop {}

    #[cfg(not(target_arch = "m68k"))]
    unsafe {
        extern "C" {
            fn write(fd: i32, buf: *const u8, count: usize) -> isize;
            fn _exit(code: i32) -> !;
        }
        let msg = b"nb_rs: panic in the Rust half\n";

        write(2, msg.as_ptr(), msg.len());
        _exit(101);
    }
}

/* abort(), which the ROM's immediate-abort panics land in: bounds
 * check, index failure, an expect on a None -- each becomes a call to
 * this, and this is the freeze the C half would give anyway.  Only
 * the m68k build gets it; the host has a libc abort and would not
 * thank us for defining over it. */
#[cfg(all(target_arch = "m68k", not(test)))]
#[no_mangle]
pub extern "C" fn abort() -> ! {
    loop {}
}

/* The unwinder's personality, which nothing here will ever call: a
 * panic takes the handler above and stops.  The host archive bundles
 * the sysroot's core, built the other way -- with unwinding -- and
 * that is what puts the reference in; the m68k's core comes out of
 * -Zbuild-std with panic aborted and never has one, which is why the
 * ROM link has never needed this. */
#[cfg(all(not(target_arch = "m68k"), not(test)))]
#[no_mangle]
pub extern "C" fn rust_eh_personality() {}
