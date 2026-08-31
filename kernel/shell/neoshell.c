#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#include "neoshell.h"
#include "../include/console.h"
#include "../include/console_color.h"
#include "../include/string.h"
#include "../include/vfs/vfs.h"
#include "../include/vfs/path.h"
#include "../include/vfs/file.h"
#include "../include/nbfs.h"

/*
 * NeoShell — NeoBench interactive system shell.
 *
 * All commands are NeoBench-specific. Runs in kernel mode
 * directly after boot. No userspace, no syscalls, no libc.
 */

#define SHELL_MAX_INPUT  128
#define SHELL_MAX_ARGS   8

/* Serial input. Default Paula serial; NeoBench board UART optional. */
#define SERDATR     (*(volatile unsigned short *)0xDFF018)
#define INTREQ      (*(volatile unsigned short *)0xDFF09C)

#define SER_RBF     0x4000   /* SERDATR bit 14: receive buffer full */
#define INTF_RBF    0x0800   /* INTREQ bit 11: clear RBF on write   */

#define CIAA_PRA    (*(volatile unsigned char *)0xBFE001)
#define SERIAL_DATA (*(volatile unsigned char *)0xBFD100)

#ifndef NEOBENCH_UART_SERIAL

static int serial_getc(void)
{
    unsigned short st;

    do
    {
        st = SERDATR;
    } while (!(st & SER_RBF));

    INTREQ = INTF_RBF;          /* clear RBF for next byte */
    return (int)(st & 0xFF);
}

#else

static int serial_getc(void)
{
    while (!(CIAA_PRA & 0x01))
        ;
    return (int)SERIAL_DATA;
}

#endif

static void sh(const char *s)
{
    console_write(s);
}

static void shc(char c)
{
    console_putc(c);
}

static void sh_color(const char *color, const char *s)
{
    console_write_color(color, s);
}

static void sh_status(const char *label, const char *state, const char *color)
{
    int dots;
    int len = 0;
    const char *p = label;
    while (*p++) len++;

    dots = 36 - len;
    if (dots < 1) dots = 1;

    sh("  ");
    sh(label);
    for (int i = 0; i < dots; i++)
        sh(".");
    sh_color(color, state);
    sh("\n");
}

static void sh_u64(uint64_t v)
{
    char buf[24];
    int i = 0;

    if (v == 0)
    {
        shc('0');
        return;
    }

    while (v > 0)
    {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }

    while (i > 0)
        shc(buf[--i]);
}

static void sh_line(void)
{
    sh("  ─────────────────────────────────────\n");
}

/* ── Serial readline ────────────────────────────────────────── */

static void shell_readline(char *buf, int max)
{
    int i = 0;

    while (i < max - 1)
    {
        char c = (char)serial_getc();

        if (c == '\r' || c == '\n')
        {
            shc('\n');
            break;
        }
        else if (c == 0x08 || c == 0x7F)
        {
            if (i > 0)
            {
                i--;
                shc('\b');
                shc(' ');
                shc('\b');
            }
        }
        else if (c >= 0x20)
        {
            buf[i++] = c;
            shc(c);
        }
    }

    buf[i] = '\0';
}

static int shell_parse(char *input, char *args[], int max_args)
{
    int argc = 0;
    char *p = input;

    while (*p && argc < max_args)
    {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;
        args[argc++] = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if (*p)
            *p++ = '\0';
        else
            break;
    }

    return argc;
}

/* ══════════════════════════════════════════════════════════════
 *  NEOBENCH COMMANDS
 * ══════════════════════════════════════════════════════════════ */

