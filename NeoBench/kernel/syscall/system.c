#include <kernel/syscall.h>

uint32_t sys_exit(uint32_t code)
{
    (void)code;
    return 0;
}

uint32_t sys_yield(void)
{
    return 0;
}

uint32_t sys_getpid(void)
{
    return 1;
}
