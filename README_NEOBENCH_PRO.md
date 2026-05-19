# NeoBench PRO & Desktop Integration

This branch contains the full integration of the **NeoBench PRO Kernel** (Linux-based) and the **NeoBench Desktop** (Bare Metal).

## Contents
- `kernel_mods/`: Custom Linux m68k drivers for NeoBench.
  - `neobench.c`: The professional "Windows-style" boot screen and system core.
  - `neobench_mediator.c`: Foundational PCI bridge support for Elbox Mediator.
  - `neobench_ppc.c`: Driver stub for PCI-based PowerPC accelerators.
  - `neobench_pro.config`: The optimized kernel configuration (with Audigy, USB, CD-ROM, and Network enabled).
  - `Kconfig.bus.patch` & `Makefile.patch`: Architecture modifications.
- `Startup-Sequence.pro`: The dual-boot selector script for Amiga.
- `NeoBench_DE.fs-uae`: The FS-UAE configuration file.
- `create_linux_hdf.py` & `analyze_rdb.py`: RDB/HDF management tools.
- All core NeoBench Bare Metal source files.

## How to push this to GitHub
Since GitHub requires a Personal Access Token (PAT) for HTTPS, run the following commands in your terminal:

```bash
cd ~/NeoBench_Upload
git push -u origin pro-kernel-integration
```
When prompted for your password, paste your **GitHub Personal Access Token**.

## How to build the PRO Kernel
1. Clone the official linux-m68k kernel.
2. Copy the files from `kernel_mods/` to their respective paths in the kernel tree.
3. Apply the patches to `arch/m68k/Kconfig.bus` and `arch/m68k/amiga/Makefile`.
4. Use `neobench_pro.config` as your `.config`.
5. Build using the m68k-linux-gnu cross-compiler.
