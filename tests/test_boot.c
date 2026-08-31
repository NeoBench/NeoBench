#include <stdio.h>
#include <string.h>

/*
 * NeoBench Boot Test — native simulation.
 *
 * Compiles on the host to verify the boot sequence and shell
 * output exactly as it would appear on Amiga serial console.
 *
 * Build:  gcc -o test_boot test_boot.c && ./test_boot
 */

#define NB_COLOR_RESET      "\033[0m"
#define NB_COLOR_BOLD       "\033[1m"
#define NB_COLOR_DIM        "\033[2m"
#define NB_COLOR_RED        "\033[31m"
#define NB_COLOR_GREEN      "\033[32m"
#define NB_COLOR_AMBER      "\033[33m"
#define NB_COLOR_CYAN       "\033[36m"
#define NB_COLOR_WHITE      "\033[37m"
#define NB_COLOR_BRIGHT_RED     "\033[91m"
#define NB_COLOR_BRIGHT_GREEN   "\033[92m"
#define NB_COLOR_BRIGHT_AMBER   "\033[93m"
#define NB_COLOR_BRIGHT_CYAN    "\033[96m"
#define NB_COLOR_BRIGHT_WHITE   "\033[97m"
#define NB_CLS              "\033[2J\033[H"

static void print_status(const char *label, const char *status, const char *color)
{
    int dots;
    int len = strlen(label);
    dots = 48 - len - 8;
    if (dots < 1) dots = 1;

    printf("  %s", label);
    for (int i = 0; i < dots; i++)
        printf(".");
    printf(" %s%s%s\n", color, status, NB_COLOR_RESET);
}

static void print_status36(const char *label, const char *state, const char *color)
{
    int dots;
    int len = strlen(label);
    dots = 36 - len;
    if (dots < 1) dots = 1;

    printf("  %s", label);
    for (int i = 0; i < dots; i++)
        printf(".");
    printf("%s%s%s\n", color, state, NB_COLOR_RESET);
}

static void sh_line(void)
{
    printf("  ─────────────────────────────────────\n");
}

/* ── Boot sequence ──────────────────────────────────────────── */

static void boot_banner(void)
{
    printf(NB_CLS);

    printf("%s╔══════════════════════════════════════════════════════════════╗%s\n",
        NB_COLOR_BRIGHT_CYAN, NB_COLOR_RESET);
    printf("%s║%s%s                                                              %s%s║%s\n",
        NB_COLOR_BRIGHT_CYAN, NB_COLOR_BOLD NB_COLOR_BRIGHT_WHITE, "",
        NB_COLOR_RESET, NB_COLOR_BRIGHT_CYAN, NB_COLOR_RESET);
    printf("%s║%s%s     _   _             ____                  _                %s%s║%s\n",
        NB_COLOR_BRIGHT_CYAN, NB_COLOR_BOLD NB_COLOR_BRIGHT_WHITE, "",
        NB_COLOR_RESET, NB_COLOR_BRIGHT_CYAN, NB_COLOR_RESET);
    printf("%s║%s%s    | \\ | | ___  ___  | __ )  ___ _ __   ___| |__            %s%s║%s\n",
        NB_COLOR_BRIGHT_CYAN, NB_COLOR_BOLD NB_COLOR_BRIGHT_WHITE, "",
        NB_COLOR_RESET, NB_COLOR_BRIGHT_CYAN, NB_COLOR_RESET);
    printf("%s║%s%s    |  \\| |/ _ \\/ _ \\ |  _ \\ / _ \\ '_ \\ / __| '_ \\          %s%s║%s\n",
        NB_COLOR_BRIGHT_CYAN, NB_COLOR_BOLD NB_COLOR_BRIGHT_WHITE, "",
        NB_COLOR_RESET, NB_COLOR_BRIGHT_CYAN, NB_COLOR_RESET);
    printf("%s║%s%s    | |\\  |  __/ (_) || |_) |  __/ | | | (__| | | |          %s%s║%s\n",
        NB_COLOR_BRIGHT_CYAN, NB_COLOR_BOLD NB_COLOR_BRIGHT_WHITE, "",
        NB_COLOR_RESET, NB_COLOR_BRIGHT_CYAN, NB_COLOR_RESET);
    printf("%s║%s%s    |_| \\_|\\___|\\___/ |____/ \\___|_| |_|\\___|_| |_|          %s%s║%s\n",
        NB_COLOR_BRIGHT_CYAN, NB_COLOR_BOLD NB_COLOR_BRIGHT_WHITE, "",
        NB_COLOR_RESET, NB_COLOR_BRIGHT_CYAN, NB_COLOR_RESET);
    printf("%s║%s%s                                                              %s%s║%s\n",
        NB_COLOR_BRIGHT_CYAN, NB_COLOR_BOLD NB_COLOR_BRIGHT_WHITE, "",
        NB_COLOR_RESET, NB_COLOR_BRIGHT_CYAN, NB_COLOR_RESET);
    printf("%s║%s%s                  N E O B E N C H                             %s%s║%s\n",
        NB_COLOR_BRIGHT_CYAN, NB_COLOR_BOLD NB_COLOR_AMBER, "",
        NB_COLOR_RESET, NB_COLOR_BRIGHT_CYAN, NB_COLOR_RESET);
    printf("%s║%s%s              FreeBSD stable/15  /  m68k/68060                %s%s║%s\n",
        NB_COLOR_BRIGHT_CYAN, NB_COLOR_DIM, "",
        NB_COLOR_RESET, NB_COLOR_BRIGHT_CYAN, NB_COLOR_RESET);
    printf("%s╚══════════════════════════════════════════════════════════════╝%s\n",
        NB_COLOR_BRIGHT_CYAN, NB_COLOR_RESET);

    printf("\n");
}

