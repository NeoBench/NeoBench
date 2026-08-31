# NeoBench

A from-scratch 68060 operating system that boots an NBFS root filesystem
through its own VFS layer, targeting the Amiga 4000 under FS-UAE.

> Status: **alpha (v0.1-alpha)** — the boot path, kernel VFS and NBFS root
> mount are implemented and covered by host tests. Verification is done on
> the host (unit/integration tests) plus a full cross-compile pipeline;
> no real-machine boot has been run yet from this tree.

## What is in the box

- `kernel/` — 68060 kernel (link base `0x40000000`, Zorro III RAM).
  Console/banner, shell (`neoshell` with `nbroot` / `nbcat`), a VFS with
  dentry/path/file abstraction, a memory-backed block device and the NBFS
  kernel driver. A small NBFS root image is embedded in the kernel binary
  and remounted onto a memdisk at boot.
- `boot/neoloader/` — boot ROM loader: low-level serial bootstrap,
  ELF loader and kernel transfer stub for FS-UAE.
- `libs/libnbfs/` — host library used by the NBFS tools and by the
  userspace VFS tests via the `nbfs_vfs` adapter.
- `tools/nbfs/` — `mkfs.nbfs` (formatter), `mkroot` (root image
  populator) and `nbfs-info` (inspector).
- `scripts/` — build helpers. `build.sh` runs the full pipeline and
  produces `images/neobench.hdf`; `build_hdf.py` lays out the RDB/boot
  block and a mountable FFS "boot" partition that carries the kernel.
- `fsuae/NeoBench-060.fs-uae` — FS-UAE configuration (A4000, 68060,
  2 MB chip, 256 MB Zorro III).

## NBFS

The on-disk format is little-endian; the 68060 target is big-endian, so
the kernel driver decodes every field with explicit little-endian readers
(`kernel/fs/nbfs/nbfs.c`). See `docs/nbfs/` and `libs/libnbfs/` for the
format and tools.

## Build

Requirements: `m68k-elf-gcc` toolchain, Python 3, a POSIX shell and make.

```sh
./scripts/build.sh
```

Produces:

- `rootfs/boot/kernel.elf` — kernel with embedded root image
- `rootfs/boot/neoloader-fsuae.bin` — FS-UAE loader binary
- `rootfs/boot/neobench-root.nbfs` — the embedded root filesystem image
- `images/neobench.hdf` — 16 MB hardfile to boot in FS-UAE

The root image can be sized with `NBFS_ROOT_SIZE_MB` when calling
`scripts/build_rootfs.sh` (default 8).

## Tests

Host integration tests exercise the kernel code paths compiled for the
host:

```sh
make -C kernel/fs/vfs all test     # userspace VFS + libnbfs adapter
make -C kernel/fs/nbfs/tests test  # kernel NBFS->VFS root mount
```

The kernel NBFS test loads `rootfs/boot/neobench-root.nbfs` into a memory
block device, mounts it through `vfs_mount_root()` and reads `/README.txt`,
`/etc/motd` and `/docs/readme.txt`.

## Roadmap

- Kernel init + shell polish, syscalls/userland handoff
- PIO IDE / SCSI real-media drivers for the root device
- NBFS write support in the kernel driver
- FreeBSD m68k integration groundwork (`bsd/`)