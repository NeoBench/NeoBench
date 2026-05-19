#include "../include/types.h"

volatile uint32 _g_chip_top = 0x200000;
volatile uint32 _g_fast_base = 0x08000000;
volatile uint32 _g_fast_size = 0;
uint32 _kernel_end = 0;

/* Stubs */
void _kernel_exception_handler(uint32 v, uint32 p, uint32 s) {}
void _kernel_fpu_exception_handler(uint32 v, uint32 p, uint32 s) {}
void _kernel_mmu_exception_handler(uint32 v, uint32 p, uint32 s) {}
