# nb_rs — the Rust half of NeoBench

Phase 0 of a move to Rust, in the terms the rest of the ROM is built in: one
crate, one exported function, and a proof that it survives the whole way to
a running machine.

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
    RUSTFLAGS="-C target-cpu=M68000" \
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
- **`m68k-linux-gnu-ld`** is the linker the target names itself. There is no
  lld for m68k.

## The habits the code keeps

- **32-bit arithmetic only.** `boot/rom/lib32.c` already owns `__mulsi3`,
  `__udivsi3`, `__divsi3` and their fellows, and every one of those is a
  plain object on the link line rather than a member of an archive. A Rust
  object asking for a 64-bit division would pull `compiler_builtins` in
  behind it and define the same names a second time; the link order (C
  objects first, archive last) keeps `lib32.c` answering, and staying in
  32 bits keeps the archive unopened. Phase 1 decides what to do about the
  64-bit case properly.
- **Nothing is a constant.** Every leg of the selftest is an identity
  checked against a value the compiler did not know when it compiled the
  code, so LLVM cannot fold the test into `return 0` and hand back a
  selftest that never ran.
- **The budget still holds.** The archive costs 222 bytes of ROM text, BSS
  is untouched, and `_end` stays at `$00139584` — under the `$00150000`
  ceiling the linker asserts on, because that is where Paula's sound buffer
  begins.

## What phase 1 is

The first real module: leaf logic with no hardware under it — preference
parsing, the store's paths, the tables `progs` and `run` read — tested on
the host the way `tools/tests` tests the C. The compositor and the kernel
come after that, not before, and each phase is judged by the same runs the
C is: 11 WARNs, 0 FAILED, and the desktop it draws.
