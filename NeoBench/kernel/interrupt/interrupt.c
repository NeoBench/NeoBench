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
