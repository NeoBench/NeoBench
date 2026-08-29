# Changelog

All notable changes to NeoBench are recorded here.

## [v0.1-alpha] - 2026-08-29

**Kernel boot path**

- Kernel links at the Zorro III RAM base `0x20000000` (A4000, 256 MB Z3),
  with VMA = base + ELF file offset, so a flat load at the load base makes
  link addresses match runtime addresses.
- `scripts/build_hdf.py` builds the RDB, boot block and a single FFS
  "boot" partition carrying the kernel; the generator now copes with
  multi-megabyte kernels (multi-block bitmap + chained file-header
  extensions) instead of crashing on the 96-pointer header limit.
- `scripts/build.sh` now runs the whole pipeline (libNBFS, mkfs, root
  image, kernel, loader, HDF).

**Root filesystem**

- A small NBFS root image (`rootfs/boot/neobench-root.nbfs`,
  `scripts/build_rootfs.sh`, `tools/nbfs/rootimg/mkroot`) is embedded into
  the kernel and remounted onto a memory-backed block device by the kernel
  driver.
- Kernel NBFS driver (`kernel/fs/nbfs/nbfs.c`) implements superblock/inode
  parsing (little-endian decode), directory lookup and extent-based reads.
- VFS filesystem now exposes a `get_size` / `read_file` operation table.
  Both the kernel NBFS driver and the userspace `nbfs_vfs` adapter
  implement it, so file open/read/seek/tell work identically in kernel and
  host-test contexts.
- Fixed an extent-read bug in `nbfs_kernel_read()` (`to_copy` was never
  decremented, causing a stack overrun / hang); caught by the new host
  kernel-root test under ASan.

**Shell**

- `neoshell` gains `nbroot` (root mount status) and `nbcat` (read a file
  via VFS).

**Tests**

- New `kernel/fs/nbfs/tests/test_kernel_root`: mounts the embedded root
  image via VFS on the host and verifies `/README.txt`, `/etc/motd`,
  `/docs/readme.txt` plus missing-name lookup.
- VFS userspace tests pass (8/8); host test builds no longer clobber the
  m68k object files in the kernel tree.
- `mkfs.nbfs` accepts an optional image size (MB) argument.

**Infrastructure**

- `kernel/include/string.h` + `kernel/lib/string.c` gain `memcmp`.

## [3d21230] - NeoLoader FS-UAE 68060 boot path

- FS-UAE bootable stage with loader binary and hardfile layout tooling.

## [ff25cab] - NBFS VFS and FreeBSD m68k groundwork

- Kernel VFS connected to NBFS; FreeBSD integration scripts and config.

## Prior

- Kernel VFS block device + NBFS, 68060 VFS in kernel, NBFS filesystem
  core, libNBFS, mkfs and the NBFS on-disk format.