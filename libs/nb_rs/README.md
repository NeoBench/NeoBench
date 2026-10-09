# nb_rs — the Rust half of NeoBench

Phase 0 of a move to Rust, in the terms the rest of the ROM is built in: one
crate, a small set of exported functions, and a proof that every one of them
survives the whole way to a running machine.

## What phase 0 proves

That rustc emits 68000 objects for this ROM, that `m68k-linux-gnu-ld` takes
them beside the C it has always taken, and that they answer correctly when
the machine is actually running. The C half calls `nb_rs_selftest(seed)` at
boot and writes what it says to serial alone — the development line
`nb_prefs_dump()` already writes — so a pass is `>rs ok` on the wire and
nothing on the screen, and a failure is the one amber line `Rust codegen
selftest` in the boot log. Both ways in are verified: as the Kickstart ROM
and as the chainload hunk under a genuine AmigaOS 3.2.3, where the image is
relocated by `LoadSeg()` before it runs.

## How it builds

`boot/rom/Makefile` builds it and links the archive after the C objects:

```sh
cd libs/nb_rs
RUSTC_BOOTSTRAP=1 RUSTC=$HOME/.cargo/bin/rustc \
    RUSTFLAGS="-C target-cpu=M68000 -Zunstable-options \
        -Cpanic=immediate-abort \
        -C llvm-args=--disable-machine-cse \
        -C llvm-args=--disable-machine-licm" \
    cargo build --release --target m68k-unknown-none-elf \
    -Zbuild-std=core,panic_abort
```

What each part of that is for:

- **`m68k-unknown-none-elf`** is a tier-3 bare-metal target: the spec is in
  every rustc, but no prebuilt `core` ships with it, so `-Zbuild-std` builds
  `core` from the `rust-src` component. Tier 3 means no guarantees — the
  toolchain that must be present is one with an LLVM that has the M68k
  backend (the official builds do; a distribution build against its own LLVM
  may not, and fails with `could not create LLVM TargetMachine for triple:
  m68k`).
- **`RUSTC_BOOTSTRAP=1`** lets the stable toolchain drive the `-Z` flag. A
  nightly would do the same without it.
- **`-C target-cpu=M68000`** is not optional: the target's own default is
  `M68010`, and everything else in this ROM is compiled for the 68000.
- **`-Zunstable-options -Cpanic=immediate-abort`** makes a panic jump
  straight to the crate's own `abort()` with no unwind machinery named —
  on the m68k a panic must stop the machine where it stands. The host
  test builds use `-Cpanic=abort` instead; neither is set in the cargo
  profile, because the ROM and the tests do not want the same answer.
- **The two `--disable` switches** turn off LLVM's machine
  common-subexpression and machine loop-invariant passes: both are free
  to leave a MOVE — which writes the condition codes on this machine —
  standing between a compare and the branch that was meant to read the
  compare's flags. With them off the Rust member scans clear of that
  pattern end to end (see the gates below).
- **`m68k-linux-gnu-ld`** is the linker the target names itself. There is no
  lld for m68k.

## The habits the code keeps

- **32-bit arithmetic only.** `boot/rom/lib32.c` already owns `__mulsi3`,
  `__udivsi3`, `__divsi3` and their fellows, and every one of those is a
  plain object on the link line rather than a member of an archive. A Rust
  object asking for a 64-bit division would pull `compiler_builtins` in
  behind it and define the same names a second time; the link order (C
  objects first, archive last) keeps `lib32.c` answering, and staying in
  32 bits keeps the archive unopened. Phase 1 kept that bargain and made
  it visible: the parser is plain byte work, the m68k has an `abort()` of
  Rust's own so no unwind path gets named, the panic strategy is passed
  on the rustc line (`-Cpanic=immediate-abort` for the ROM, where a panic
  must stop the machine where it stands), `codegen-units = 1` leaves the
  archive one object whose undefs are those seven integers and a final
  link with none.
- **Nothing is a constant.** Every leg of the selftest is an identity
  checked against a value the compiler did not know when it compiled the
  code, so LLVM cannot fold the test into `return 0` and hand back a
  selftest that never ran. The proofs read through volatile now, and leg
  five's six masks go through `nb_rs_opaque()` — an `#[inline(never)]`
  identity nothing follows — so they stand in the m68k disassembly as
  themselves.
- **The budget still holds.** The archive costs 14,371 bytes of ROM now
  that it parses, proves itself, checks the store and draws the scene —
  the one member the link pulls, behind `core` and `compiler_builtins`
  it never opens — and `_end` stands at `$0013AA40` — under the
  `$00150000` ceiling the linker asserts on, because that is where
  Paula's sound buffer begins.

## Two faults, and the gates over them

This backend commits two faults silently, and both were found by boots
that had already gone wrong — so the archive is scanned before it can
become a bootable image. `make -C boot/rom gates` runs both, and both
stand in the way of the ROM and the chain image:

