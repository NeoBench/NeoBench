#include <stdint.h>

/* Machine-dependent exception handoff. */
void neobench_exception_dispatch(void)
{
    /* TODO: decode the 68060 exception frame and hand it to BSD trap code. */
}
