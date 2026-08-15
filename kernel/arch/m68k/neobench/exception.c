#include <stdint.h>

/*
 * Common entry point for the initial NeoBench 68060 vector table.
 *
 * Until the FreeBSD m68k trap-frame ABI is established, keep this handler
 * side-effect free. The assembly vector entry owns exception-frame handling
 * and must eventually perform the appropriate RTE sequence.
 */
__attribute__((noreturn))
void neobench_exception(uint32_t vector)
{
    (void)vector;

    for (;;) {
        __asm__ volatile ("stop #0x2700");
    }
}

/*
 * Machine-dependent exception handoff.
 *
 * This is retained as the FreeBSD-facing dispatch boundary. The actual
 * 68060 exception-frame decoding and BSD trap handoff will be connected
 * when the FreeBSD m68k trap-frame ABI is established.
 */
void neobench_exception_dispatch(void)
{
    neobench_exception(0);
}
