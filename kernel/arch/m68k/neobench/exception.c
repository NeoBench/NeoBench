#include <stdint.h>

/*
 * Common entry point for the initial NeoBench vector table.
 *
 * Until the FreeBSD m68k trap-frame ABI is established, keep this handler
 * side-effect free.  Returning to the assembly stub is intentionally avoided
 * because an exception frame must be unwound with the correct RTE sequence.
 */
__attribute__((noreturn))
void neobench_exception(uint32_t vector)
{
    (void)vector;
    for (;;) {
        __asm__ volatile ("stop #0x2700");
    }
}