static void boot_config(void)
{
    printf("%s  System Configuration%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    printf("%s  ─────────────────────────────────────────────%s\n", NB_COLOR_WHITE, NB_COLOR_RESET);
    print_status("CPU",          "Motorola MC68060",          NB_COLOR_BRIGHT_WHITE);
    print_status("RAM",          "128 MB Fast + 2 MB Chip",   NB_COLOR_BRIGHT_WHITE);
    print_status("Machine",      "Amiga A4000",               NB_COLOR_BRIGHT_WHITE);
    print_status("Kernel",       "NeoBench r1.0",             NB_COLOR_BRIGHT_WHITE);
    print_status("Arch",         "m68k/68060",                NB_COLOR_BRIGHT_WHITE);
    print_status("Root",         "NBFS",                      NB_COLOR_BRIGHT_WHITE);
    printf("\n");
}

static void boot_modules(void)
{
    printf("%s  Initializing Kernel Modules%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    printf("%s  ─────────────────────────────────────────────%s\n", NB_COLOR_WHITE, NB_COLOR_RESET);

    const char *names[] = {
        "Serial console driver",
        "Amiga custom chipset",
        "CIA interrupt controller",
        "Zorro bus enumeration",
        "Block device layer",
        "NBFS filesystem module",
        "VFS mount root",
        "Process scheduler (4BSD)",
        "Network stack",
        "RTG framebuffer",
        "NeoBench init",
    };
    const char *colors[] = {
        NB_COLOR_GREEN,     NB_COLOR_GREEN,     NB_COLOR_GREEN,
        NB_COLOR_AMBER,     NB_COLOR_GREEN,     NB_COLOR_GREEN,
        NB_COLOR_GREEN,     NB_COLOR_AMBER,     NB_COLOR_RED,
        NB_COLOR_AMBER,     NB_COLOR_GREEN,
    };
    const char *states[] = {
        "[ok]",  "[ok]",  "[ok]",
        "[warn]","[ok]",  "[ok]",
        "[ok]",  "[warn]","[fail]",
        "[warn]","[ok]",
    };

    for (int i = 0; i < 11; i++)
    {
        int len = strlen(names[i]);
        int dots = 48 - len - 8;
        if (dots < 1) dots = 1;

        printf("  %s", names[i]);
        for (int j = 0; j < dots; j++)
            printf(".");
        printf(" %s%s%s\n", colors[i], states[i], NB_COLOR_RESET);
    }
}

static void boot_complete(void)
{
    printf("\n");
    printf("%s%s  Boot complete. Entering NeoBench.%s\n",
        NB_COLOR_BRIGHT_GREEN NB_COLOR_BOLD, "", NB_COLOR_RESET);
}

/* ── Shell commands ─────────────────────────────────────────── */

static void sh_help(void)
{
    printf("\n");
    printf("%s%s  NeoBench Shell v1.0%s\n", NB_COLOR_BOLD NB_COLOR_BRIGHT_CYAN, "", NB_COLOR_RESET);
    printf("\n");
    sh_line();
    printf("%s  System%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    sh_line();
    printf("  nbver        Kernel version and build info\n");
    printf("  nbstatus     Full system status\n");
    printf("  nbcpu        CPU details and cache state\n");
    printf("  nbmem        Memory map and usage\n");
    printf("  nbboot       Boot information from NeoLoader\n");
    printf("\n");
    printf("%s  Hardware%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    sh_line();
    printf("  nbamiga      Amiga custom chip state\n");
    printf("  nbzorro      Zorro bus enumeration\n");
    printf("  nbpci        PCI bus (Mediator) devices\n");
    printf("  nbcia        CIA chip registers\n");
    printf("  nbrtg        RTG framebuffer status\n");
    printf("  nbaudio      Audio subsystem\n");
    printf("\n");
    printf("%s  Filesystem%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    sh_line();
    printf("  nbfs         NBFS filesystem info\n");
    printf("  nbmount      Mount/unmount filesystems\n");
    printf("  nbcheck      Run NBFS consistency check\n");
    printf("\n");
    printf("%s  Kernel%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    sh_line();
    printf("  nbdrivers    List loaded drivers\n");
    printf("  nbmodules    List kernel modules\n");
    printf("  nbconfig     Show/set kernel config\n");
    printf("  nblog        Kernel log buffer\n");
    printf("\n");
    printf("%s  Actions%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    sh_line();
    printf("  nbreboot     Reboot the machine\n");
    printf("  nbhalt       Halt the machine\n");
    printf("  nbpoweroff   Power off (ACPI/CIA)\n");
    printf("  nbclear      Clear screen\n");
    printf("  nbhelp       Show this help\n");
    printf("\n");
}

static void sh_ver(void)
{
    printf("\n");
    printf("%s  NeoBench Kernel%s\n", NB_COLOR_BOLD, NB_COLOR_RESET);
    sh_line();
    printf("  Version ..... %s r1.0 %s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    printf("  Base ........ %s FreeBSD stable/15 %s\n", NB_COLOR_BRIGHT_WHITE, NB_COLOR_RESET);
    printf("  Machine ..... %s m68k/68060 Amiga %s\n", NB_COLOR_BRIGHT_WHITE, NB_COLOR_RESET);
    printf("  Build ....... %s Aug 20 2026 08:43:00 %s\n", NB_COLOR_BRIGHT_WHITE, NB_COLOR_RESET);
    printf("  Compiler .... %s m68k-elf-gcc 16.1.0 %s\n", NB_COLOR_BRIGHT_WHITE, NB_COLOR_RESET);
    printf("\n");
}

static void sh_status(void)
{
    printf("\n");
    printf("%s%s  NeoBench System Status%s\n", NB_COLOR_BRIGHT_GREEN NB_COLOR_BOLD, "", NB_COLOR_RESET);
    sh_line();
    print_status36("CPU",         "Motorola MC68060",       NB_COLOR_BRIGHT_WHITE);
    print_status36("RAM",         "128 MB Fast + 2 MB Chip", NB_COLOR_BRIGHT_WHITE);
    print_status36("Machine",     "Amiga A4000",            NB_COLOR_BRIGHT_WHITE);
    print_status36("Kernel",      "running",                NB_COLOR_BRIGHT_GREEN);
    print_status36("Serial",      "active",                 NB_COLOR_BRIGHT_GREEN);
    print_status36("NBFS",        "mounted (root)",         NB_COLOR_BRIGHT_AMBER);
    print_status36("VFS",         "ready",                  NB_COLOR_BRIGHT_GREEN);
    print_status36("Scheduler",   "4BSD",                   NB_COLOR_BRIGHT_AMBER);
    print_status36("Network",     "not loaded",             NB_COLOR_RED);
    print_status36("RTG",         "framebuffer ready",      NB_COLOR_BRIGHT_AMBER);
    print_status36("Audio",       "not loaded",             NB_COLOR_RED);
    print_status36("USB",         "not loaded",             NB_COLOR_RED);
    printf("\n");
}

static void sh_cpu(void)
{
    printf("\n");
    printf("%s  CPU Information%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    sh_line();
    printf("  Model ....... %s Motorola MC68060 %s\n", NB_COLOR_BRIGHT_WHITE, NB_COLOR_RESET);
    printf("  Clock ....... %s 50 MHz %s\n", NB_COLOR_BRIGHT_WHITE, NB_COLOR_RESET);
    printf("  ISA ......... %s Motorola 68000 %s\n", NB_COLOR_BRIGHT_WHITE, NB_COLOR_RESET);
    printf("  MMU ......... %s 68060 MMU (enabled) %s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    printf("  FPU ......... %s Integrated %s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    printf("  I-Cache ..... %s 8 KB %s\n", NB_COLOR_BRIGHT_WHITE, NB_COLOR_RESET);
    printf("  D-Cache ..... %s 8 KB %s\n", NB_COLOR_BRIGHT_WHITE, NB_COLOR_RESET);
    printf("  Pipeline .... %s Superscalar, dual-issue %s\n", NB_COLOR_BRIGHT_WHITE, NB_COLOR_RESET);
    printf("  Endianness .. %s Big-endian %s\n", NB_COLOR_BRIGHT_WHITE, NB_COLOR_RESET);
    printf("\n");
}

static void sh_mem(void)
{
    printf("\n");
    printf("%s  Memory Map%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    sh_line();
    printf("  Chip RAM .... %s 2 MB     0x00000000 - 0x001FFFFF %s\n", NB_COLOR_BRIGHT_WHITE, NB_COLOR_RESET);
    printf("  Fast RAM ... %s 128 MB   0x00200000 - 0x07FFFFFF %s\n", NB_COLOR_BRIGHT_WHITE, NB_COLOR_RESET);
    printf("  Zorro III ... %s 256 MB   0x40000000 - 0x4FFFFFFF %s\n", NB_COLOR_BRIGHT_WHITE, NB_COLOR_RESET);
    printf("  Kernel ..... %s 30 KB    0x00001000 - 0x00009388 %s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    printf("\n");
    printf("  Total ....... %s 386 MB %s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    printf("\n");
}

static void sh_nbfs(void)
{
    printf("\n");
    printf("%s  NBFS Filesystem%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    sh_line();
    printf("  Version ...... %s 1.0 %s\n", NB_COLOR_BRIGHT_WHITE, NB_COLOR_RESET);
    printf("  Block size ... %s 4096 bytes %s\n", NB_COLOR_BRIGHT_WHITE, NB_COLOR_RESET);
    printf("  Root inode ... %s 2 %s\n", NB_COLOR_BRIGHT_WHITE, NB_COLOR_RESET);
    printf("  Journal ...... %s enabled %s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    printf("  Checksum ..... %s CRC32 %s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    printf("  Mount point .. %s / %s\n", NB_COLOR_BRIGHT_WHITE, NB_COLOR_RESET);
    printf("  Status ....... %s mounted %s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    printf("\n");
}

static void sh_drivers(void)
{
    printf("\n");
    printf("%s  Loaded Drivers%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    sh_line();
    print_status36("neobench",      "nexus platform",   NB_COLOR_BRIGHT_GREEN);
    print_status36("serial",        "CIA B console",    NB_COLOR_BRIGHT_GREEN);
    print_status36("nbfs",          "filesystem",       NB_COLOR_BRIGHT_GREEN);
    print_status36("zorro",         "bus driver",       NB_COLOR_BRIGHT_AMBER);
    print_status36("rtg",           "framebuffer",      NB_COLOR_BRIGHT_AMBER);
    print_status36("ata",           "storage",          NB_COLOR_BRIGHT_AMBER);
    print_status36("audio",         "Paula sound",      NB_COLOR_RED);
    print_status36("net",           "network",          NB_COLOR_RED);
    print_status36("usb",           "USB host",         NB_COLOR_RED);
    printf("\n");
}

static void sh_log(void)
{
    printf("\n");
    printf("%s  Kernel Log%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    sh_line();
    printf("  [boot]  NeoLoader v0.1 started\n");
    printf("  [boot]  Loading kernel ELF... ok\n");
    printf("  [boot]  Jumping to 0x00001000\n");
    printf("  [init]  Serial console driver ... %s[ok]%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    printf("  [init]  Amiga custom chipset .... %s[ok]%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    printf("  [init]  CIA interrupt controller  %s[ok]%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    printf("  [init]  Zorro bus enumeration .. %s[warn]%s\n", NB_COLOR_BRIGHT_AMBER, NB_COLOR_RESET);
    printf("  [init]  Block device layer ..... %s[ok]%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    printf("  [init]  NBFS filesystem module . %s[ok]%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    printf("  [init]  VFS mount root ......... %s[ok]%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    printf("  [init]  Process scheduler (4BSD) %s[warn]%s\n", NB_COLOR_BRIGHT_AMBER, NB_COLOR_RESET);
    printf("  [init]  Network stack ......... %s[fail]%s\n", NB_COLOR_RED, NB_COLOR_RESET);
    printf("  [init]  RTG framebuffer ....... %s[warn]%s\n", NB_COLOR_BRIGHT_AMBER, NB_COLOR_RESET);
    printf("  [init]  NeoBench init ......... %s[ok]%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    printf("  [shell] NeoShell v1.0 ready\n");
    printf("\n");
}

/* ── Main ───────────────────────────────────────────────────── */

int main(void)
{
    /* NeoLoader mini banner */
    printf("%s%s╔══════════════════════════════════════╗%s\n",
        NB_COLOR_BOLD NB_COLOR_BRIGHT_CYAN, "", NB_COLOR_RESET);
    printf("%s%s║        N E O L O A D E R            ║%s\n",
        NB_COLOR_BOLD NB_COLOR_BRIGHT_CYAN, "", NB_COLOR_RESET);
    printf("%s%s║       NeoBench Boot Loader          ║%s\n",
        NB_COLOR_BOLD NB_COLOR_BRIGHT_CYAN, "", NB_COLOR_RESET);
    printf("%s%s╚══════════════════════════════════════╝%s\n",
        NB_COLOR_BOLD NB_COLOR_BRIGHT_CYAN, "", NB_COLOR_RESET);
    printf("\n");
    printf("  Loading NeoBench kernel...\n\n");
    printf("  Kernel ELF ............ %s[ok]%s\n", NB_COLOR_GREEN, NB_COLOR_RESET);
    printf("  Entry point ........... %s[ok]%s\n", NB_COLOR_GREEN, NB_COLOR_RESET);
    printf("  Jumping to kernel ......\n\n");

    /* Kernel boot sequence */
    boot_banner();
    boot_config();
    boot_modules();
    boot_complete();

    /* Shell prompt */
    printf("\n");
    printf("%s%s  NeoShell v1.0%s\n", NB_COLOR_BOLD NB_COLOR_BRIGHT_CYAN, "", NB_COLOR_RESET);
    printf("  Type %s nbhelp %s for NeoBench commands.\n\n", NB_COLOR_BRIGHT_CYAN, NB_COLOR_RESET);

    /* Simulate all commands */
    const char *cmds[] = {
        "nbhelp", "nbver", "nbstatus", "nbcpu", "nbmem",
        "nbfs", "nbdrivers", "nblog", "nbboot",
        "nbamiga", "nbzorro", "nbcia", "nbrtg", "nbaudio",
        "nbcheck", "nbmount", "nbmodules", "nbconfig",
    };
    const char *descs[] = {
        "help", "version", "status", "cpu", "memory",
        "nbfs", "drivers", "log", "boot",
        "amiga", "zorro", "cia", "rtg", "audio",
        "nbfs check", "mount", "modules", "config",
    };

    for (int i = 0; i < 18; i++)
    {
        printf("%sneobench> %s%s\n", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET, cmds[i]);

        if (strcmp(cmds[i], "nbhelp") == 0)        sh_help();
        else if (strcmp(cmds[i], "nbver") == 0)    sh_ver();
        else if (strcmp(cmds[i], "nbstatus") == 0) sh_status();
        else if (strcmp(cmds[i], "nbcpu") == 0)    sh_cpu();
        else if (strcmp(cmds[i], "nbmem") == 0)    sh_mem();
        else if (strcmp(cmds[i], "nbfs") == 0)     sh_nbfs();
        else if (strcmp(cmds[i], "nbdrivers") == 0) sh_drivers();
        else if (strcmp(cmds[i], "nblog") == 0)    sh_log();
    }

    printf("%sneobench> %s", NB_COLOR_BRIGHT_GREEN, NB_COLOR_RESET);
    fflush(stdout);

    return 0;
}
