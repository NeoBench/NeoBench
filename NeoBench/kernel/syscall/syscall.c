#include <kernel/syscall.h>

static syscall_handler_t syscall_table[NB_MAX_SYSCALLS];

void syscall_init(void)
{
    for (int i = 0; i < NB_MAX_SYSCALLS; i++)
        syscall_table[i] = 0;
}

void syscall_register(uint32_t number,
                      syscall_handler_t handler)
{
    if (number < NB_MAX_SYSCALLS)
        syscall_table[number] = handler;
}

uint32_t syscall_dispatch(uint32_t number,
                          uint32_t a,
                          uint32_t b,
                          uint32_t c,
                          uint32_t d)
{
    if (number >= NB_MAX_SYSCALLS)
        return (uint32_t)-1;

    if (!syscall_table[number])
        return (uint32_t)-1;

    return syscall_table[number](a,b,c,d);
}
