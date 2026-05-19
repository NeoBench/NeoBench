#ifndef TIMER_H
#define TIMER_H

#include "types.h"

void timer_init(void);
uint32 timer_get_ticks(void);
void timer_delay_ms(uint32 ms);
uint32 timer_get_uptime_seconds(void);
uint32 timer_get_tick_hz(void);
uint8 timer_get_is_pal(void);

#endif
