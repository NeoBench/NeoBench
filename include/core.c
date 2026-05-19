#include "neobench.h"

#include <proto/exec.h>
#include <proto/dos.h>

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;

int nb_init(void)
{
    SysBase = *(struct ExecBase **)4UL;

    DOSBase = (struct DosLibrary *)
        OpenLibrary("dos.library", 0);

    if (!DOSBase)
        return 0;

    return 1;
}

void nb_shutdown(void)
{
    if (DOSBase)
        CloseLibrary((struct Library *)DOSBase);
}
