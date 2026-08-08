#ifndef NB_SYSCALL_H
#define NB_SYSCALL_H

#include <neobench/types.h>

#define NB_MAX_SYSCALLS 256

typedef uint32_t (*syscall_handler_t)(
    uint32_t,
    uint32_t,
    uint32_t,
    uint32_t);

void syscall_init(void);

void syscall_register(
    uint32_t,
    syscall_handler_t);

uint32_t syscall_dispatch(
    uint32_t,
    uint32_t,
    uint32_t,
    uint32_t,
    uint32_t);

#endif
