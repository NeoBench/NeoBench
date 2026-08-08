#ifndef NEOBENCH_TRAPFRAME_H
#define NEOBENCH_TRAPFRAME_H

#include <stdint.h>

/*
 * Minimal software-owned frame used at the MD boundary.  The exact 68060
 * format/special-status words are decoded by the assembly entry code before
 * this structure is handed to the BSD trap layer.
 */
struct neobench_trapframe {
    uint32_t d[8];
    uint32_t a[7];
    uint32_t usp;
    uint32_t sr;
    uint32_t pc;
    uint16_t vector;
    uint16_t format;
};

#endif
