# NeoBench Technical Outline & Roadmap

## 1. System Layers (Top-Down)

### Layer 5: Application Domain
- **Global Services:** Taskbar, Start Menu, Window Manager.
- **App Ecosystem:** 40+ native m68k applications (NeoWrite, NeoBrowse, etc.).
- **User Space:** Custom API for windowing, graphics, and filesystem access.

### Layer 4: Environment Manager (Loader)
- **Startup-Sequence:** Professional boot selector.
- **Integration Layer:** Handover logic between AmigaOS and NeoBench Kernels.

### Layer 3: Kernel Domain (Dual Stack)
- **PRO Kernel (Linux 6.x m68k):**
  - Modular driver architecture.
  - Multi-user / Multi-tasking protection.
  - Advanced networking and PCI stack.
- **Bare Metal Kernel (Native Denise):**
  - Ultra-low latency hardware access.
  - Custom interrupt handling.
  - Specialized for GUI performance.

### Layer 2: Hardware Abstraction (HAL)
- **Agnus/Alice (Chipset):** Direct DMA and Blitter management.
- **Zorro III:** AutoConfig device detection.
- **Mediator Bridge:** Z3-to-PCI memory mapping logic.

### Layer 1: Hardware Fabric
- **CPU:** Motorola 680x0.
- **Busboards:** Elbox Mediator.
- **Coprocessors:** PowerPC (Sonnet).

---

## 2. Technical Roadmap

### Phase 1: Core Integration [COMPLETED]
- [x] Dual-boot environment established.
- [x] Professional Windows-quality Linux boot sequence implemented.
- [x] Audigy, USB, and CD-ROM drivers enabled in PRO kernel.

### Phase 2: Hardware Expansion [IN PROGRESS]
- [ ] Finalize Z3 window mapping for Mediator PCI Config Space.
- [ ] Implement PPC memory buffer synchronization (Grackle Bridge).
- [ ] Expand RTG (Retargetable Graphics) support for Mediator-based Radeon/Voodoo cards.

### Phase 3: Desktop Refinement
- [ ] "Aero-style" transparency effects for Bare Metal WM.
- [ ] Unified /proc interface for hardware monitoring.
- [ ] Drag-and-drop file support between environments.

---

## 3. Directory Structure
- `kernel_mods/`: Source for custom Linux m68k NeoBench drivers.
- `apps/`: Native C++ source for the NeoBench Desktop application suite.
- `drivers/`: Bare metal hardware drivers (Chipset, Zorro, Input).
- `fs/`: Filesystem implementations (NeoFS, FFS).
- `boot/`: Low-level assembly entry points and bootblocks.

---
Copyright (c) **Lord Protector 2026**