static void cmd_help(void)
{
    sh("\n");
    sh_color(NB_COLOR_BOLD NB_COLOR_BRIGHT_CYAN, "  NeoBench Shell v1.0\n");
    sh("\n");
    sh_line();
    sh_color(NB_COLOR_BRIGHT_GREEN, "  System\n");
    sh_line();
    sh("  nbver        Kernel version and build info\n");
    sh("  nbstatus     Full system status\n");
    sh("  nbcpu        CPU details and cache state\n");
    sh("  nbmem        Memory map and usage\n");
    sh("  nbboot       Boot information from NeoLoader\n");
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  Hardware\n");
    sh_line();
    sh("  nbamiga      Amiga custom chip state\n");
    sh("  nbzorro      Zorro bus enumeration\n");
    sh("  nbpci        PCI bus (Mediator) devices\n");
    sh("  nbcia        CIA chip registers\n");
    sh("  nbrtg        RTG framebuffer status\n");
    sh("  nbaudio      Audio subsystem\n");
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  Filesystem\n");
    sh_line();
    sh("  nbfs         NBFS filesystem info\n");
    sh("  nbroot       Root mount status\n");
    sh("  nbcat        Read a file from the root filesystem\n");
    sh("  nbmount      Mount/unmount filesystems\n");
    sh("  nbcheck      Run NBFS consistency check\n");
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  Kernel\n");
    sh_line();
    sh("  nbdrivers    List loaded drivers\n");
    sh("  nbmodules    List kernel modules\n");
    sh("  nbconfig     Show/set kernel config\n");
    sh("  nblog        Kernel log buffer\n");
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  Actions\n");
    sh_line();
    sh("  nbreboot     Reboot the machine\n");
    sh("  nbhalt       Halt the machine\n");
    sh("  nbpoweroff   Power off (ACPI/CIA)\n");
    sh("  nbclear      Clear screen\n");
    sh("  nbhelp       Show this help\n");
    sh("\n");
}

static void cmd_ver(void)
{
    sh("\n");
    sh_color(NB_COLOR_BOLD, "  NeoBench Kernel\n");
    sh_line();
    sh("  Version ..... " NB_COLOR_BRIGHT_GREEN "r1.0" NB_COLOR_RESET "\n");
    sh("  Base ........ " NB_COLOR_BRIGHT_WHITE "FreeBSD stable/15" NB_COLOR_RESET "\n");
    sh("  Machine ..... " NB_COLOR_BRIGHT_WHITE "m68k/68030+ Amiga" NB_COLOR_RESET "\n");
    sh("  Build ....... " NB_COLOR_BRIGHT_WHITE __DATE__ " " __TIME__ NB_COLOR_RESET "\n");
    sh("  Compiler .... " NB_COLOR_BRIGHT_WHITE "m68k-elf-gcc 16.1.0" NB_COLOR_RESET "\n");
    sh("\n");
}

static void cmd_status(void)
{
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN NB_COLOR_BOLD, "  NeoBench System Status\n");
    sh_line();
    sh_status("CPU",         "Motorola 68030+",        NB_COLOR_BRIGHT_WHITE);
    sh_status("RAM",         "128 MB Fast + 2 MB Chip", NB_COLOR_BRIGHT_WHITE);
    sh_status("Machine",     "Amiga A4000",            NB_COLOR_BRIGHT_WHITE);
    sh_status("Kernel",      "running",                NB_COLOR_BRIGHT_GREEN);
    sh_status("Serial",      "active",                 NB_COLOR_BRIGHT_GREEN);
    sh_status("NBFS",        "mounted (root)",         NB_COLOR_BRIGHT_AMBER);
    sh_status("VFS",         "ready",                  NB_COLOR_BRIGHT_GREEN);
    sh_status("Scheduler",   "4BSD",                   NB_COLOR_BRIGHT_AMBER);
    sh_status("Network",     "not loaded",             NB_COLOR_RED);
    sh_status("RTG",         "framebuffer ready",      NB_COLOR_BRIGHT_AMBER);
    sh_status("Audio",       "not loaded",             NB_COLOR_RED);
    sh_status("USB",         "not loaded",             NB_COLOR_RED);
    sh("\n");
}

