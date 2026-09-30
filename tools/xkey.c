/*
 * xkey -- press keys into the emulator's window from the host, so the
 * keyboard path can be driven from somewhere other than the serial port.
 *
 *   gcc -O2 -o xkey tools/xkey.c -lX11 -l:libXtst.so.6 -L<libdir>
 *   LD_LIBRARY_PATH=<libdir> ./xkey fs-uae Down Return a
 *
 * Each argument after the window's name is one key: a single character is
 * sent as that character, with Shift held when the character needs it,
 * and anything else is looked up as an X keysym name -- "Return",
 * "Escape", "Up", "Super_L".  The window is focused first, because a
 * window without the focus gets nothing from XTEST: the events go where
 * the server puts keyboard input, which is what a real keyboard does too.
 *
 * Nothing here claims what the machine received.  The window is focused
 * and verified to be focused before a single key goes out, so keystrokes
 * cannot land in somebody else's terminal, and what the emulator turned
 * Super_L into -- the Amiga key, or nothing -- is answered by the
 * keyboard driver's own `>key ... raw=$xx' line in the serial log rather
 * than by anything guessed here.
 *
 * libXtst lives only in the Steam runtime on this machine, which is why
 * the library path is an argument to the link and to the run rather than
 * something this file could name.
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define PRESS_MS 30                /* down before up: one key, not two    */
#define GAP_MS   80                /* between keys: a frame and a bit     */

static Display    *dpy;
static Window      found;
static const char *needle;

static int has(const char *s, const char *sub)
{
    size_t n = strlen(sub);

    if (*sub == '\0')
        return 1;
    for (; *s; s++)
        if (strncasecmp(s, sub, n) == 0)
            return 1;
    return 0;
}

static void walk(Window w, int depth)
{
    Window root, parent, *kids = NULL;
    unsigned n = 0, i;

    if (found)
        return;

    if (depth > 0)
    {
        char *name = NULL;

        if (XFetchName(dpy, w, &name) && name)
        {
            if (has(name, needle))
            {
                found = w;
                XFree(name);
                return;
            }
            XFree(name);
        }
    }

    if (!XQueryTree(dpy, w, &root, &parent, &kids, &n))
        return;
    for (i = 0; i < n && !found; i++)
        walk(kids[i], depth + 1);
    if (kids)
        XFree(kids);
}

/* Is `w' the window, or a child of it?  The focus may land on a child
 * that the window manager or the toolkit put in front of it. */
static int ours(Window w)
{
    Window root, parent, *kids = NULL;
    unsigned n = 0;

    for (;;)
    {
        if (w == found)
            return 1;
        if (w == None || w == PointerRoot)
            return 0;
        if (!XQueryTree(dpy, w, &root, &parent, &kids, &n))
            return 0;
        if (kids)
            XFree(kids);
        if (parent == w || parent == None)
            return 0;
        w = parent;
    }
}

/*
 * One key: down, a moment, up -- with Shift round it when the character
 * is the shifted one on that key.
 *
 * A keysym is a character and a keycode is a key, and `A' and `a' are the
 * same key with one of them held down: level 0 of the keycode is what it
 * does on its own and level 1 what it does with Shift, so which of the
 * two the machine is meant to see is answered by looking rather than by
 * assuming a layout.
 */
static int tap(KeySym ks, const char *label, KeyCode shift_kc)
{
    KeyCode kc;
    KeySym l0, l1;
    int shift = 0;

    if (ks == NoSymbol)
    {
        fprintf(stderr, "xkey: %s: no such keysym\n", label);
        return 1;
    }

    kc = XKeysymToKeycode(dpy, ks);
    if (!kc)
    {
        fprintf(stderr, "xkey: %s: no keycode for it on this keyboard\n",
                label);
        return 1;
    }

    l0 = XkbKeycodeToKeysym(dpy, kc, 0, 0);
    l1 = XkbKeycodeToKeysym(dpy, kc, 0, 1);
    if (ks == l1 && ks != l0)
        shift = 1;
    else if (ks != l0)
        fprintf(stderr, "xkey: %s: keycode %u gives $%lx, sending unshifted\n",
                label, (unsigned)kc, (unsigned long)l0);

    if (shift)
        XTestFakeKeyEvent(dpy, shift_kc, True, 0);
    XTestFakeKeyEvent(dpy, kc, True, 0);
    XFlush(dpy);
    usleep(PRESS_MS * 1000);
    XTestFakeKeyEvent(dpy, kc, False, 0);
    if (shift)
        XTestFakeKeyEvent(dpy, shift_kc, False, 0);
    XFlush(dpy);
    usleep(GAP_MS * 1000);

    fprintf(stderr, "xkey: %s keycode %u%s\n", label, (unsigned)kc,
            shift ? " (shifted)" : "");
    return 0;
}

int main(int argc, char **argv)
{
    Window target;
    Window focus = None;
    XWindowAttributes wa;
    KeyCode shift_kc;
    int i, bad = 0;

    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <window substring> <key> [<key> ...]\n",
                argv[0]);
        return 2;
    }

    dpy = XOpenDisplay(NULL);
    if (!dpy)
    {
        fprintf(stderr, "xkey: no display\n");
        return 1;
    }

    needle = argv[1];
    walk(DefaultRootWindow(dpy), 0);
    if (!found)
    {
        fprintf(stderr, "xkey: no window named *%s*\n", needle);
        XCloseDisplay(dpy);
        return 1;
    }
    target = found;

    if (!XGetWindowAttributes(dpy, target, &wa))
    {
        fprintf(stderr, "xkey: cannot read the window\n");
        XCloseDisplay(dpy);
        return 1;
    }

    /*
     * Focus it, raise it, and then *check* -- because the alternative to
     * a keystroke going nowhere is a keystroke going somewhere else, and
     * a key typed into somebody else's terminal is not a failed test, it
     * is a broken machine.
     */
    XRaiseWindow(dpy, target);
    XSetInputFocus(dpy, target, RevertToParent, CurrentTime);
    XFlush(dpy);
    usleep(200000);
    XGetInputFocus(dpy, &focus, &i);
    if (!ours(focus))
    {
        fprintf(stderr, "xkey: window %dx%d is not the focus "
                "(focus is $%lx); nothing sent\n",
                wa.width, wa.height, (unsigned long)focus);
        XCloseDisplay(dpy);
        return 1;
    }

    fprintf(stderr, "xkey: window %dx%d at %d,%d\n",
            wa.width, wa.height, wa.x, wa.y);

    shift_kc = XKeysymToKeycode(dpy, XK_Shift_L);

    for (i = 2; i < argc; i++)
    {
        KeySym ks;
        const char *a = argv[i];

        if (a[0] != '\0' && a[1] == '\0' && (unsigned char)a[0] >= 0x20 &&
            (unsigned char)a[0] < 0x7f)
            ks = (KeySym)(unsigned char)a[0];   /* ASCII codes are keysyms */
        else
            ks = XStringToKeysym(a);

        bad |= tap(ks, a, shift_kc);
    }

    XCloseDisplay(dpy);
    return bad;
}
