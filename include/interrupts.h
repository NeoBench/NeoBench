#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include "types.h"

typedef void (*IntHandler)(uint32 level, void* data);

void intr_init(void);
void intr_disable(void);
void intr_enable(void);
uint16 intr_disable_save(void);
void intr_restore(uint16 sr);

int32 intr_set_handler(uint16 int_bit, IntHandler handler, void* data);
void intr_remove_handler(int32 slot);

uint32 intr_get_ticks(void);
uint32 intr_get_vblank_count(void);

#endif