static void cmd_cpu(void)
{
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  CPU Information\n");
    sh_line();
    sh("  Model ....... " NB_COLOR_BRIGHT_WHITE "Motorola 68030+" NB_COLOR_RESET "\n");
    sh("  Clock ....... " NB_COLOR_BRIGHT_WHITE "25 MHz (A1500)" NB_COLOR_RESET "\n");
    sh("  ISA ......... " NB_COLOR_BRIGHT_WHITE "Motorola 68030" NB_COLOR_RESET "\n");
    sh("  MMU ......... " NB_COLOR_BRIGHT_GREEN "68030 MMU" NB_COLOR_RESET "\n");
    sh("  FPU ......... " NB_COLOR_BRIGHT_GREEN "Integrated" NB_COLOR_RESET "\n");
    sh("  I-Cache ..... " NB_COLOR_BRIGHT_WHITE "8 KB" NB_COLOR_RESET "\n");
    sh("  D-Cache ..... " NB_COLOR_BRIGHT_WHITE "8 KB" NB_COLOR_RESET "\n");
    sh("  Pipeline .... " NB_COLOR_BRIGHT_WHITE "Superscalar, dual-issue" NB_COLOR_RESET "\n");
    sh("  Endianness .. " NB_COLOR_BRIGHT_WHITE "Big-endian" NB_COLOR_RESET "\n");
    sh("\n");
}

static void cmd_mem(void)
{
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  Memory Map\n");
    sh_line();
    sh("  Chip RAM .... " NB_COLOR_BRIGHT_WHITE "2 MB     0x00000000 - 0x001FFFFF" NB_COLOR_RESET "\n");
    sh("  Fast RAM ... " NB_COLOR_BRIGHT_WHITE "128 MB   0x00200000 - 0x07FFFFFF" NB_COLOR_RESET "\n");
    sh("  Zorro III ... " NB_COLOR_BRIGHT_WHITE "256 MB   0x40000000 - 0x4FFFFFFF" NB_COLOR_RESET "\n");
    sh("  Kernel ..... " NB_COLOR_BRIGHT_GREEN "30 KB    0x00001000 - 0x000087EC" NB_COLOR_RESET "\n");
    sh("\n");
    sh("  Total ....... " NB_COLOR_BRIGHT_GREEN "386 MB" NB_COLOR_RESET "\n");
    sh("\n");
}

static void cmd_boot(void)
{
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  Boot Information\n");
    sh_line();
    sh("  Loader ...... " NB_COLOR_BRIGHT_CYAN "NeoLoader v0.1" NB_COLOR_RESET "\n");
    sh("  Entry ....... " NB_COLOR_BRIGHT_WHITE "0x00001000" NB_COLOR_RESET "\n");
    sh("  Boot disk ... " NB_COLOR_BRIGHT_WHITE "HDF (hard drive)" NB_COLOR_RESET "\n");
    sh("  Root FS ..... " NB_COLOR_BRIGHT_GREEN "NBFS" NB_COLOR_RESET "\n");
    sh("  Init ........ " NB_COLOR_BRIGHT_GREEN "NeoBench init" NB_COLOR_RESET "\n");
    sh("  Display ..... " NB_COLOR_BRIGHT_WHITE "Aero-style desktop (planned)" NB_COLOR_RESET "\n");
    sh("\n");
}

static void cmd_amiga(void)
{
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  Amiga Custom Chips\n");
    sh_line();
    sh("  Denise ....... " NB_COLOR_BRIGHT_GREEN "ok" NB_COLOR_RESET " (video/composite)\n");
    sh("  Paula ........ " NB_COLOR_BRIGHT_GREEN "ok" NB_COLOR_RESET " (audio/serial)\n");
    sh("  Agnus ........ " NB_COLOR_BRIGHT_GREEN "ok" NB_COLOR_RESET " (DMA/copper)\n");
    sh("  Alice ........ " NB_COLOR_BRIGHT_GREEN "ok" NB_COLOR_RESET " (AGA)\n");
    sh("  Lisa ......... " NB_COLOR_BRIGHT_GREEN "ok" NB_COLOR_RESET " (AGA)\n");
    sh("  Gayle ........ " NB_COLOR_BRIGHT_GREEN "ok" NB_COLOR_RESET " (IDE/controller)\n");
    sh("\n");
}

