#ifndef NB_USER_SYSCALL_H
#define NB_USER_SYSCALL_H

#include <neobench/types.h>

uint32_t syscall(
    uint32_t number,
    uint32_t a,
    uint32_t b,
    uint32_t c,
    uint32_t d);

#endif
