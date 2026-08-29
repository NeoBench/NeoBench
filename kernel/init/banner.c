#include <stdint.h>

#include "console.h"
#include "banner.h"
#include "console_color.h"

static void print_status(const char *label, const char *status, const char *color)
{
    int dots;
    int len = 0;
    const char *p = label;
    while (*p++) len++;

    dots = 48 - len - 8;
    if (dots < 1) dots = 1;

    console_write("  ");
    console_write(label);
    for (int i = 0; i < dots; i++)
        console_write(".");
    console_write(" ");
    console_write_color(color, status);
    console_write("\n");
}

void kernel_banner(void)
{
    console_write(NB_CLS);

    /* 3D box-framed logo */
    console_write_color(NB_COLOR_BRIGHT_CYAN,
        "╔══════════════════════════════════════════════════════════════╗\n");
    console_write_color(NB_COLOR_BRIGHT_CYAN, "║");
    console_write(NB_COLOR_BOLD NB_COLOR_BRIGHT_WHITE);
    console_write("                                                              ");
    console_write_color(NB_COLOR_BRIGHT_CYAN, "║\n");

    console_write_color(NB_COLOR_BRIGHT_CYAN, "║");
    console_write(NB_COLOR_BOLD NB_COLOR_BRIGHT_WHITE);
    console_write("     _   _             ____                  _                ");
    console_write_color(NB_COLOR_BRIGHT_CYAN, "║\n");

    console_write_color(NB_COLOR_BRIGHT_CYAN, "║");
    console_write(NB_COLOR_BOLD NB_COLOR_BRIGHT_WHITE);
    console_write("    | \\ | | ___  ___  | __ )  ___ _ __   ___| |__            ");
    console_write_color(NB_COLOR_BRIGHT_CYAN, "║\n");

    console_write_color(NB_COLOR_BRIGHT_CYAN, "║");
    console_write(NB_COLOR_BOLD NB_COLOR_BRIGHT_WHITE);
    console_write("    |  \\| |/ _ \\/ _ \\ |  _ \\ / _ \\ '_ \\ / __| '_ \\          ");
    console_write_color(NB_COLOR_BRIGHT_CYAN, "║\n");

    console_write_color(NB_COLOR_BRIGHT_CYAN, "║");
    console_write(NB_COLOR_BOLD NB_COLOR_BRIGHT_WHITE);
    console_write("    | |\\  |  __/ (_) || |_) |  __/ | | | (__| | | |          ");
    console_write_color(NB_COLOR_BRIGHT_CYAN, "║\n");

    console_write_color(NB_COLOR_BRIGHT_CYAN, "║");
    console_write(NB_COLOR_BOLD NB_COLOR_BRIGHT_WHITE);
    console_write("    |_| \\_|\\___|\\___/ |____/ \\___|_| |_|\\___|_| |_|          ");
    console_write_color(NB_COLOR_BRIGHT_CYAN, "║\n");

    console_write_color(NB_COLOR_BRIGHT_CYAN, "║");
    console_write(NB_COLOR_BOLD NB_COLOR_BRIGHT_WHITE);
    console_write("                                                              ");
    console_write_color(NB_COLOR_BRIGHT_CYAN, "║\n");

    console_write_color(NB_COLOR_BRIGHT_CYAN, "║");
    console_write(NB_COLOR_BOLD NB_COLOR_AMBER);
    console_write("                  N E O B E N C H                             ");
    console_write_color(NB_COLOR_BRIGHT_CYAN, "║\n");

    console_write_color(NB_COLOR_BRIGHT_CYAN, "║");
    console_write(NB_COLOR_DIM);
    console_write("              FreeBSD stable/15  /  m68k/68060                ");
    console_write_color(NB_COLOR_BRIGHT_CYAN, "║\n");

    console_write_color(NB_COLOR_BRIGHT_CYAN,
        "╚══════════════════════════════════════════════════════════════╝\n");

    console_write("\n");

    /* System configuration panel */
    console_write_color(NB_COLOR_BRIGHT_GREEN, "  System Configuration\n");
    console_write(NB_COLOR_WHITE "  ─────────────────────────────────────────────\n" NB_COLOR_RESET);
    print_status("CPU",          "Motorola MC68060",          NB_COLOR_BRIGHT_WHITE);
    print_status("RAM",          "128 MB Fast + 2 MB Chip",   NB_COLOR_BRIGHT_WHITE);
    print_status("Machine",      "Amiga A4000",               NB_COLOR_BRIGHT_WHITE);
    print_status("Kernel",       "NeoBench r1.0",             NB_COLOR_BRIGHT_WHITE);
    print_status("Arch",         "m68k/68060",                NB_COLOR_BRIGHT_WHITE);
    print_status("Root",         "NBFS",                      NB_COLOR_BRIGHT_WHITE);

    console_write("\n");
}

void kernel_module_loading(void)
{
    console_write_color(NB_COLOR_BRIGHT_GREEN, "  Initializing Kernel Modules\n");
    console_write(NB_COLOR_WHITE "  ─────────────────────────────────────────────\n" NB_COLOR_RESET);
}

void kernel_module_begin(const char *name)
{
    int dots;
    int len = 0;
    const char *p = name;
    while (*p++) len++;

    dots = 48 - len - 8;
    if (dots < 1) dots = 1;

    console_write("  ");
    console_write(name);
    for (int i = 0; i < dots; i++)
        console_write(".");
}

void kernel_module_ok(void)
{
    console_write(" ");
    console_write_color(NB_COLOR_GREEN, "[ok]");
    console_write("\n");
}

void kernel_module_fail(void)
{
    console_write(" ");
    console_write_color(NB_COLOR_RED, "[fail]");
    console_write("\n");
}

void kernel_module_warn(void)
{
    console_write(" ");
    console_write_color(NB_COLOR_AMBER, "[warn]");
    console_write("\n");
}

void kernel_boot_complete(void)
{
    console_write("\n");
    console_write_color(NB_COLOR_BRIGHT_GREEN, NB_COLOR_BOLD);
    console_write("  Boot complete. Entering NeoBench.\n");
    console_write(NB_COLOR_RESET);
}
