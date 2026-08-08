#include <kernel/interrupt.h>

volatile uint64_t system_ticks = 0;

void timer_init(void)
{

}

void timer_tick(void)
{
    system_ticks++;
}
