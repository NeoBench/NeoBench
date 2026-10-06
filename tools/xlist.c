/*
 * xlist -- every window on the display, with the name that identifies it.
 *
 *   gcc -O2 -o xlist tools/xlist.c -lX11
 *   ./xlist [needle]
 *
 * The grabber picks the first window whose name contains a needle, and
 * if that is the wrong window then everything grabbed afterwards is a
 * picture of the wrong thing.  This is how the right one is found out:
 * the tree is walked from the root, each window's name and class are
 * printed with its geometry, and the depth is shown by indentation so a
 * frame with the real window inside it is visible as one.
 *
 * With an argument only the windows whose name or class contains it are
 * printed -- the same case-insensitive search the grabber does, so what
 * this lists is what that would choose, in the order it would choose it.
 */

#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static Display *dpy;
static int      have_arg;
static char    *needle;

static int matches(const char *s)
{
    return !have_arg || (s && strcasestr(s, needle) != NULL);
}

static void walk(Window w, int depth)
{
    Window root, parent, *kids = NULL;
    unsigned n = 0, i;
    XClassHint ch;
    char *name = NULL;

    if (!XQueryTree(dpy, w, &root, &parent, &kids, &n))
        return;

    XFetchName(dpy, w, &name);

    ch.res_name = NULL;
    ch.res_class = NULL;
    XGetClassHint(dpy, w, &ch);

    if (name || ch.res_class)
        if (matches(name) || matches(ch.res_class)) {
            XWindowAttributes wa;

            memset(&wa, 0, sizeof wa);
            XGetWindowAttributes(dpy, w, &wa);
            printf("%*s0x%lx %4dx%-4d at %4d,%-4d depth %2d  name=\"%s\""
                   "  class=\"%s\"%s\n",
                   depth * 2, "", (unsigned long)w,
                   wa.width, wa.height, wa.x, wa.y, wa.depth,
                   name ? name : "", ch.res_class ? ch.res_class : "",
                   wa.map_state == IsViewable ? "" : "  (unmapped)");
        }

    for (i = 0; i < n; i++)
        if (kids[i] != root)
            walk(kids[i], depth + 1);

    if (name) XFree(name);
    if (ch.res_name)   XFree(ch.res_name);
    if (ch.res_class)  XFree(ch.res_class);
    if (kids) XFree(kids);
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        have_arg = 1;
        needle = argv[1];
    }

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "xlist: cannot open display\n");
        return 1;
    }

    walk(DefaultRootWindow(dpy), 0);
    XCloseDisplay(dpy);
    return 0;
}
