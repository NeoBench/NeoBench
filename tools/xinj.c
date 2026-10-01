/*
 * xinj -- drive the host pointer over the emulator window from a script.
 *
 * NeoBench's input comes from the emulated mouse (JOY0DAT and the CIA
 * button line), and nothing else in this environment can move a host
 * pointer, so this is how the click paths get tested:
 *
 *   xinj geo                 window and client geometry, for calibrating
 *   xinj move  <x> <y>       absolute host coordinates
 *   xinj emu   <x> <y>       display coordinates (640x512) using the
 *                            origin/scale from $XINJ_EMU
 *   xinj click <x> <y>       move, press, release
 *   xinj eclick <x> <y>      the same in display coordinates
 *
 * $XINJ_EMU is "origin_x,origin_y,scale" as measured from a screenshot.
 *
 * Build:  cc -o xinj xinj.c -lX11 -lXtst
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>

#define TITLE "NeoBench"

static Display *dpy;

static Window find_window(Window root)
{
    Atom net_list = XInternAtom(dpy, "_NET_CLIENT_LIST", True);
    Atom net_name = XInternAtom(dpy, "_NET_WM_NAME", True);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", True);
    Window root_ret, parent, *kids = NULL;
    unsigned n = 0, i;
    Window found = 0;

    if (net_list != None)
    {
        Atom type;
        int fmt;
        unsigned long nitems, after;
        unsigned char *data = NULL;

        if (XGetWindowProperty(dpy, root, net_list, 0, 1024, False,
                               XA_WINDOW, &type, &fmt, &nitems, &after,
                               &data) == Success && data)
        {
            Window *wins = (Window *)data;

            n = (unsigned)nitems;
            for (i = 0; i < n && !found; i++)
            {
                if (net_name != None)
                {
                    Atom t;
                    int f;
                    unsigned long ni, a2;
                    unsigned char *name = NULL;

                    if (XGetWindowProperty(dpy, wins[i], net_name, 0, 256,
                                           False, utf8, &t, &f, &ni, &a2,
                                           &name) == Success && name)
                    {
                        if (strstr((char *)name, TITLE))
                            found = wins[i];
                        XFree(name);
                    }
                }
            }
            XFree(data);
        }
    }

    if (found)
        return found;

    /* fall back: walk the tree for a top level carrying the name */
    if (!XQueryTree(dpy, root, &root_ret, &parent, &kids, &n))
        return None;
    for (i = 0; i < n && !found; i++)
    {
        char *name = NULL;

        if (XFetchName(dpy, kids[i], &name) && name)
        {
            if (strstr(name, TITLE))
                found = kids[i];
            XFree(name);
        }
    }
    if (kids)
        XFree(kids);
    return found;
}

static void list_walk(Window w, int depth)
{
    Window root_ret, parent, *kids = NULL;
    unsigned n = 0, i;
    XWindowAttributes wa;
    char *name = NULL;

    if (!XGetWindowAttributes(dpy, w, &wa))
        return;
    XFetchName(dpy, w, &name);
    if (depth > 0 || (name && name[0]))
        printf("%*s%lu  %d,%d %dx%d  %s\n", depth * 2, "",
               (unsigned long)w, wa.x, wa.y, wa.width, wa.height,
               name ? name : "-");
    if (name)
        XFree(name);

    if (depth >= 4 || !XQueryTree(dpy, w, &root_ret, &parent, &kids, &n))
        return;
    for (i = 0; i < n; i++)
        list_walk(kids[i], depth + 1);
    if (kids)
        XFree(kids);
}

static void list_windows(void)
{
    list_walk(DefaultRootWindow(dpy), 0);
}

static void geo(void)
{
    Window root = DefaultRootWindow(dpy);
    Window win = find_window(root);
    XWindowAttributes wa;

    if (!win)
    {
        printf("xinj: no window titled %s\n", TITLE);
        return;
    }
    if (!XGetWindowAttributes(dpy, win, &wa))
    {
        printf("xinj: cannot read attributes\n");
        return;
    }
    printf("window %lu at %d,%d size %dx%d depth %d\n",
           (unsigned long)win, wa.x, wa.y, wa.width, wa.height, wa.depth);
    printf("emu origin %d,%d scale %.4f,%.4f\n",
           wa.x, wa.y, wa.width / 640.0, wa.height / 512.0);
}

static void warp(int x, int y)
{
    XTestFakeMotionEvent(dpy, DefaultScreen(dpy), x, y, 0);
    XFlush(dpy);
}

static void button(int press)
{
    XTestFakeButtonEvent(dpy, 1, press, 0);
    XFlush(dpy);
}

static int emu_point(const char *sx, const char *sy, int *ox, int *oy)
{
    const char *env = getenv("XINJ_EMU");
    double x = atof(sx), y = atof(sy);
    double ox0 = 0, oy0 = 0, sc = 2.0;

    if (!env || sscanf(env, "%lf,%lf,%lf", &ox0, &oy0, &sc) != 3)
    {
        fprintf(stderr, "xinj: set XINJ_EMU=origin_x,origin_y,scale\n");
        return 0;
    }
    *ox = (int)(ox0 + x * sc + 0.5);
    *oy = (int)(oy0 + y * sc + 0.5);
    return 1;
}

int main(int argc, char **argv)
{
    int x, y;

    dpy = XOpenDisplay(NULL);
    if (!dpy)
    {
        fprintf(stderr, "xinj: cannot open display\n");
        return 1;
    }
    if (argc < 2)
    {
        fprintf(stderr, "usage: xinj geo|move|emu|click|eclick ...\n");
        return 2;
    }

    if (!strcmp(argv[1], "geo"))
        geo();
    else if (!strcmp(argv[1], "list"))
        list_windows();
    else if (!strcmp(argv[1], "move") && argc == 4)
        warp(atoi(argv[2]), atoi(argv[3]));
    else if (!strcmp(argv[1], "click") && argc == 4)
    {
        warp(atoi(argv[2]), atoi(argv[3]));
        XSync(dpy, False);
        button(1); XSync(dpy, False);
        button(0); XSync(dpy, False);
    }
    else if (!strcmp(argv[1], "emu") && argc == 4)
    {
        if (emu_point(argv[2], argv[3], &x, &y))
            warp(x, y);
    }
    else if (!strcmp(argv[1], "eclick") && argc == 4)
    {
        if (emu_point(argv[2], argv[3], &x, &y))
        {
            warp(x, y);
            XSync(dpy, False);
            button(1); XSync(dpy, False);
            button(0); XSync(dpy, False);
        }
    }
    else
    {
        fprintf(stderr, "xinj: bad arguments\n");
        return 2;
    }

    XCloseDisplay(dpy);
    return 0;
}
