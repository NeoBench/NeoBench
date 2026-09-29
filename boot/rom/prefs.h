#ifndef NB_PREFS_H
#define NB_PREFS_H

#include <stdint.h>

/*
 * NeoBench preferences, read at boot from Config/ inside the ROM.
 *
 * Defaults live in nb_prefs_defaults() and match what the desktop used
 * before there were files to read; a missing or malformed file leaves
 * that default alone, so a typo in screen.cfg degrades to a bootable
 * desktop rather than to a black screen.
 */
struct nb_prefs
{
    /* screen.cfg */
    unsigned w, h, depth;
    int      lace;
    int      grid;
    int      glow;
    int      taskbar;
    int      bar_style;        /* BAR_AERO (the default) or BAR_CLASSIC */
    unsigned bar_glass;        /* how much of the backdrop shows, 0..100 */
    uint16_t bg_top;
    uint16_t bg_bot;
    char     font[16];        /* "Xen" (standard) or "System"         */

    /* pointer.cfg */
    int      shape;            /* NB_PTR_ARROW..NB_PTR_DOT             */
    int      scale;            /* 1 or 2                               */
    int      speed;            /* pointer pixels per mouse count       */
    int      shadow;
    int      visible;
    uint16_t colour;           /* fill colour, RGB565                  */
    int      start_x, start_y;

    /* sound.cfg */
    int      snd_startup;      /* play the chime on boot               */
    unsigned snd_volume;       /* Paula level, 0..64                   */
    char     snd_file[40];     /* path inside the file store           */

    /* boot.cfg */
    unsigned hold;             /* seconds the boot log is held up      */
    int      hwscan;           /* walk the hardware before binding     */
};

#define NB_PTR_ARROW  0
#define NB_PTR_CROSS  1
#define NB_PTR_IBEAM  2
#define NB_PTR_DOT    3

/* the taskbar's own style: the Aero glass the desktop draws by
 * default, or the flat Workbench field it was drawn with before that */
#define BAR_AERO      0
#define BAR_CLASSIC   1

extern struct nb_prefs nb_prefs;

void nb_prefs_load(void);

/*
 * The parsed values as one serial line each.  Serial only: preferences
 * are a development read-out, not something the boot log should carry.
 */
void nb_prefs_dump(void);

#endif /* NB_PREFS_H */