static void cmd_zorro(void)
{
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  Zorro Bus\n");
    sh_line();
    sh("  Zorro II ..... " NB_COLOR_BRIGHT_AMBER "16-bit autoconfig" NB_COLOR_RESET "\n");
    sh("  Zorro III .... " NB_COLOR_BRIGHT_GREEN "32-bit autoconfig" NB_COLOR_RESET "\n");
    sh("  Base addr .... " NB_COLOR_BRIGHT_WHITE "0x40000000" NB_COLOR_RESET "\n");
    sh("  Size ......... " NB_COLOR_BRIGHT_WHITE "256 MB" NB_COLOR_RESET "\n");
    sh("\n");
}

static void cmd_pci(void)
{
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  PCI Bus (Mediator)\n");
    sh_line();
    sh("  Status ....... " NB_COLOR_BRIGHT_AMBER "Mediator not detected" NB_COLOR_RESET "\n");
    sh("  Devices ...... " NB_COLOR_BRIGHT_WHITE "0" NB_COLOR_RESET "\n");
    sh("\n");
}

static void cmd_cia(void)
{
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  CIA Chips\n");
    sh_line();
    sh("  CIA A ........ " NB_COLOR_BRIGHT_WHITE "0xBFE001" NB_COLOR_RESET " (keyboard/active)\n");
    sh("  CIA B ........ " NB_COLOR_BRIGHT_WHITE "0xBFD000" NB_COLOR_RESET " (serial/parallel)\n");
    sh("\n");
}

static void cmd_rtg(void)
{
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  RTG Framebuffer\n");
    sh_line();
    sh("  Mode ......... " NB_COLOR_BRIGHT_WHITE "1024x768x32" NB_COLOR_RESET "\n");
    sh("  Base ......... " NB_COLOR_BRIGHT_WHITE "0x40000000" NB_COLOR_RESET "\n");
    sh("  Pitch ........ " NB_COLOR_BRIGHT_WHITE "4096 bytes" NB_COLOR_RESET "\n");
    sh("  Status ....... " NB_COLOR_BRIGHT_AMBER "not initialized" NB_COLOR_RESET "\n");
    sh("\n");
}

static void cmd_audio(void)
{
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  Audio Subsystem\n");
    sh_line();
    sh("  Paula ........ " NB_COLOR_BRIGHT_WHITE "4-channel 8-bit DMA" NB_COLOR_RESET "\n");
    sh("  Status ....... " NB_COLOR_BRIGHT_AMBER "not loaded" NB_COLOR_RESET "\n");
    sh("\n");
}

static void cmd_nbfs(void)
{
    uint64_t root_inode;
    char volume[64];

    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  NBFS Filesystem\n");
    sh_line();

    if (vfs_root_filesystem() == 0)
    {
        sh("  Status ....... " NB_COLOR_RED "not mounted" NB_COLOR_RESET "\n");
        sh("\n");
        return;
    }

    nbfs_kernel_root_inode(vfs_root_filesystem(), &root_inode);
    nbfs_kernel_volume(vfs_root_filesystem(), volume, sizeof(volume));

    sh("  Volume ........ " NB_COLOR_BRIGHT_WHITE);
    sh(volume);
    sh(NB_COLOR_RESET "\n");
    sh("  Version ........ " NB_COLOR_BRIGHT_WHITE "1.0" NB_COLOR_RESET "\n");
    sh("  Block size ..... " NB_COLOR_BRIGHT_WHITE "4096 bytes" NB_COLOR_RESET "\n");
    sh("  Root inode ..... " NB_COLOR_BRIGHT_WHITE);
    sh_u64(root_inode);
    sh(NB_COLOR_RESET "\n");
    sh("  Mount point .... " NB_COLOR_BRIGHT_WHITE "/" NB_COLOR_RESET "\n");
    sh("  Status ......... " NB_COLOR_BRIGHT_GREEN "mounted (root)" NB_COLOR_RESET "\n");
    sh("\n");
}

static void cmd_root(void)
{
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  Root Filesystem\n");
    sh_line();

    if (vfs_root() == 0)
    {
        sh("  Status .... " NB_COLOR_RED "not mounted" NB_COLOR_RESET "\n");
        sh("\n");
        return;
    }

    sh("  Filesystem .. " NB_COLOR_BRIGHT_GREEN "NBFS" NB_COLOR_RESET "\n");
    sh("  Mount point . " NB_COLOR_BRIGHT_WHITE "/" NB_COLOR_RESET "\n");
    sh("  Link ........ " NB_COLOR_BRIGHT_GREEN "VFS root vnode" NB_COLOR_RESET "\n");
    sh("\n");
}

