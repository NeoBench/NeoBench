#ifndef CONSOLE_H
#define CONSOLE_H

void console_init(void);
void console_putc(char c);
void console_write(const char *s);
void console_write_color(const char *color, const char *s);
void console_write_bold(const char *s);

#endif
