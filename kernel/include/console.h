#ifndef CONSOLE_H
#define CONSOLE_H

void console_init(void);
void console_putc(char c);
void console_write(const char *s);

/* Ink colour for subsequent output; NB_COL_* from boot/rom/amiga.h. */
void console_set_color(unsigned idx);

#endif
