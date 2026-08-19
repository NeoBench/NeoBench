# NeoBench BSD Kernel Integration

NeoBench uses FreeBSD `stable/15` as the upstream BSD kernel base.

## Upstream baseline

- Repository: https://github.com/freebsd/freebsd-src
- Branch: `stable/15`
- Baseline commit: `fd9a9cbcdf27e8dabec9c4ce3edf4c4f868f0cf2`
- Baseline date: 2026-08-08

FreeBSD does not provide an official m68k/Amiga build target. NeoBench therefore maintains an out-of-tree 68060/Amiga machine port and integrates the existing NeoBench low-level work with the BSD kernel services.

## Target boot path

```text
Amiga 68060
    -> NeoLoader
    -> NeoBench m68k/68060 machine initialization
    -> FreeBSD-derived kernel core
    -> VM / scheduler / IPC / VFS / drivers
    -> NBFS root filesystem
    -> /System/NeoBench/init
    -> RTG/graphics
    -> NeoBench desktop
```

The kernel must boot directly to NeoBench; a Unix login shell is not the desktop interface.

## NeoBench integration layers

- `kernel/arch/m68k`: existing 68060 startup, exception, MMU and context code to be adapted to the BSD machine layer.
- `kernel/mm`, `kernel/sched`, `kernel/process`, `kernel/ipc`: existing kernel facilities to map onto BSD VM/scheduler/process/IPC services.
- `libs/libnbfs` and `tools/nbfs`: retained as the native NBFS implementation and tooling.
- `kernel/fs` and `kernel/vfs`: NBFS/VFS integration target.
- `drivers`: Amiga/Zorro/PCI/Mediator/RTG/storage/network/sound work to become BSD device drivers.
- `gui`: NeoBench desktop and Aero-style window/taskbar layer.

## Desktop target

NeoBench is the user-facing environment. The first graphics milestone is a 68060 RTG framebuffer with:

- bottom taskbar
- Start button/menu
- running application buttons
- system tray
- clock/date
- network and audio indicators
- Aero-inspired glass/translucent window chrome
- desktop icons

The visual design is inspired by Windows 7 Aero but is implemented as NeoBench code and assets.

## Development rule

`main` remains the known NeoBench baseline. BSD work is performed on `freebsd-integration` until the 68060 kernel reaches a bootable milestone.
