#include "../include/kernel.h"
#include "../include/console.h"
#include "../../boot/rom/amiga.h"
#include "../../boot/rom/audio.h"
#include "../../boot/rom/prefs.h"
#include "../../boot/rom/pfs.h"

extern void kernel_banner(void);
extern void kernel_detect(void);
extern void kernel_drivers(void);
extern void kernel_ok(const char *msg);
extern void kernel_warn(const char *msg);
extern void kernel_ok_sound(unsigned rate, unsigned vol);
extern void kernel_starting(const char *unit);
extern void kernel_started(const char *unit);
extern void kernel_target(const char *target);
extern void nb_desktop_render(void);
extern int  nb_desktop_click(int x, int y, int btn);
extern void nb_pointer_enable(void);
extern void nb_pointer_after_present(void);
extern int  nb_pointer_frame(void);
extern int  nb_pointer_x(void);
extern int  nb_pointer_y(void);
extern void nb_pointer_dump(void);
extern void nb_desktop_dump(void);

void kernel_main(const nb_bootinfo_t *boot)
{
    /* Boot information will be used later */
    (void)boot;

    /*
     * The console has to be up before anything can be said, so the
     * header is printed first and every unit after it is announced
     * before it runs -- the systemd order, where the first thing the log
     * shows is a job being queued rather than a job already finished.
     */
    amiga_serial_putc('K');
    console_init();
    kernel_banner();
    amiga_serial_putc('B');

    /*
     * Preferences come out of the ROM's own file store first of all,
     * before anything is composited and before the two passes below:
     * Config/boot.cfg is what says whether the hardware is walked at
     * all, and the desktop still gets every value in time because it is
     * the last thing drawn.  The read-out goes to serial only -- it is
     * a development line, not part of the boot log.
     */
    kernel_starting("Load Preferences from Config/");
    nb_prefs_load();
    nb_prefs_dump();
    kernel_started("Load Preferences from Config/");

    kernel_starting("NeoBench Kernel Initialisation");
    nb_sound_init();
    amiga_serial_putc('C');
    kernel_started("NeoBench Kernel Initialisation");

    kernel_starting("Detect Hardware");
    kernel_detect();
    kernel_started("Detect Hardware");
    kernel_target("NeoBench Hardware");
    amiga_serial_putc('D');

    /*
     * The device walk is a preference rather than a fixed step: a
     * machine that wants the log quicker sets scan = off in
     * Config/boot.cfg and gets the one amber line that says so, which
     * is honest about there being no drivers bound instead of quiet
     * about it.
     */
    if (nb_prefs.hwscan)
    {
        kernel_starting("NeoBench Device Drivers");
        kernel_drivers();
        kernel_started("NeoBench Device Drivers");
        kernel_target("NeoBench Devices");
    }
    else
        kernel_warn("Hardware scan disabled by Config/boot.cfg");
    amiga_serial_putc('R');

    /*
     * The startup chime is armed here rather than after the desktop is
     * presented, so Paula is already fetching through the one stretch of
     * boot where the machine looks idle -- the compositor's median cut
     * over the whole back buffer.  The sample comes out of the file
     * store and is copied into chip RAM on the way in: Paula is a DMA
     * master and reads neither the ROM image nor fast RAM.
     *
     * A unit that cannot start prints no second line: the amber tag is
     * the hole, exactly as for a job that never finished.
     */
    if (nb_prefs.snd_startup)
    {
        const struct pfs_node *n = pfs_find(nb_prefs.snd_file);
        unsigned rate = 0;

        kernel_starting("Play Startup Chime");
        if (n && !n->dir)
            rate = nb_sound_play(n->data, n->size, nb_prefs.snd_volume);
        if (rate)
        {
            kernel_ok_sound(rate, nb_prefs.snd_volume);
            kernel_started("Play Startup Chime");
        }
        else
            kernel_warn("No startup chime in the file store");
    }
    else
        kernel_ok("Startup chime disabled by Config/sound.cfg");

    /*
     * The log is then held before the desktop takes the screen over --
     * three seconds by default, from Config/boot.cfg.  The last tags
     * stay legible, the chime is heard out over them rather than cut
     * off by the first composed frame, and the machine reads as having
     * come up rather than as having jumped.  The wait is on the field
     * counter rather than on a loop of no-ops, so it is three seconds
     * however long a frame takes to compose, and the sound is polled
     * through it because that is what the main loop below would do.
     */
    if (nb_prefs.hold)
    {
        uint32_t t0 = nb_fields;

        while (nb_fields - t0 < nb_prefs.hold * 50u)
            nb_sound_poll();
    }

    /* Desktop scene replaces the boot log on screen. */
    kernel_target("Graphical Interface");
    amiga_serial_putc('G');
    nb_desktop_render();
    nb_pointer_enable();
    nb_pointer_dump();
    amiga_serial_putc('P');

    /*
     * Interactive phase.  The pointer is polled every field; a press -- * left or right, 1 or 2 -- is handed to the scene, which recomposites
     * only when the press actually changed something: a full present
     * costs a median cut over the whole back buffer, and even a band
     * wants its rows redrawn and repacked, so it is worth being sure.
     */
    for (;;)
    {
        int pressed;

        nb_sound_poll();
        pressed = nb_pointer_frame();

        if (pressed &&
            nb_desktop_click(nb_pointer_x(), nb_pointer_y(), pressed))
        {
            nb_desktop_render();
            nb_pointer_after_present();
            nb_desktop_dump();      /* which programs are on screen     */
        }

        amiga_display_vsync();
    }
}
