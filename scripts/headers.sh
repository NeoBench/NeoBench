#!/usr/bin/env bash
set -euo pipefail

PROJECT="NeoBench"

if [ ! -d "$PROJECT" ]; then
    echo "Error: $PROJECT directory not found."
    echo "Run ./scripts/00_create_project.sh first."
    exit 1
fi

cd "$PROJECT"

echo "Creating header structure..."

mkdir -p include/neobench
mkdir -p include/arch
mkdir -p include/drivers
mkdir -p include/kernel

###############################################################################
# types.h
###############################################################################

cat > include/neobench/types.h <<'EOF'
#ifndef NB_TYPES_H
#define NB_TYPES_H

typedef signed char        int8_t;
typedef unsigned char      uint8_t;

typedef signed short       int16_t;
typedef unsigned short     uint16_t;

typedef signed int         int32_t;
typedef unsigned int       uint32_t;

typedef signed long long   int64_t;
typedef unsigned long long uint64_t;

typedef uint32_t size_t;

#endif
EOF

###############################################################################
# config.h
###############################################################################

cat > include/neobench/config.h <<'EOF'
#ifndef NB_CONFIG_H
#define NB_CONFIG_H

#define NB_NAME        "NeoBench"
#define NB_VERSION     "1.0.0"

#define NB_ARCH_M68K   1

#endif
EOF

###############################################################################
# kernel.h
###############################################################################

cat > include/kernel/kernel.h <<'EOF'
#ifndef NB_KERNEL_H
#define NB_KERNEL_H

void kernel_main(void);

#endif
EOF

###############################################################################
# cpu.h
###############################################################################

cat > include/arch/cpu.h <<'EOF'
#ifndef NB_CPU_H
#define NB_CPU_H

void cpu_init(void);
void mmu_init(void);
void fpu_init(void);

#endif
EOF

###############################################################################
# panic.h
###############################################################################

cat > include/kernel/panic.h <<'EOF'
#ifndef NB_PANIC_H
#define NB_PANIC_H

void panic(const char *message);

#endif
EOF

###############################################################################
# version.h
###############################################################################

cat > include/neobench/version.h <<'EOF'
#ifndef NB_VERSION_H
#define NB_VERSION_H

#define NB_MAJOR 1
#define NB_MINOR 0
#define NB_PATCH 0

#endif
EOF

echo "Headers created successfully."
