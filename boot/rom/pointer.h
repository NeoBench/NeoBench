#ifndef NB_POINTER_H
#define NB_POINTER_H

/*
 * The screen pointer.
 *
 * NeoBench draws the pointer itself rather than programming an AGA
 * sprite: the pointer has to survive on top of a desktop that is
 * re-quantised into 256 colours on every repaint, and a software
 * overlay borrows the frame underneath it, so it can be put back exactly
 * without knowing how the scene was coloured.
 *
 * All of the behaviour -- shape, size, speed, whether it is there at
 * all -- comes from Config/pointer.cfg, loaded before the desktop.
 */

/* Park the pointer where pointer.cfg says and draw it.   */
void nb_pointer_enable(void);

/* The palette changed under it (gfx_present just ran): re-pick the fill
 * and outline entries and redraw. */
void nb_pointer_after_present(void);

/*
 * A band of rows has just been repacked.  The overlay is holding pixels
 * borrowed from the frame that was on screen when it went down: rows the
 * band rewrote no longer hold them, rows the band left alone still do.
 * Called straight after the pack, before anything else touches the
 * frame, it hands back the rows that survived and drops the borrow if
 * any of the overlay's rows were rewritten.
 */
void nb_pointer_present_rows(int y0, int y1);

/*
 * Poll the mouse and move the overlay.  Returns 1 once per press of the
 * left button and 2 once per press of the right, 0 on the fields that
 * carry neither -- which is what the desktop reads as a click and as a
 * context press.
 */
int nb_pointer_frame(void);

/* Current pointer position, in screen pixels. */
int nb_pointer_x(void);
int nb_pointer_y(void);

/* Show or hide the overlay without losing the position. */
void nb_pointer_show(int show);

/* Where the overlay was last drawn, and which palette entries it picked:
 * one serial line, for telling "not drawn" from "drawn somewhere else". */
void nb_pointer_dump(void);

#endif /* NB_POINTER_H */
