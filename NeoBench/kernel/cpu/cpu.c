#include <arch/cpu.h>

static cpu_info_t cpu_info;

void cpu_init(void)
{
    cpu_detect();
}

cpu_info_t *cpu_get_info(void)
{
    return &cpu_info;
}
