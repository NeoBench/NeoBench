#ifndef NB_CPU_H
#define NB_CPU_H

#include <neobench/types.h>

typedef enum
{
    CPU_UNKNOWN = 0,
    CPU_68020,
    CPU_68030,
    CPU_68040,
    CPU_68060
} cpu_type_t;

typedef struct
{
    cpu_type_t type;
    uint8_t has_mmu;
    uint8_t has_fpu;
    uint8_t has_icache;
    uint8_t has_dcache;
} cpu_info_t;

void cpu_init(void);
void cpu_detect(void);

cpu_info_t *cpu_get_info(void);

void mmu_init(void);
void fpu_init(void);
void cache_init(void);

#endif
