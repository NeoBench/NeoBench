#ifndef NEOBENCH_CONSOLE_COLOR_H
#define NEOBENCH_CONSOLE_COLOR_H

/*
 * ANSI escape sequences for serial console output.
 *
 * Used by the NeoBench boot display to show colored status indicators
 * on Amiga serial port and FS-UAE serial console.
 */

#define NB_COLOR_RESET      "\033[0m"
#define NB_COLOR_BOLD       "\033[1m"
#define NB_COLOR_DIM        "\033[2m"

#define NB_COLOR_BLACK      "\033[30m"
#define NB_COLOR_RED        "\033[31m"
#define NB_COLOR_GREEN      "\033[32m"
#define NB_COLOR_AMBER      "\033[33m"
#define NB_COLOR_BLUE       "\033[34m"
#define NB_COLOR_MAGENTA    "\033[35m"
#define NB_COLOR_CYAN       "\033[36m"
#define NB_COLOR_WHITE      "\033[37m"

#define NB_COLOR_BRIGHT_RED     "\033[91m"
#define NB_COLOR_BRIGHT_GREEN   "\033[92m"
#define NB_COLOR_BRIGHT_AMBER   "\033[93m"
#define NB_COLOR_BRIGHT_CYAN    "\033[96m"
#define NB_COLOR_BRIGHT_WHITE   "\033[97m"

/* Screen control */
#define NB_CLS              "\033[2J\033[H"
#define NB_CURSOR_HOME      "\033[H"

#endif
