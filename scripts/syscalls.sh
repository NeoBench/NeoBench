#!/usr/bin/env bash
set -euo pipefail

PROJECT="NeoBench"

cd "$PROJECT"

echo "========================================"
echo "Creating NeoBench System Call Layer"
echo "========================================"

mkdir -p kernel/syscall
mkdir -p include/kernel
mkdir -p include/user

###############################################################################
# Syscall Table
###############################################################################

cat > kernel/syscall/syscall.c <<'EOF'
#include <kernel/syscall.h>

static syscall_handler_t syscall_table[NB_MAX_SYSCALLS];

void syscall_init(void)
{
    for (int i = 0; i < NB_MAX_SYSCALLS; i++)
        syscall_table[i] = 0;
}

void syscall_register(uint32_t number,
                      syscall_handler_t handler)
{
    if (number < NB_MAX_SYSCALLS)
        syscall_table[number] = handler;
}

uint32_t syscall_dispatch(uint32_t number,
                          uint32_t a,
                          uint32_t b,
                          uint32_t c,
                          uint32_t d)
{
    if (number >= NB_MAX_SYSCALLS)
        return (uint32_t)-1;

    if (!syscall_table[number])
        return (uint32_t)-1;

    return syscall_table[number](a,b,c,d);
}
EOF

###############################################################################
# Default Syscalls
###############################################################################

cat > kernel/syscall/system.c <<'EOF'
#include <kernel/syscall.h>

uint32_t sys_exit(uint32_t code)
{
    (void)code;
    return 0;
}

uint32_t sys_yield(void)
{
    return 0;
}

uint32_t sys_getpid(void)
{
    return 1;
}
EOF

###############################################################################
# Kernel Header
###############################################################################

cat > include/kernel/syscall.h <<'EOF'
#ifndef NB_SYSCALL_H
#define NB_SYSCALL_H

#include <neobench/types.h>

#define NB_MAX_SYSCALLS 256

typedef uint32_t (*syscall_handler_t)(
    uint32_t,
    uint32_t,
    uint32_t,
    uint32_t);

void syscall_init(void);

void syscall_register(
    uint32_t,
    syscall_handler_t);

uint32_t syscall_dispatch(
    uint32_t,
    uint32_t,
    uint32_t,
    uint32_t,
    uint32_t);

#endif
EOF

###############################################################################
# User Header
###############################################################################

cat > include/user/syscall.h <<'EOF'
#ifndef NB_USER_SYSCALL_H
#define NB_USER_SYSCALL_H

#include <neobench/types.h>

uint32_t syscall(
    uint32_t number,
    uint32_t a,
    uint32_t b,
    uint32_t c,
    uint32_t d);

#endif
EOF

echo
echo "System call layer created."
