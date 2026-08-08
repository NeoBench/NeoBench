#!/usr/bin/env bash
set -euo pipefail

PROJECT="NeoBench"

cd "$PROJECT"

echo "========================================"
echo "Creating NeoBench Interrupt Subsystem"
echo "========================================"

mkdir -p kernel/interrupt
mkdir -p include/kernel

###############################################################################
# Interrupt Manager
###############################################################################

cat > kernel/interrupt/interrupt.c <<'EOF'
#include <kernel/interrupt.h>

static interrupt_handler_t handlers[256];

void interrupt_init(void)
{
    for (int i = 0; i < 256; i++)
        handlers[i] = 0;
}

void interrupt_register(uint32_t vector,
                        interrupt_handler_t handler)
{
    if (vector < 256)
        handlers[vector] = handler;
}

void interrupt_dispatch(uint32_t vector)
{
    if (handlers[vector])
        handlers[vector]();
}
EOF

###############################################################################
# Exceptions
###############################################################################

cat > kernel/interrupt/exceptions.c <<'EOF'
#include <kernel/interrupt.h>

void exception_bus_error(void){}
void exception_address_error(void){}
void exception_illegal_instruction(void){}
void exception_zero_divide(void){}
void exception_trace(void){}
void exception_privilege(void){}
EOF

###############################################################################
# Timer
###############################################################################

cat > kernel/interrupt/timer.c <<'EOF'
#include <kernel/interrupt.h>

volatile uint64_t system_ticks = 0;

void timer_init(void)
{

}

void timer_tick(void)
{
    system_ticks++;
}
EOF

###############################################################################
# Header
###############################################################################

cat > include/kernel/interrupt.h <<'EOF'
#ifndef NB_INTERRUPT_H
#define NB_INTERRUPT_H

#include <neobench/types.h>

typedef void (*interrupt_handler_t)(void);

void interrupt_init(void);

void interrupt_register(uint32_t,
                        interrupt_handler_t);

void interrupt_dispatch(uint32_t);

void timer_init(void);

void timer_tick(void);

extern volatile uint64_t system_ticks;

#endif
EOF

echo
echo "Interrupt subsystem complete."
