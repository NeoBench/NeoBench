/* NeoBench Compilation Bridge Global Injector (C99) */
#ifndef GUI_BRIDGE_H
#define GUI_BRIDGE_H

#include <stdint.h>
#include "gui.h"

/* C-style interface for memory */
void* nb_mem_alloc(uint32_t size);
void  nb_mem_free(void* ptr);

extern volatile uint32_t g_chip_top;
extern volatile uint32_t g_fast_base;
extern volatile uint32_t g_fast_size;
extern uint32_t kernel_end;

#endif
