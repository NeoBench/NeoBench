#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* NeoBench Graphical Installer v1.2
 * Direct Library Calling version
 */

void *IntuitionBase;
void *GadToolsBase;

typedef void * BPTR;
extern void *OpenLibrary(char *, unsigned long);
extern void CloseLibrary(void *);
extern void *OpenWindowTags(void *, unsigned long, ...);
extern void CloseWindow(void *);
extern void *GT_GetIMsg(void *);
extern void GT_ReplyIMsg(void *);
extern void WaitPort(void *);

#define WA_Title (0x80000000+1)
#define WA_Width (0x80000000+4)
#define WA_Height (0x80000000+5)
#define WA_CloseGadget (0x80000000+10)
#define WA_DragBar (0x80000000+12)
#define IDCMP_CLOSEWINDOW (1L<<9)

int main(int argc, char **argv) {
    void *win;
    int done = 0;

    if (!(IntuitionBase = OpenLibrary("intuition.library", 37))) return 20;
    if (!(GadToolsBase = OpenLibrary("gadtools.library", 37))) return 20;

    win = OpenWindowTags(NULL,
        WA_Title, "NeoBench Installer",
        WA_Width, 400, WA_Height, 200,
        WA_DragBar, 1, WA_CloseGadget, 1,
        (0x80000000+74), (IDCMP_CLOSEWINDOW), 
        0);

    if (win) {
        while (!done) {
            void *msg;
            WaitPort(((char *)win + 84));
            while ((msg = GT_GetIMsg(((char *)win + 84)))) {
                unsigned long class = *(unsigned long *)((char *)msg + 20);
                if (class == IDCMP_CLOSEWINDOW) done = 1;
                GT_ReplyIMsg(msg);
            }
        }
        CloseWindow(win);
    }

    CloseLibrary(GadToolsBase);
    CloseLibrary(IntuitionBase);
    return 0;
}