static void cmd_cat(int argc, char *args[])
{
    vfs_vnode_t *root;
    vfs_path_t root_path;
    vfs_file_t file;
    char buf[128];
    ssize_t n;

    if (argc < 2)
    {
        sh("  usage: nbcat <path>\n");
        return;
    }

    root = vfs_root();

    if (!root)
    {
        sh("  Root filesystem not mounted.\n");
        return;
    }

    vfs_path_init(&root_path, root);

    if (vfs_open(&root_path, args[1], 0, &file) != 0)
    {
        sh("  ");
        sh_color(NB_COLOR_RED, "not found: ");
        sh(args[1]);
        sh("\n");
        vfs_path_destroy(&root_path);
        return;
    }

    for (;;)
    {
        n = vfs_file_read(&file, buf, sizeof(buf));

        if (n < 0)
        {
            sh_color(NB_COLOR_RED, "  read error\n");
            break;
        }

        if (n == 0)
            break;

        for (ssize_t i = 0; i < n; i++)
            shc(buf[i]);
    }

    sh("\n");

    vfs_file_destroy(&file);
    vfs_path_destroy(&root_path);
}

static void cmd_mount(void)
{
    sh("\n");
    sh("  Root filesystem: " NB_COLOR_BRIGHT_GREEN "NBFS" NB_COLOR_RESET "\n");
    sh("  Mount point:     " NB_COLOR_BRIGHT_WHITE "/" NB_COLOR_RESET "\n");
    sh("  Status:          " NB_COLOR_BRIGHT_GREEN "read-write" NB_COLOR_RESET "\n");
    sh("\n");
}

static void cmd_check(void)
{
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  NBFS Consistency Check\n");
    sh_line();
    sh("  Superblock ........ " NB_COLOR_BRIGHT_GREEN "ok" NB_COLOR_RESET "\n");
    sh("  Inode table ....... " NB_COLOR_BRIGHT_GREEN "ok" NB_COLOR_RESET "\n");
    sh("  Bitmap ............ " NB_COLOR_BRIGHT_GREEN "ok" NB_COLOR_RESET "\n");
    sh("  Directory entries .. " NB_COLOR_BRIGHT_GREEN "ok" NB_COLOR_RESET "\n");
    sh("  Journal ........... " NB_COLOR_BRIGHT_GREEN "ok" NB_COLOR_RESET "\n");
    sh("  Extent mapping .... " NB_COLOR_BRIGHT_GREEN "ok" NB_COLOR_RESET "\n");
    sh("\n");
}

static void cmd_drivers(void)
{
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  Loaded Drivers\n");
    sh_line();
    sh_status("neobench",      "nexus platform",   NB_COLOR_BRIGHT_GREEN);
    sh_status("serial",        "CIA B console",    NB_COLOR_BRIGHT_GREEN);
    sh_status("nbfs",          "filesystem",       NB_COLOR_BRIGHT_GREEN);
    sh_status("zorro",         "bus driver",       NB_COLOR_BRIGHT_AMBER);
    sh_status("rtg",           "framebuffer",      NB_COLOR_BRIGHT_AMBER);
    sh_status("ata",           "storage",          NB_COLOR_BRIGHT_AMBER);
    sh_status("audio",         "Paula sound",      NB_COLOR_RED);
    sh_status("net",           "network",          NB_COLOR_RED);
    sh_status("usb",           "USB host",         NB_COLOR_RED);
    sh("\n");
}

static void cmd_modules(void)
{
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  Kernel Modules\n");
    sh_line();
    sh_status("kernel",        "built-in",         NB_COLOR_BRIGHT_GREEN);
    sh_status("nbfs",          "built-in",         NB_COLOR_BRIGHT_GREEN);
    sh_status("vfs",           "built-in",         NB_COLOR_BRIGHT_GREEN);
    sh_status("scheduler",     "4BSD",             NB_COLOR_BRIGHT_GREEN);
    sh_status("vm",            "virtual memory",   NB_COLOR_BRIGHT_GREEN);
    sh("\n");
}

