#!/usr/bin/env bash
set -euo pipefail

PROJECT="NeoBench"

cd "$PROJECT"

echo "========================================"
echo "Creating NeoBench Kernel"
echo "========================================"

###############################################################################
# Directories
###############################################################################

mkdir -p kernel

mkdir -p kernel/arch/m68k
mkdir -p kernel/cpu
mkdir -p kernel/mm
mkdir -p kernel/scheduler
mkdir -p kernel/ipc
mkdir -p kernel/syscall
mkdir -p kernel/fs
mkdir -p kernel/device
mkdir -p kernel/drivers
mkdir -p kernel/security
mkdir -p kernel/debug
mkdir -p kernel/init

###############################################################################
# Kernel Main
###############################################################################

cat > kernel/main.c <<'EOF'
#include <kernel/kernel.h>

#include <arch/cpu.h>

void kernel_main(void)
{
    cpu_init();

    mmu_init();

    fpu_init();

    kernel_init();

    scheduler_init();

    kernel_loop();
}
EOF

###############################################################################
# Kernel Init
###############################################################################

cat > kernel/init/init.c <<'EOF'
#include <kernel/kernel.h>

void kernel_init(void)
{

}
EOF

###############################################################################
# Scheduler
###############################################################################

cat > kernel/scheduler/scheduler.c <<'EOF'
#include <kernel/kernel.h>

void scheduler_init(void)
{

}
EOF

###############################################################################
# Kernel Loop
###############################################################################

cat > kernel/debug/loop.c <<'EOF'
#include <kernel/kernel.h>

void kernel_loop(void)
{
    for (;;)
    {
        __asm__ volatile ("nop");
    }
}
EOF

###############################################################################
# Panic
###############################################################################

cat > kernel/debug/panic.c <<'EOF'
#include <kernel/kernel.h>

void panic(const char *msg)
{
    (void)msg;

    for (;;)
    {
        __asm__ volatile ("stop #0x2700");
    }
}
EOF

###############################################################################
# CPU
###############################################################################

cat > kernel/cpu/cpu.c <<'EOF'
#include <arch/cpu.h>

void cpu_init(void)
{

}
EOF

cat > kernel/cpu/mmu.c <<'EOF'
#include <arch/cpu.h>

void mmu_init(void)
{

}
EOF

cat > kernel/cpu/fpu.c <<'EOF'
#include <arch/cpu.h>

void fpu_init(void)
{

}
EOF

###############################################################################
# Kernel Header
###############################################################################

cat > include/kernel/kernel.h <<'EOF'
#ifndef NB_KERNEL_H
#define NB_KERNEL_H

void kernel_main(void);

void kernel_init(void);

void scheduler_init(void);

void kernel_loop(void);

void panic(const char *);

#endif
EOF

echo
echo "Kernel created."
