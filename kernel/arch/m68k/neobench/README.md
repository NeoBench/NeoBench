# NeoBench 68060 machine port

This directory is the boundary between FreeBSD's machine-independent kernel and the NeoBench Amiga 68060 platform.

## Existing NeoBench low-level code

The current bare-metal tree already contains an m68k startup path that establishes a stack, clears BSS, and enters `kernel_main`. It is intentionally retained as the starting point for the eventual BSD machine bootstrap rather than discarded.

## Target responsibilities

The machine port will provide:

- 68060 CPU/cache/MMU initialization
- exception/vector setup
- interrupt entry and dispatch
- context switch glue
- Amiga memory discovery
- CIA/custom-chip interrupt integration where required
- Zorro and Mediator/PCI platform discovery
- early console
- RTG framebuffer handoff
- boot information passed from NeoLoader

## Boot contract

```text
NeoLoader
  -> 68060 machine entry
  -> early MMU/cache/exception setup
  -> FreeBSD kernel entry
  -> platform/device initialization
  -> VM/scheduler/VFS
  -> NBFS root
  -> NeoBench init
  -> graphics
```

The current `start.S` is not yet a FreeBSD-compatible machine entry point. It is the preserved NeoBench bootstrap reference and must be adapted as the BSD port is implemented.