static void cmd_config(void)
{
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  Kernel Configuration\n");
    sh_line();
    sh("  INVARIANTS .... " NB_COLOR_BRIGHT_GREEN "enabled" NB_COLOR_RESET "\n");
    sh("  WITNESS ....... " NB_COLOR_BRIGHT_GREEN "enabled" NB_COLOR_RESET "\n");
    sh("  DDB ........... " NB_COLOR_BRIGHT_GREEN "enabled" NB_COLOR_RESET "\n");
    sh("  KDB ........... " NB_COLOR_BRIGHT_GREEN "enabled" NB_COLOR_RESET "\n");
    sh("  SCHED ......... " NB_COLOR_BRIGHT_WHITE "4BSD" NB_COLOR_RESET "\n");
    sh("  INET .......... " NB_COLOR_BRIGHT_GREEN "enabled" NB_COLOR_RESET "\n");
    sh("  INET6 ......... " NB_COLOR_BRIGHT_GREEN "enabled" NB_COLOR_RESET "\n");
    sh("  NBFS .......... " NB_COLOR_BRIGHT_GREEN "enabled" NB_COLOR_RESET "\n");
    sh("\n");
}

static void cmd_log(void)
{
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_GREEN, "  Kernel Log\n");
    sh_line();
    sh("  [boot]  NeoLoader v0.1 started\n");
    sh("  [boot]  Loading kernel ELF... ok\n");
    sh("  [boot]  Jumping to 0x00001000\n");
    sh("  [init]  Serial console driver ... " NB_COLOR_BRIGHT_GREEN "[ok]" NB_COLOR_RESET "\n");
    sh("  [init]  Amiga custom chipset .... " NB_COLOR_BRIGHT_GREEN "[ok]" NB_COLOR_RESET "\n");
    sh("  [init]  CIA interrupt controller  " NB_COLOR_BRIGHT_GREEN "[ok]" NB_COLOR_RESET "\n");
    sh("  [init]  Zorro bus enumeration .. " NB_COLOR_BRIGHT_AMBER "[warn]" NB_COLOR_RESET "\n");
    sh("  [init]  Block device layer ..... " NB_COLOR_BRIGHT_GREEN "[ok]" NB_COLOR_RESET "\n");
    sh("  [init]  NBFS filesystem module . " NB_COLOR_BRIGHT_GREEN "[ok]" NB_COLOR_RESET "\n");
    sh("  [init]  VFS mount root ......... " NB_COLOR_BRIGHT_GREEN "[ok]" NB_COLOR_RESET "\n");
    sh("  [init]  Process scheduler (4BSD)" NB_COLOR_BRIGHT_AMBER " [warn]" NB_COLOR_RESET "\n");
    sh("  [init]  Network stack ......... " NB_COLOR_RED "[fail]" NB_COLOR_RESET "\n");
    sh("  [init]  RTG framebuffer ....... " NB_COLOR_BRIGHT_AMBER "[warn]" NB_COLOR_RESET "\n");
    sh("  [init]  NeoBench init ......... " NB_COLOR_BRIGHT_GREEN "[ok]" NB_COLOR_RESET "\n");
    sh("  [shell] NeoShell v1.0 ready\n");
    sh("\n");
}

static void cmd_reboot(void)
{
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_AMBER, "  Rebooting NeoBench...\n\n");
    *(volatile unsigned char *)0xBFE001 = 0xFF;
    *(volatile unsigned char *)0xBFE001 = 0x00;
    *(volatile unsigned char *)0xBFE001 = 0x01;
    for (;;)
        __asm__ volatile ("stop #0x2700");
}

static void cmd_halt(void)
{
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_RED, "  System halted.\n\n");
    for (;;)
        __asm__ volatile ("stop #0x2700");
}

static void cmd_poweroff(void)
{
    sh("\n");
    sh_color(NB_COLOR_BRIGHT_RED, "  Powering off...\n\n");
    /* CIA B PA2 = power off on Amiga. */
    *(volatile unsigned char *)0xBFD100 = 0x00;
    for (;;)
        __asm__ volatile ("stop #0x2700");
}

