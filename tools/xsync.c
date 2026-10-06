/*
 * xsync -- ask the window manager's client to redraw.
 *
 *   gcc -O2 -o xsync tools/xsync.c -lX11
 *   ./xsync [needle]
 *
 * XGetImage reads what the server holds for a window, which is what
 * was last put there.  Whether that is current depends on the client:
 * one that renders into the window every frame answers straight away,
 * and one that renders elsewhere and blits only when it feels like it
 * does not.  Rather than guess which, this sends an Expose over the
 * whole window without clearing a pixel of it, which is the event that
 * makes a client repaint the window from whatever it is holding, and
 * then the grab that follows is of the newest thing it has.
 *
 * The same needle search as the grabber, so it acts on the window the
 * grab would take.
 */

#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static Display *dpy;
static char    *needle;
static Window   hit;

static void walk(Window w)
{
    Window root, parent, *kids = NULL;
    unsigned n = 0, i;
    char *name = NULL;
    XClassHint ch;

    if (hit)
        return;
    if (!XQueryTree(dpy, w, &root, &parent, &kids, &n))
        return;

    XFetchName(dpy, w, &name);
    ch.res_name = ch.res_class = NULL;
    XGetClassHint(dpy, w, &ch);

    if ((name && strcasestr(name, needle)) ||
        (ch.res_class && strcasestr(ch.res_class, needle)))
        hit = w;

    for (i = 0; !hit && i < n; i++)
        walk(kids[i]);

    if (name) XFree(name);
    if (ch.res_name)  XFree(ch.res_name);
    if (ch.res_class) XFree(ch.res_class);
    if (kids) XFree(kids);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: xsync needle\n");
        return 2;
    }
    needle = argv[1];

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "xsync: cannot open display\n");
        return 1;
    }

    walk(DefaultRootWindow(dpy));
    if (!hit) {
        fprintf(stderr, "xsync: no window named like \"%s\"\n", needle);
        XCloseDisplay(dpy);
        return 1;
    }

    /* width and height of zero clear nothing; exposures True asks for
     * the whole window's Expose instead */
    XClearArea(dpy, hit, 0, 0, 0, 0, True);
    XFlush(dpy);
    printf("xsync: exposed 0x%lx\n", (unsigned long)hit);
    XCloseDisplay(dpy);
    return 0;
}
