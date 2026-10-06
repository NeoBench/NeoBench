#include "../include/kernel.h"
#include "../include/console.h"
#include "../../boot/rom/amiga.h"
#include "../../boot/rom/audio.h"
#include "../../boot/rom/kbd.h"
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
extern void nb_desktop_tick(void);
extern int  nb_desktop_click(int x, int y, int btn);
extern int  nb_desktop_drag(void);
extern int  nb_desktop_key(int c);
extern void nb_pointer_enable(void);
extern void nb_pointer_after_present(void);
extern int  nb_pointer_frame(void);
extern int  nb_pointer_x(void);
extern int  nb_pointer_y(void);
extern void nb_pointer_dump(void);
extern void nb_desktop_dump(void);
extern int  nb_shell_boot(void);

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
    /*
     * Input is armed with the kernel rather than with the desktop,
     * because what it takes away belongs to the ROM underneath: the
     * keyboard's interrupt mask is cleared here, which is the moment the
     * chainloaded system stops seeing keys and NeoBench starts.  It is
     * early and it is deliberately not repeated -- the receivers are
     * polled from the first field onwards, and there is nothing to arm
     * a second time (kbd.c).
     */
    nb_kbd_init();
    nb_kbd_dump();
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
     *
     * The hold is a window as well as a pause: Escape at any point in
     * it brings NeoShell up instead of the compositor, which is the
     * failsafe's manual side -- a machine that will not start its
     * desktop, or that is being asked about rather than used, is
     * reached from here with no tool and no rebuild.  Nothing is lost
     * by a key typed early or late: the receivers are polled for the
     * whole of the hold and the queue holds everything they caught.
     *
     * The other two ways in are Config/boot.cfg's own.  `failsafe = on`
     * boots to the shell without asking, for a machine kept for
     * recovery; and a store with no Config/screen.cfg in it has no
     * desktop to composite -- the wash, the bar and the face all come
     * out of that file -- so the shell is where it goes rather than to
     * a screen nothing can be drawn on.  Either way the shell decides
     * when the desktop comes up: `desktop` at the prompt is what
     * returns here, which is what makes it a place to fall back to
     * rather than a place to be stuck in.
     */
    {
        int shell = 0;

        if (!pfs_find("Config/screen.cfg"))
        {
            kernel_warn("Config/screen.cfg missing -- NeoShell");
            shell = 1;
        }
        else if (nb_prefs.failsafe)
        {
            kernel_ok("Failsafe (Config/boot.cfg) -- NeoShell");
            shell = 1;
        }

        if (!shell && nb_prefs.hold)
        {
            uint32_t t0 = nb_fields;
            int esc = 0;
            int c;

            console_set_color(NB_COL_GREY);
            console_write("         Esc for NeoShell\n");
            console_set_color(NB_COL_GREEN);

            while (nb_fields - t0 < nb_prefs.hold * 50u)
            {
                while (!amiga_vbl_pending())
                    nb_kbd_poll();
                while ((c = nb_kbd_get()) >= 0)
                    if (c == NB_KEY_ESC)
                        esc = 1;
                nb_sound_poll();
                if (esc)
                    break;
            }
            if (esc)
                shell = 1;
        }

        if (shell)
        {
            kernel_target("NeoShell");
            nb_shell_boot();
        }
    }

    /* Desktop scene replaces the boot log on screen. */
    kernel_target("Graphical Interface");
    amiga_serial_putc('G');
    nb_desktop_render();
    nb_pointer_enable();
    nb_pointer_dump();
    amiga_serial_putc('P');

    /*
     * Interactive phase.  The pointer is polled every field; a press --
     * left or right, 1 or 2 -- is handed to the scene, which recomposites
     * only when the press actually changed something: a full present
     * costs a median cut over the whole back buffer, and even a band
     * wants its rows redrawn and repacked, so it is worth being sure.
     *
     * A press that stays down is a different case and is asked for
     * separately, because it lasts longer than the field it arrived on.
     * It picks a window up by its caption and carries it for as long as
     * the button is held; the answer is the same one a press gives --
     * only when the rows on screen would change -- so it takes the same
     * three calls behind it, and a pointer that has not moved between
     * two fields costs the two reads that took to find out.
     */
    for (;;)
    {
        int pressed;
        int c;

        nb_sound_poll();

        /*
         * The wait for the next field is where the receivers are read.
         * Paula carries one received byte and the next arrival overwrites
         * it, so a character typed between two fields has to be taken
         * here rather than 20 ms later, and the keyboard is read in the
         * same loop because the queue is what the two have in common.
         *
         * The one stretch nothing reads is a present, which lasts as long
         * as the repaint behind it: the keyboard's code waits for the
         * handshake either way, and the byte on the wire is the one that
         * can be lost there.
         */
        while (!amiga_vbl_pending())
            nb_kbd_poll();

        /*
         * What was typed, from whichever receiver it came from, handed to
         * the scene as a key.  The scene takes it the way it takes a
         * press and says whether anything changed, so a key that lands
         * where nothing is lit costs no present.
         */
        while ((c = nb_kbd_get()) >= 0)
        {
            if (nb_desktop_key(c))
            {
                nb_desktop_render();
                nb_pointer_after_present();
                nb_desktop_dump();      /* which programs are on screen     */
            }
        }

        pressed = nb_pointer_frame();

        if (pressed &&
            nb_desktop_click(nb_pointer_x(), nb_pointer_y(), pressed))
        {
            nb_desktop_render();
            nb_pointer_after_present();
            nb_desktop_dump();          /* which programs are on screen */
        }

        /*
         * And the press that was still down when this field came
         * round: the window it came down on is carried wherever the
         * pointer has got to, and let go when the button is.
         */
        if (nb_desktop_drag())
        {
            nb_desktop_render();
            nb_pointer_after_present();
            nb_desktop_dump();          /* where the window has got to  */
        }

        /*
         * And the one thing that moves without anybody touching it: the
         * player's film steps on the field counter rather than on a key,
         * so it is asked here, once a field, whether anything is due.  It
         * answers with nothing at all nine fields in ten and paints for
         * itself the tenth, so it owes the loop no decision -- only the
         * knowledge that a field happened.
         */
        nb_desktop_tick();
    }
}
