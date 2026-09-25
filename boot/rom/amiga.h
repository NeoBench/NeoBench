#ifndef NB_AMIGA_H
#define NB_AMIGA_H

/* Target: AGA (A1200/A4000) only. */
#define NB_TARGET_AGA 1

void amiga_serial_init(void);
void amiga_display_init(void);
void amiga_putc(char c);
void amiga_serial_putc(char c);
void amiga_display_clear(void);
void amiga_display_vsync(void);

#endif /* NB_AMIGA_H */