- **`tools/hazard.py`** reads the final bytes for a branch reading
  condition codes a MOVE has overwritten: ISel leaves a copy between a
  compare and the branch that was meant to read the compare's flags, and
  post-RA it materialises as `movel`, which sets the codes here — the
  branch then tests the MOVE's operand, right for one input shape and
  wrong for the next. Zero tolerance, and the boot is the oracle behind
  it. The rule had a blind spot of its own: it matched the short and
  long forms only, and objdump spells a back-edge wider than 127 bytes
  with a `w` on the end — `bnew`, `beqw` — so every long back-edge was
  invisible to it. The word forms are in the rule now, and with them it
  read three sites the store check had hidden behind its own earlier
  zero; all three were reshaped out of the source rather than argued
  harmless, and the gate reads 0 over the image that boots.
- **`tools/framefold.py`** reads the IR for a stack slot reached through
  a variable index, which this backend lowers to a fixed displacement
  with the variable dropped — every access landing on the first slot.
  That is the shape that hung the first Phase 1 boot with the IR correct
  at every step.

Between them they set the house style for anything that runs on the m68k:
stack slots are reached with constant indices, variable indices belong to
caller pointers (registers keep theirs), a conditional arms a call rather
than a value the optimiser may set down between the compare and the
branch, and `apply()` is `#[inline(never)]` because inlined it would
hand `copy_str()` a frame slot where called it hands a pointer. Phase 3
added three more, all learned by reading what the gate refused: a run of
rows with one condition per run beats a condition per row — the
optimiser merges `i < h && i < 4` back into `i < min(h, 4)`, which is a
value selected; a clamp belongs in a frame of its own, so the value is
computed into the return register and the compare runs on a scratch
register with the branch straight over the arm, which is how prefs'
clamp has always lowered; and a table indexed by a loop counter is a
stack slot reached through a variable index, so the battery's states are
two tests of the counter rather than an array of them.

## What phase 1 is

Preference parsing, the first module with logic in it: `nb_rs_prefs_check()`
takes the four files the boot has just read and parses them again beside
the C's own `nb_prefs_load()`, answering with one bit per field where the
two halves disagree — the same call over the same bytes as
`tools/tests/test_prefs` runs differentially against `boot/rom/prefs.c` —
held to the C's struct numbers by compile-time asserts on both sides and
by `nb_rs_prefs_size()` at run time, with the crate's own fourteen unit
tests pinning the tables underneath.

## What phase 2 is

The store's own paths and the tables the shell reads, checked the same
way: `nb_rs_store_check()` takes the store the kernel has just walked —
every path through its `find`, the walk rebuilt from
`first_child`/`next_child` as header, children, NONE once for every node
in order, the `progs` name table against `PROG_NAME`, the `run` matching
against `tok_is`, and four corner samples of `next_child` per directory
— and answers with a six-family mask, `>rs store ok` or
`>rs store fail mask=… at=…` on the wire straight after the prefs
answer, amber `Rust store check` behind a failure. The lift that makes
the check mean something came with it: `N_PROGRAMS`, `prog_name`,
`tok_is` and `program_slot_of` moved out of `main.c` into
`user/gui/desktop/progs.c` behind `progs.h`, so the C the boot runs and
the Rust that checks it read one table rather than two that could
drift, and `tools/tests/test_store` runs both halves over the same
nodes differentially — `Config/Preferences` at slot 4, `Tools/Clock` at
1, `Core/Media/VLC` at 6 — with eight crate tests pinning the store's
shape and twenty-two tests in the crate in all. The phase also paid off
the scanner's blind spot above: with the word forms in the rule it
found three sites its own earlier zero had hidden — the hang that
stopped the plain boot after `>rs prefs ok` among them — and each was
reshaped out of the source. The compositor and the kernel come next —
not before them, and each phase is judged by the same runs the C is:
11 WARNs, 0 FAILED, and the desktop it draws.

## What phase 3 is

The scene's own paint: `wallpaper()`, the bar's field, the button and
the orb are `gfx.rs`'s now — the gradient, glow, grid, bloom and mark,
the twenty-two rows of glass, the button's field, the orb's two states
— drawn through the C compositor's primitives, which stayed C, with the
tables the scene is made of (the palette, the blooms, the orb and its
state filter) and the three formulas beside them in the same file, and
`main.c` keeping the handle, called the way the C called it over the
prefs it loads. `nb_rs_gfx_check()` answers for them the way prefs and
store answer for their halves: sixteen arguments, six families in one
mask — the night field's two seeds, the bar's rows, the ladder's
rungs, the bloom's words, the palette, and the orb counted in all three
states it draws in — `>rs gfx ok` straight after `>rs store ok`, amber
`Rust gfx check` behind a failure, and every leg standing on the
machine's own glass and backdrop so the boot runs it over values this
build never knew. `tools/tests/test_gfx` holds the same C reference on
the host: ten glasses over four backdrops swept, every family
falsified one word at a time with the mask and `at` pinned to the word
that moved, and the bad calls asked for by name — eleven crate tests
pin the tables underneath, thirty-three tests in the crate in all. The
two gates read 0 over the image that boots, a screenshot against the
C build's own desktop comes back pixel-identical bar the uptime
readout, and both boots answer 11 WARNs, 0 FAILED with `>rs gfx ok` on
the wire. What is left of the chrome in C — the menu's glass and the
windows' captions — is the slice after this one, and the kernel is
still to come.
