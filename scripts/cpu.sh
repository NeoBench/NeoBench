#!/usr/bin/env bash
set -euo pipefail

PROJECT="NeoBench"

cd "$PROJECT"

echo "======================================="
echo "Creating NeoBench CPU Subsystem"
echo "======================================="

mkdir -p kernel/cpu
mkdir -p include/arch

###############################################################################
# CPU Initialisation
###############################################################################

cat > kernel/cpu/cpu.c <<'EOF'
#include <arch/cpu.h>

static cpu_info_t cpu_info;

void cpu_init(void)
{
    cpu_detect();
}

cpu_info_t *cpu_get_info(void)
{
    return &cpu_info;
}
EOF

###############################################################################
# CPU Detection
###############################################################################

cat > kernel/cpu/detect.c <<'EOF'
#include <arch/cpu.h>

void cpu_detect(void)
{
    /* TODO:
     * Detect 68020/030/040/060
     * Determine MMU and FPU availability
     * Record cache capabilities
     */
}
EOF

###############################################################################
# MMU
###############################################################################

cat > kernel/cpu/mmu.c <<'EOF'
#include <arch/cpu.h>

void mmu_init(void)
{
    /* Enable and configure the MMU */
}
EOF

###############################################################################
# FPU
###############################################################################

cat > kernel/cpu/fpu.c <<'EOF'
#include <arch/cpu.h>

void fpu_init(void)
{
    /* Initialise the FPU if present */
}
EOF

###############################################################################
# Cache
###############################################################################

cat > kernel/cpu/cache.c <<'EOF'
#include <arch/cpu.h>

void cache_init(void)
{
    /* Enable instruction/data caches */
}
EOF

###############################################################################
# Header
###############################################################################

cat > include/arch/cpu.h <<'EOF'
#ifndef NB_CPU_H
#define NB_CPU_H

#include <neobench/types.h>

typedef enum
{
    CPU_UNKNOWN = 0,
    CPU_68020,
    CPU_68030,
    CPU_68040,
    CPU_68060
} cpu_type_t;

typedef struct
{
    cpu_type_t type;
    uint8_t has_mmu;
    uint8_t has_fpu;
    uint8_t has_icache;
    uint8_t has_dcache;
} cpu_info_t;

void cpu_init(void);
void cpu_detect(void);

cpu_info_t *cpu_get_info(void);

void mmu_init(void);
void fpu_init(void);
void cache_init(void);

#endif
EOF

echo
echo "CPU subsystem created."
