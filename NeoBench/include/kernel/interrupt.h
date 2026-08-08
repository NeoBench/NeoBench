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
