/*
 * xgrab -- capture an X window to a PPM, so what NeoBench put on the
 * screen can be looked at rather than described from a serial log.
 *
 *   gcc -O2 -o xgrab tools/xgrab.c -lX11
 *   ./xgrab FS-UAE /tmp/opencode/shot.ppm      a window whose name matches
 *   ./xgrab -       /tmp/opencode/shot.ppm      the root window instead
 *
 * The window tree is walked for the first name that contains the needle
 * (case-insensitively, so "fs-uae" and "FS-UAE" are the same search),
 * and that window's contents are pulled back with XGetImage.  The
 * geometry it took is printed to stderr: the emulator's window is the
 * raster doubled, so knowing where it starts is the difference between
 * cropping NeoBench's 640x512 and cropping somebody's panel.
 *
 * ZPixmap is what every server can hand back and XGetPixel reads it on
 * TrueColor and PseudoColor alike, so this does not care how deep the
 * display is; the channel widths come out of the image's own masks
 * rather than out of a guess about the visual.
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static Display   *dpy;
static Window     found;
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

/* the position of the least significant set bit, and how many there are */
static int shift_of(unsigned long m)
{
    int s = 0;

    if (m == 0)
        return 0;
    while ((m & 1) == 0) { m >>= 1; s++; }
    return s;
}

static int bits_of(unsigned long m)
{
    int n = 0;

    while (m & 1) { m >>= 1; n++; }
    return n;
}

static unsigned char channel(unsigned long p, unsigned long mask)
{
    int s, b;
    unsigned long v;

    if (mask == 0)
        return 0;
    s = shift_of(mask);
    b = bits_of(mask);
    v = (p & mask) >> s;
    if (b >= 8)
        v >>= (b - 8);
    else if (b > 0)
        v = v * 255u / ((1u << b) - 1u);
    if (v > 255)
        v = 255;
    return (unsigned char)v;
}

int main(int argc, char **argv)
{
    Window target;
    XWindowAttributes wa;
    XImage *im;
    FILE *f;
    int x, y;

    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <name substring | -> <out.ppm>\n",
                argv[0]);
        return 2;
    }

    dpy = XOpenDisplay(NULL);
    if (!dpy)
    {
        fprintf(stderr, "xgrab: no display\n");
        return 1;
    }

    needle = argv[1];
    if (needle[0] == '-' && needle[1] == '\0')
        target = DefaultRootWindow(dpy);
    else
    {
        walk(DefaultRootWindow(dpy), 0);
        if (!found)
        {
            fprintf(stderr, "xgrab: no window named *%s*\n", needle);
            XCloseDisplay(dpy);
            return 1;
        }
        target = found;
    }

    if (!XGetWindowAttributes(dpy, target, &wa))
    {
        fprintf(stderr, "xgrab: cannot read the window\n");
        XCloseDisplay(dpy);
        return 1;
    }

    im = XGetImage(dpy, target, 0, 0,
                   (unsigned)wa.width, (unsigned)wa.height,
                   AllPlanes, ZPixmap);
    if (!im)
    {
        fprintf(stderr, "xgrab: XGetImage failed (window not viewable?)\n");
        XCloseDisplay(dpy);
        return 1;
    }

    fprintf(stderr, "xgrab: %dx%d at %d,%d -> %s\n",
            im->width, im->height, wa.x, wa.y, argv[2]);

    f = fopen(argv[2], "wb");
    if (!f)
    {
        perror("xgrab");
        XDestroyImage(im);
        XCloseDisplay(dpy);
        return 1;
    }

    fprintf(f, "P6\n%d %d\n255\n", im->width, im->height);
    for (y = 0; y < im->height; y++)
        for (x = 0; x < im->width; x++)
        {
            unsigned long p = XGetPixel(im, x, y);
            unsigned char rgb[3];

            rgb[0] = channel(p, im->red_mask);
            rgb[1] = channel(p, im->green_mask);
            rgb[2] = channel(p, im->blue_mask);
            fwrite(rgb, 1, 3, f);
        }

    fclose(f);
    XDestroyImage(im);
    XCloseDisplay(dpy);
    return 0;
}
