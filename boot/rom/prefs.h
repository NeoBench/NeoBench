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
    int      backdrop;         /* NB_BD_WASH .. NB_BD_SLATE             */
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
    int      failsafe;         /* boot to NeoShell rather than the desk */
};

#define NB_PTR_ARROW  0
#define NB_PTR_CROSS  1
#define NB_PTR_IBEAM  2
#define NB_PTR_DOT    3

/*
 * The five backdrops, in the order the Preferences pane lays them out.
 * Config/screen.cfg names one with "backdrop = wash" and the pane picks
 * one while the machine is running.  Wash is Windows Vista's own: a
 * deep navy falling to the blue its glass is cut for, the two colours
 * bg_top and bg_bot carry, with the aurora laid over them.  The other
 * four are pairs of their own: four light fields, because a cream, a
 * sky, a peach and a grey-blue want the mark, the wordmark and the
 * chrome over them inked rather than lit.  Which way the artwork goes
 * on any of them is decided by the weight of the top colour, in
 * wallpaper().
 */
#define NB_BD_WASH   0
#define NB_BD_PAPER  1
#define NB_BD_AZURE  2
#define NB_BD_DUSK   3
#define NB_BD_SLATE  4
#define NB_BD_COUNT  5

/* the taskbar's own style: the Aero glass the desktop draws by
 * default, or the flat Workbench field it was drawn with before that */
#define BAR_AERO      0
#define BAR_CLASSIC   1

extern struct nb_prefs nb_prefs;

void nb_prefs_load(void);

/* the backdrop's name as the file and the pane spell it */
const char *nb_bd_name(int i);

/* the same name the other way round, -1 when it names no backdrop */
int nb_bd_index(const char *v);

/* the pair of gradient colours backdrop i is painted with; wash takes
 * the ones Config/screen.cfg carries */
void nb_bd_colours(int i, uint16_t *top, uint16_t *bot);

/*
 * The parsed values as one serial line each.  Serial only: preferences
 * are a development read-out, not something the boot log should carry.
 */
void nb_prefs_dump(void);

#endif /* NB_PREFS_H */