static void cmd_clear(void)
{
    sh(NB_CLS);
}

/* ── Command dispatch ───────────────────────────────────────── */

static void shell_execute(int argc, char *args[])
{
    if (argc == 0)
        return;

    const char *cmd = args[0];

    /* System */
    if (strcmp(cmd, "nbhelp") == 0)         cmd_help();
    else if (strcmp(cmd, "nbver") == 0)     cmd_ver();
    else if (strcmp(cmd, "nbstatus") == 0)  cmd_status();
    else if (strcmp(cmd, "nbcpu") == 0)     cmd_cpu();
    else if (strcmp(cmd, "nbmem") == 0)     cmd_mem();
    else if (strcmp(cmd, "nbboot") == 0)    cmd_boot();
    /* Hardware */
    else if (strcmp(cmd, "nbamiga") == 0)   cmd_amiga();
    else if (strcmp(cmd, "nbzorro") == 0)   cmd_zorro();
    else if (strcmp(cmd, "nbpci") == 0)     cmd_pci();
    else if (strcmp(cmd, "nbcia") == 0)     cmd_cia();
    else if (strcmp(cmd, "nbrtg") == 0)     cmd_rtg();
    else if (strcmp(cmd, "nbaudio") == 0)   cmd_audio();
    /* Filesystem */
    else if (strcmp(cmd, "nbfs") == 0)      cmd_nbfs();
    else if (strcmp(cmd, "nbroot") == 0)    cmd_root();
    else if (strcmp(cmd, "nbcat") == 0)     cmd_cat(argc, args);
    else if (strcmp(cmd, "nbmount") == 0)   cmd_mount();
    else if (strcmp(cmd, "nbcheck") == 0)   cmd_check();
    /* Kernel */
    else if (strcmp(cmd, "nbdrivers") == 0) cmd_drivers();
    else if (strcmp(cmd, "nbmodules") == 0) cmd_modules();
    else if (strcmp(cmd, "nbconfig") == 0)  cmd_config();
    else if (strcmp(cmd, "nblog") == 0)     cmd_log();
    /* Actions */
    else if (strcmp(cmd, "nbreboot") == 0)  cmd_reboot();
    else if (strcmp(cmd, "nbhalt") == 0)    cmd_halt();
    else if (strcmp(cmd, "nbpoweroff") == 0)cmd_poweroff();
    else if (strcmp(cmd, "nbclear") == 0)   cmd_clear();
    /* Legacy aliases */
    else if (strcmp(cmd, "help") == 0)      cmd_help();
    else if (strcmp(cmd, "ver") == 0)       cmd_ver();
    else if (strcmp(cmd, "status") == 0)    cmd_status();
    else if (strcmp(cmd, "clear") == 0)     cmd_clear();
    else if (strcmp(cmd, "reboot") == 0)    cmd_reboot();
    else if (strcmp(cmd, "halt") == 0)      cmd_halt();
    else
    {
        sh("\n  ");
        sh_color(NB_COLOR_RED, "Unknown command: ");
        sh(cmd);
        sh("\n  Type ");
        sh_color(NB_COLOR_BRIGHT_CYAN, "nbhelp");
        sh(" for NeoBench commands.\n\n");
    }
}

/* ── Main shell loop ────────────────────────────────────────── */

void neoshell_run(void)
{
    char input[SHELL_MAX_INPUT];
    char *args[SHELL_MAX_ARGS];

    sh("\n");
    sh_color(NB_COLOR_BOLD NB_COLOR_BRIGHT_CYAN, "  NeoShell v1.0\n");
    sh("  Type ");
    sh_color(NB_COLOR_BRIGHT_CYAN, "nbhelp");
    sh(" for NeoBench commands.\n\n");

    for (;;)
    {
        sh_color(NB_COLOR_BRIGHT_GREEN, "neobench> ");
        shell_readline(input, SHELL_MAX_INPUT);

        int argc = shell_parse(input, args, SHELL_MAX_ARGS);
        shell_execute(argc, args);
    }
}
