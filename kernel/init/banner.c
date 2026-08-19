#include <stdint.h>

#include "console.h"
#include "banner.h"

void kernel_banner(void)
{
    console_write("\n");

    console_write(" _   _             ____                  _     \n");
    console_write("| \\ | | ___  ___  | __ )  ___ _ __   ___| |__  \n");
    console_write("|  \\| |/ _ \\/ _ \\ |  _ \\ / _ \\ '_ \\ / __| '_ \\ \n");
    console_write("| |\\  |  __/ (_) || |_) |  __/ | | | (__| | | |\n");
    console_write("|_| \\_|\\___|\\___/ |____/ \\___|_| |_|\\___|_| |_|\n");
    console_write("\n");

    console_write("                 N E O B E N C H\n");
    console_write("              68060 Kernel Starting\n");
    console_write("              =====================\n");
    console_write("\n");

    console_write("SYSTEM SETTINGS\n");
    console_write("----------------\n");
    console_write("CPU:       MC68060\n");
    console_write("RAM:       detecting...\n");
    console_write("VFS:       initializing...\n");
    console_write("NBFS:      initializing...\n");
    console_write("VIDEO:     initializing...\n");
    console_write("AUDIO:     initializing...\n");
    console_write("NETWORK:   initializing...\n");
    console_write("\n");
}
