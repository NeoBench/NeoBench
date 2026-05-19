/*
 * NeoBench Kernel - NeoShell Interactive Command Shell
 * Bare-metal Amiga 68030/040/060
 *
 * Full-featured command shell with line editing, history,
 * tab completion, and a rich set of built-in commands.
 *
 * Corrections vs v1.0:
 *
 *  1. cmd_date READS TOD IN WRONG ORDER (silent wrong time).
 *     CIA TOD registers must be read HI first (which latches all three
 *     registers atomically), then MID, then LO (which releases the latch).
 *     Reading LO first or any other order can give a torn read where HI
 *     and LO are from different counter values.
 *     The original read: hi, mid, lo - correct!
 *     Actually re-reading: CIAA_TODHI is read, then TODMID, then TODLO.
 *     The 8520 datasheet says: reading TODHI latches all three. Reading
 *     TODLO releases the latch. So the order hi -> mid -> lo IS correct.
 *     The original code is correct.  No change needed; added comment.
 *
 *  2. cmd_cd RELATIVE PATH BUFFER OVERFLOW.
 *     In the relative path case:
 *       if (cwdlen > 1) strcat(s_cwd, "/");
 *       strncat(s_cwd, target, PATH_MAX - cwdlen - 2);
 *     After strcat adds "/", cwdlen is stale (it was computed before the
 *     strcat).  The strncat limit uses the old cwdlen, so we may allow
 *     one more character than the buffer can hold.
 *     Also: if cwdlen == PATH_MAX - 1 (buffer already at max), strcat
 *     writes a '\0' one past the buffer end.
 *     Fixed: compute remaining space correctly and guard against overflow.
 *
 *  3. cmd_top USES goto ACROSS VARIABLE INITIALIZATION.
 *     The loop uses "goto done" to exit past the 'neo::process::Info
 *     procs[16]' local declaration.  In C++ it is illegal to jump over
 *     a variable initialization with goto if the variable has a non-trivial
 *     constructor.  process::Info has no constructor (POD), but the
 *     declaration is still in scope.  The label 'done:' is AFTER the
 *     declaration in the enclosing scope, so this is actually fine for
 *     POD types.  However some compilers warn.  Fixed by restructuring
 *     with a bool flag instead of goto.
 *
 *  4. cmd_reboot VECTOR ADDRESS WRONG.
 *     The original reads the reset entry point from 0xFC0002.  The Amiga
 *     Kickstart ROM is typically mapped at 0xF80000 (INODE_SIZEKB ROM) or
 *     0xFC0000 (for 512KB ROM at 0xF80000 mapped twice, with ROM at
 *     0xFC0000 being the same content).  The standard warm reset procedure
 *     reads from 0xF80002 (reset vector), not 0xFC0002.
 *     More correctly for a generic Amiga warm reboot:
 *       Load the initial SSP from 0xF80000 and PC from 0xF80004 (Kickstart
 *       reset vectors), or use the ExecBase ColdCapture/WarmCapture hooks
 *       if AmigaOS is present (we're bare-metal so we don't have ExecBase).
 *     The simplest bare-metal warm reboot: execute RESET then jump to the
 *     Kickstart reset vector.  On a 68k, RESET asserts /RESET on the bus
 *     for 512 clock cycles.  The ROM reset vector at physical 0x00F80002
 *     gives the initial PC.  But after RESET the ROM is remapped to 0x0.
 *     Simplest approach: set the initial SSP from 0xF80000 and jump to
 *     the initial PC from 0xF80004 (Kickstart vectors).
 *     Fixed to use 0xF80000/0xF80004.
 *
 *  5. uptime CALCULATION SHOWS WRONG MINUTES.
 *     Original:
 *       uint32 mins  = secs / 60;
 *       uint32 hours = mins / 60;
 *       kprintf("up %u:%02u:%02u\n", hours, mins % 60, secs % 60);
 *     secs % 60 gives the seconds within the current minute. Correct.
 *     mins % 60 gives the minutes within the current hour. Correct.
 *     No bug; format is h:mm:ss which is the intended output.
 *
 *  6. PATH_MAX LOCAL CONSTANT SHADOWS GLOBAL MACRO.
 *     The shell defines:
 *       static constexpr uint32 PATH_MAX = INODE_SIZE;
 *     types.h also defines:
 *       #define MAX_PATH 512
 *     These don't conflict (different names), but PATH_MAX = INODE_SIZE is
 *     too short for NeoFS paths (MAX_NAME = 255 + separators).
 *     Raised to match types.h MAX_PATH = 512.
 */

#include "../include/neobench.h"
#include "../include/types.h"
#include "../lib/string.h"

namespace neo {
namespace shell {

/* ======================================================================
 * Shell state
 * ====================================================================== */

static constexpr uint32 MAX_ARGS    = 16;
static constexpr uint32 SHELL_PATH_MAX = 512;   /* Renamed to avoid macro conflict */
static constexpr uint32 CMD_BUF_LEN = INODE_SIZE;

static char s_cwd[SHELL_PATH_MAX] = "/";
static bool s_running = true;

/* ======================================================================
 * Command parsing
 * ====================================================================== */

struct CmdArgs {
    uint32 argc;
    char  *argv[MAX_ARGS];
};

static void parse_args(char *line, CmdArgs &args)
{
    args.argc = 0;
    char *p = line;

    while (*p && args.argc < MAX_ARGS) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        if (*p == '"') {
            p++;
            args.argv[args.argc++] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') *p++ = '\0';
        } else {
            args.argv[args.argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }
}

/* ======================================================================
 * Tab completion
 * ====================================================================== */

static const char *s_commands[] = {
    "cat", "cd", "clear", "cls", "date", "df", "echo", "free",
    "halt", "help", "history", "ls", "mount", "neocli", "neofetch",
    "ps", "pwd", "reboot", "top", "umount", "uname", "uptime",
    "ver", "version",
    nullptr
};

static void tab_complete(const char *partial, char *completion, uint32 comp_size)
{
    uint32      plen        = strlen(partial);
    const char *match       = nullptr;
    int         match_count = 0;

    if (plen == 0) { completion[0] = '\0'; return; }

    for (int i = 0; s_commands[i]; i++) {
        if (strncmp(s_commands[i], partial, plen) == 0) {
            match = s_commands[i];
            match_count++;
        }
    }

    if (match_count == 1 && match) {
        strncpy(completion, match, comp_size - 1);
        completion[comp_size - 1] = '\0';
        uint32 mlen = strlen(completion);
        if (mlen + 2 <= comp_size) {
            completion[mlen]     = ' ';
            completion[mlen + 1] = '\0';
        }
    } else if (match_count > 1) {
        kprintf("\n");
        for (int i = 0; s_commands[i]; i++) {
            if (strncmp(s_commands[i], partial, plen) == 0)
                kprintf("  %s", s_commands[i]);
        }
        kprintf("\n");
        completion[0] = '\0';
    } else {
        completion[0] = '\0';
    }
}

/* ======================================================================
 * Prompt
 * ====================================================================== */

static void show_prompt()
{
    neo::display::set_fg(2); kprintf("neobench:");
    neo::display::set_fg(6); kprintf("%s", s_cwd);
    neo::display::set_fg(7); kprintf("> ");
}

/* ======================================================================
 * Built-in commands
 * ====================================================================== */

static void cmd_help(const CmdArgs &)
{
    kprintf("\nNeoBench Shell Commands:\n\n");
    kprintf("  %-14s %s\n", "ls [path]",   "List directory contents");
    kprintf("  %-14s %s\n", "cd <path>",   "Change directory");
    kprintf("  %-14s %s\n", "cat <file>",  "Display file contents");
    kprintf("  %-14s %s\n", "pwd",         "Print working directory");
    kprintf("  %-14s %s\n", "echo <text>", "Print text");
    kprintf("  %-14s %s\n", "clear/cls",   "Clear screen");
    kprintf("  %-14s %s\n", "help",        "This help text");
    kprintf("  %-14s %s\n", "ver/version", "Kernel version info");
    kprintf("  %-14s %s\n", "uname [-a]",  "System identification");
    kprintf("  %-14s %s\n", "date",        "Show current date/time");
    kprintf("  %-14s %s\n", "uptime",      "System uptime");
    kprintf("  %-14s %s\n", "free",        "Memory usage");
    kprintf("  %-14s %s\n", "ps",          "Process list");
    kprintf("  %-14s %s\n", "top",         "Live process monitor");
    kprintf("  %-14s %s\n", "df",          "Disk free space");
    kprintf("  %-14s %s\n", "neofetch",    "System info + ASCII art");
    kprintf("  %-14s %s\n", "neocli",      "Launch NeoCLI admin tool");
    kprintf("  %-14s %s\n", "mount",       "Mount filesystem");
    kprintf("  %-14s %s\n", "umount",      "Unmount filesystem");
    kprintf("  %-14s %s\n", "history",     "Command history");
    kprintf("  %-14s %s\n", "reboot",      "Warm reboot");
    kprintf("  %-14s %s\n", "halt",        "Stop CPU");
    kprintf("\n");
}

static void cmd_version(const CmdArgs &)
{
    kprintf("\nNeoBench Kernel v1.0.0 (Denise)\n");
    kprintf("Built: %s %s\n", __DATE__, __TIME__);
    kprintf("Target: Motorola 68030/040/060\n");
    kprintf("Copyright (c) 2026 NeoBench Project\n\n");
}

static void cmd_uname(const CmdArgs &args)
{
    bool all = (args.argc > 1 && strcmp(args.argv[1], "-a") == 0);
    if (all) {
        neo::cpu::CpuInfo info;
        neo::cpu::detect(&info);
        kprintf("NeoBench 1.0.0 Amiga m68k 680%s NeoBench/Denise\n",
                (info.type == neo::cpu::CPU_68060) ? "60" :
                (info.type == neo::cpu::CPU_68040) ? "40" : "30");
    } else {
        kprintf("NeoBench\n");
    }
}

static void cmd_pwd(const CmdArgs &)  { kprintf("%s\n", s_cwd); }

static void cmd_echo(const CmdArgs &args)
{
    for (uint32 i = 1; i < args.argc; i++) {
        if (i > 1) kprintf(" ");
        kprintf("%s", args.argv[i]);
    }
    kprintf("\n");
}

static void cmd_clear(const CmdArgs &) { neo::display::clear(); }

/* ---- ls ---- */
static void cmd_ls(const CmdArgs &args)
{
    const char *path = (args.argc > 1) ? args.argv[1] : s_cwd;

    if (strcmp(path, "/") == 0) {
        kprintf("dev/    proc/   sys/    DH0:    DF0:\n");
        return;
    }
    if (strcmp(path, "/dev") == 0 || strcmp(path, "/dev/") == 0) {
        kprintf("con     ser     par     df0     dh0\n");
        return;
    }
    if (strcmp(path, "/proc") == 0 || strcmp(path, "/proc/") == 0) {
        kprintf("cpuinfo   meminfo   uptime   version\n");
        return;
    }
    if (strcmp(path, "/sys") == 0 || strcmp(path, "/sys/") == 0) {
        kprintf("chipset     interrupts\n");
        return;
    }

    neo::filesystem::DirEntry entries[32];
    int32 count = neo::filesystem::readdir(path, entries, 32);

    if (count < 0) {
        kprintf("ls: cannot access '%s': no such file or directory\n", path);
        return;
    }

    for (int32 i = 0; i < count; i++) {
        if (entries[i].type == 1) {
            neo::display::set_fg(4);
            kprintf("%-20s", entries[i].name);
            neo::display::set_fg(7);
        } else {
            kprintf("%-16s %8u", entries[i].name, entries[i].size);
        }
        kprintf("\n");
    }
}

/* ---- cd ---- */
static void cmd_cd(const CmdArgs &args)
{
    if (args.argc < 2) { strcpy(s_cwd, "/"); return; }

    const char *target = args.argv[1];

    if (strcmp(target, "/") == 0 || strcmp(target, "~") == 0) {
        strcpy(s_cwd, "/");
    } else if (strcmp(target, "..") == 0) {
        char *last_slash = strrchr(s_cwd, '/');
        if (last_slash && last_slash != s_cwd)
            *last_slash = '\0';
        else
            strcpy(s_cwd, "/");
    } else if (target[0] == '/') {
        strncpy(s_cwd, target, SHELL_PATH_MAX - 1);
        s_cwd[SHELL_PATH_MAX - 1] = '\0';
    } else {
        /* Relative path: append /target to s_cwd.
         * Must account for the '/' separator we may add. */
        uint32 cwdlen = strlen(s_cwd);
        bool   needs_sep = (cwdlen > 1); /* "/" already ends with '/' */
        uint32 sep_len   = needs_sep ? 1 : 0;
        uint32 tgt_len   = strlen(target);
        uint32 new_len   = cwdlen + sep_len + tgt_len;

        if (new_len >= SHELL_PATH_MAX) {
            kprintf("cd: path too long\n");
            return;
        }
        if (needs_sep) s_cwd[cwdlen++] = '/';
        strncpy(s_cwd + cwdlen, target, SHELL_PATH_MAX - cwdlen - 1);
        s_cwd[SHELL_PATH_MAX - 1] = '\0';
    }
}

/* ---- cat ---- */
static void cmd_cat(const CmdArgs &args)
{
    if (args.argc < 2) { kprintf("Usage: cat <file>\n"); return; }

    const char *path = args.argv[1];

    /* Virtual /proc files */
    if (strcmp(path, "/proc/cpuinfo") == 0) {
        neo::cpu::CpuInfo info;
        neo::cpu::detect(&info);
        kprintf("Processor\t: Motorola 680%s\n",
                (info.type == neo::cpu::CPU_68060) ? "60" :
                (info.type == neo::cpu::CPU_68040) ? "40" : "30");
        kprintf("Clock\t\t: %u MHz\n", info.clock_mhz);
        kprintf("FPU\t\t: %s\n",
                (info.fpu_type == neo::cpu::FPU_INTERNAL) ? "Internal" :
                (info.fpu_type == neo::cpu::FPU_68882)    ? "68882" :
                (info.fpu_type == neo::cpu::FPU_68881)    ? "68881" : "None");
        kprintf("MMU\t\t: %s\n", info.has_mmu ? "Yes" : "No");
        kprintf("Data Cache\t: %s\n", info.dcache_on ? "Enabled" : "Disabled");
        kprintf("Inst Cache\t: %s\n", info.icache_on ? "Enabled" : "Disabled");
        return;
    }
    if (strcmp(path, "/proc/meminfo") == 0) {
        neo::memory::Stats s;
        neo::memory::get_stats(&s);
        kprintf("ChipTotal:   %10u KB\n", s.chip_total / 1024);
        kprintf("ChipFree:    %10u KB\n", s.chip_free  / 1024);
        kprintf("FastTotal:   %10u KB\n", s.fast_total / 1024);
        kprintf("FastFree:    %10u KB\n", s.fast_free  / 1024);
        kprintf("TotalFree:   %10u KB\n", (s.chip_free + s.fast_free) / 1024);
        return;
    }
    if (strcmp(path, "/proc/uptime") == 0) {
        uint32 ticks = neo::timer::get_ticks();
        uint32 hz    = neo::timer::get_is_pal() ? 50 : 60;
        uint32 secs  = ticks / hz;
        kprintf("%u.%02u\n", secs, (ticks % hz) * (100 / hz));
        return;
    }
    if (strcmp(path, "/proc/version") == 0) {
        kprintf("NeoBench Kernel v1.0.0 (Denise) %s %s m68k\n", __DATE__, __TIME__);
        return;
    }
    if (strcmp(path, "/sys/chipset") == 0) {
        kprintf("Type: %s\n",        neo::display::get_chipset_name());
        kprintf("Agnus/Alice: %s\n", neo::display::get_agnus_name());
        kprintf("Denise/Lisa: %s\n", neo::display::get_denise_name());
        kprintf("Paula: 8364\n");
        return;
    }
    if (strcmp(path, "/sys/interrupts") == 0) {
        neo::interrupts::dump_stats();
        return;
    }

    /* Real file */
    neo::filesystem::FileHandle fh;
    if (!neo::filesystem::open(&fh, path, neo::filesystem::MODE_READ)) {
        kprintf("cat: %s: No such file or directory\n", path);
        return;
    }
    char    buf[512];
    int32   bytes;
    while ((bytes = neo::filesystem::read(&fh, buf, sizeof(buf) - 1)) > 0) {
        buf[bytes] = '\0';
        kprintf("%s", buf);
    }
    neo::filesystem::close(&fh);
}

/* ---- date ---- */
static void cmd_date(const CmdArgs &)
{
    /*
     * Read CIA-A TOD registers.
     * CRITICAL ORDER: Read TODHI first (latches all three atomically),
     * then TODMID, then TODLO (releases the latch).
     * Reading in any other order can give a torn value.
     */
    volatile uint8 *todhi  = (volatile uint8 *)0xBFEA01;
    volatile uint8 *todmid = (volatile uint8 *)0xBFE901;
    volatile uint8 *todlo  = (volatile uint8 *)0xBFE801;

    uint32 hi  = *todhi;    /* latch */
    uint32 mid = *todmid;
    uint32 lo  = *todlo;    /* release */
    uint32 tod = (hi << 16) | (mid << 8) | lo;

    uint32 hz          = neo::display::is_pal() ? 50 : 60;
    uint32 total_secs  = tod / hz;
    uint32 hours       = total_secs / 3600;
    uint32 minutes     = (total_secs % 3600) / 60;
    uint32 seconds     = total_secs % 60;
    kprintf("%02u:%02u:%02u (CIA-A TOD)\n", hours, minutes, seconds);

    /* Hardware RTC (A4000 has MSM6242B / RP5C01A at 0xDC0000) */
    if (neo::rtc::is_present()) {
        neo::rtc::DateTime dt;
        neo::rtc::read(&dt);
        kprintf("%04u-%02u-%02u %02u:%02u:%02u (RTC)\n",
                dt.year, dt.month, dt.day,
                dt.hour, dt.minute, dt.second);
    }
}

/* ---- uptime ---- */
static void cmd_uptime(const CmdArgs &)
{
    uint32 ticks = neo::timer::get_ticks();
    uint32 hz    = neo::timer::get_is_pal() ? 50 : 60;
    uint32 secs  = ticks / hz;
    uint32 mins  = secs  / 60;
    uint32 hours = mins  / 60;
    kprintf("up %u:%02u:%02u\n", hours, mins % 60, secs % 60);
}

/* ---- free ---- */
static void cmd_free(const CmdArgs &)
{
    neo::memory::Stats s;
    neo::memory::get_stats(&s);
    kprintf("            %10s %10s %10s\n", "Total", "Used", "Free");
    kprintf("Chip RAM    %8u KB %8u KB %8u KB\n",
            s.chip_total / 1024,
            (s.chip_total - s.chip_free) / 1024,
            s.chip_free / 1024);
    kprintf("Fast RAM    %8u KB %8u KB %8u KB\n",
            s.fast_total / 1024,
            (s.fast_total - s.fast_free) / 1024,
            s.fast_free / 1024);
    kprintf("──────────  ──────────  ──────────  ──────────\n");
    kprintf("Total       %8u KB %8u KB %8u KB\n",
            (s.chip_total + s.fast_total) / 1024,
            (s.chip_total + s.fast_total - s.chip_free - s.fast_free) / 1024,
            (s.chip_free  + s.fast_free)  / 1024);
}

/* ---- ps ---- */
static void cmd_ps(const CmdArgs &)
{
    kprintf("  PID  STATE    PRI  NAME\n");
    kprintf("  ───  ─────    ───  ────\n");

    neo::process::Info procs[64];
    uint32 count = neo::process::list(procs, 64);

    for (uint32 i = 0; i < count; i++) {
        const char *state;
        switch (procs[i].state) {
        case neo::process::STATE_RUNNING: state = "RUN  "; break;
        case neo::process::STATE_READY:   state = "READY"; break;
        case neo::process::STATE_WAIT:    state = "WAIT "; break;
        case neo::process::STATE_SLEEP:   state = "SLEEP"; break;
        default:                          state = "?????"; break;
        }
        kprintf("  %3u  %s  %4d  %s\n",
                procs[i].pid, state, procs[i].priority, procs[i].name);
    }
}

/* ---- top ---- */
static void cmd_top(const CmdArgs &)
{
    kprintf("Live process monitor (press any key to exit)\n\n");

    bool running = true;
    while (running) {
        neo::process::Info procs[16];
        uint32 count = neo::process::list(procs, 16);

        kprintf("\033[2J");
        kprintf("NeoBench top - ");
        uint32 ticks = neo::timer::get_ticks();
        uint32 hz    = neo::timer::get_is_pal() ? 50 : 60;
        uint32 secs  = ticks / hz;
        kprintf("up %u:%02u:%02u\n\n", secs / 3600, (secs / 60) % 60, secs % 60);

        neo::memory::Stats mem;
        neo::memory::get_stats(&mem);
        kprintf("Mem: %uK total, %uK used, %uK free\n\n",
                (mem.chip_total + mem.fast_total) / 1024,
                (mem.chip_total + mem.fast_total - mem.chip_free - mem.fast_free) / 1024,
                (mem.chip_free  + mem.fast_free)  / 1024);

        kprintf("  PID  STATE    CPU%%  NAME\n");
        kprintf("  ───  ─────    ────  ────\n");
        for (uint32 i = 0; i < count; i++) {
            const char *state;
            switch (procs[i].state) {
            case neo::process::STATE_RUNNING: state = "RUN  "; break;
            case neo::process::STATE_READY:   state = "READY"; break;
            default:                          state = "WAIT "; break;
            }
            kprintf("  %3u  %s  %3u%%  %s\n",
                    procs[i].pid, state, procs[i].cpu_usage, procs[i].name);
        }

        /* Wait ~1 second then check for keypress */
        for (int v = 0; v < (int)hz; v++) {
            if (neo::keyboard::key_available()) { running = false; break; }
            asm volatile("stop #0x2000" ::: "cc");
        }
    }

    if (neo::keyboard::key_available())
        neo::keyboard::read_scancode(); /* consume the exit key */
    kprintf("\n");
}

/* ---- df ---- */
static void cmd_df(const CmdArgs &)
{
    kprintf("Device     Type   Size     Used     Free     Use%%  Mounted\n");
    kprintf("────────── ─────  ──────── ──────── ──────── ────  ───────\n");

    neo::filesystem::MountInfo mounts[8];
    uint32 count = neo::filesystem::list_mounts(mounts, 8);

    for (uint32 i = 0; i < count; i++) {
        uint32 total_kb = (mounts[i].total_blocks * mounts[i].block_size) / 1024;
        uint32 free_kb  = (mounts[i].free_blocks  * mounts[i].block_size) / 1024;
        uint32 used_kb  = total_kb > free_kb ? total_kb - free_kb : 0;
        uint32 pct      = (total_kb > 0) ? (used_kb * 100) / total_kb : 0;

        kprintf("%-10s %-5s  %6uMB %6uMB %6uMB  %3u%%  %s\n",
                mounts[i].device, mounts[i].fs_type,
                total_kb / 1024, used_kb / 1024, free_kb / 1024,
                pct, mounts[i].mount_point);
    }
}

/* ---- neofetch ---- */
static void cmd_neofetch(const CmdArgs &)
{
    neo::cpu::CpuInfo cpu;
    neo::cpu::detect(&cpu);
    neo::memory::Stats mem;
    neo::memory::get_stats(&mem);

    uint32 ticks = neo::timer::get_ticks();
    uint32 hz    = neo::timer::get_is_pal() ? 50 : 60;
    uint32 secs  = ticks / hz;

    const char *cpu_name =
        (cpu.type == neo::cpu::CPU_68060) ? "Motorola 68060" :
        (cpu.type == neo::cpu::CPU_68040) ? "Motorola 68040" : "Motorola 68030";

    const char *fpu_name =
        (cpu.fpu_type == neo::cpu::FPU_INTERNAL) ? "Internal" :
        (cpu.fpu_type == neo::cpu::FPU_68882)    ? "68882"    :
        (cpu.fpu_type == neo::cpu::FPU_68881)    ? "68881"    : "None";

    neo::display::set_fg(6);
    kprintf("    ╔══════════════════╗");
    neo::display::set_fg(7);
    kprintf("   OS:        NeoBench 1.0.0\n");

    neo::display::set_fg(6);
    kprintf("    ║  ███╗   ██╗██████║");
    neo::display::set_fg(7);
    kprintf("   Kernel:    Denise v1.0.0\n");

    neo::display::set_fg(6);
    kprintf("    ║  ████╗  ██║██╔══█║");
    neo::display::set_fg(7);
    kprintf("   CPU:       %s @ %uMHz\n", cpu_name, cpu.clock_mhz);

    neo::display::set_fg(6);
    kprintf("    ║  ██╔██╗ ██║████╔╝║");
    neo::display::set_fg(7);
    kprintf("   FPU:       %s\n", fpu_name);

    neo::display::set_fg(6);
    kprintf("    ║  ██║╚██╗██║██╔══╝║");
    neo::display::set_fg(7);
    kprintf("   Chipset:   %s\n", neo::display::get_chipset_name());

    neo::display::set_fg(6);
    kprintf("    ║  ██║ ╚████║██████║");
    neo::display::set_fg(7);
    kprintf("   RAM:       %u KB Chip + %u KB Fast\n",
            mem.chip_total / 1024, mem.fast_total / 1024);

    neo::display::set_fg(6);
    kprintf("    ║  ╚═╝  ╚═══╝╚═════║");
    neo::display::set_fg(7);
    kprintf("   Uptime:    %u:%02u:%02u\n",
            secs / 3600, (secs / 60) % 60, secs % 60);

    neo::display::set_fg(6);
    kprintf("    ╚══════════════════╝");
    neo::display::set_fg(7);
    kprintf("   Shell:     NeoShell 1.0\n");

    kprintf("                         ");
    kprintf("   Display:   %ux%u\n",
            neo::display::get_width(), neo::display::get_height());
    kprintf("\n");

    kprintf("    ");
    for (uint8 c = 0; c < 8; c++) { neo::display::set_fg(c); kprintf("███"); }
    neo::display::set_fg(7);
    kprintf("\n\n");
}

/* ---- reboot ---- */
static void __attribute__((noreturn)) cmd_reboot(const CmdArgs &)
{
    kprintf("Rebooting...\n");
    neo::display::set_fg(3);

    /*
     * Amiga warm reset:
     * 1. Disable all interrupts (IPL=7).
     * 2. Execute RESET (asserts /RESET on bus for ~500 cycles, resets peripherals).
     * 3. Load initial SSP and PC from Kickstart ROM vectors (0xF80000, 0xF80004).
     *
     * After RESET, the ROM is remapped to address 0.  The Kickstart has
     * its own reset vector at physical 0x00F80000 (SSP) and 0x00F80004 (PC).
     * We load SP from 0xF80000 and jump to 0xF80004 content to hand off.
     */
    asm volatile(
        "ori.w  #0x0700, %%sr       \n\t" /* IPL=7, no interrupts */
        "reset                      \n\t" /* assert /RESET ~500 cycles */
        "move.l 0x00F80000, %%sp    \n\t" /* load Kickstart initial SSP */
        "move.l 0x00F80004, %%a0    \n\t" /* load Kickstart initial PC */
        "jmp    (%%a0)              \n\t"
        ::: "a0"
    );
    __builtin_unreachable();
}

/* ---- halt ---- */
static void __attribute__((noreturn)) cmd_halt(const CmdArgs &)
{
    kprintf("System halted. Power off or reset.\n");
    asm volatile(
        "ori.w  #0x0700, %%sr   \n\t"
        "stop   #0x2700         \n\t"
        :::
    );
    __builtin_unreachable();
}

/* ---- mount / umount ---- */
static void cmd_mount(const CmdArgs &args)
{
    if (args.argc < 2) {
        neo::filesystem::MountInfo mounts[8];
        uint32 count = neo::filesystem::list_mounts(mounts, 8);
        for (uint32 i = 0; i < count; i++) {
            kprintf("%s on %s type %s\n",
                    mounts[i].device, mounts[i].mount_point, mounts[i].fs_type);
        }
        return;
    }
    kprintf("mount: manual mount not yet implemented\n");
}

static void cmd_umount(const CmdArgs &args)
{
    if (args.argc < 2) { kprintf("Usage: umount <device>\n"); return; }
    kprintf("umount: not yet implemented\n");
}

/* ---- history ---- */
static void cmd_history(const CmdArgs &)
{
    uint32 count = neo::console::history_count();
    for (uint32 i = count; i > 0; i--) {
        const char *entry = neo::console::history_get(i - 1);
        if (entry) kprintf("  %3u  %s\n", count - i + 1, entry);
    }
}

/* ======================================================================
 * Command dispatch table
 * ====================================================================== */

typedef void (*CmdFunc)(const CmdArgs &);
struct CmdEntry { const char *name; CmdFunc func; };

static const CmdEntry s_cmd_table[] = {
    { "help",       cmd_help    },
    { "ver",        cmd_version },
    { "version",    cmd_version },
    { "uname",      cmd_uname   },
    { "pwd",        cmd_pwd     },
    { "echo",       cmd_echo    },
    { "clear",      cmd_clear   },
    { "cls",        cmd_clear   },
    { "ls",         cmd_ls      },
    { "cd",         cmd_cd      },
    { "cat",        cmd_cat     },
    { "date",       cmd_date    },
    { "uptime",     cmd_uptime  },
    { "free",       cmd_free    },
    { "ps",         cmd_ps      },
    { "top",        cmd_top     },
    { "df",         cmd_df      },
    { "neofetch",   cmd_neofetch},
    { "reboot",     cmd_reboot  },
    { "halt",       cmd_halt    },
    { "mount",      cmd_mount   },
    { "umount",     cmd_umount  },
    { "history",    cmd_history },
    { nullptr,      nullptr     },
};

/* ======================================================================
 * Execute a command line
 * ====================================================================== */

static void execute(char *line)
{
    CmdArgs args;
    parse_args(line, args);
    if (args.argc == 0) return;

    if (strcmp(args.argv[0], "neocli") == 0) {
        neo::cli::main(args.argc, args.argv);
        return;
    }

    for (int i = 0; s_cmd_table[i].name; i++) {
        if (strcmp(args.argv[0], s_cmd_table[i].name) == 0) {
            s_cmd_table[i].func(args);
            return;
        }
    }

    kprintf("%s: command not found. Type 'help' for available commands.\n",
            args.argv[0]);
}

/* ======================================================================
 * Shell entry points
 * ====================================================================== */

void init()
{
    s_running = true;
    strcpy(s_cwd, "/");
}

void run()
{
    main_loop();
}

void main_loop()
{
    char line_buf[CMD_BUF_LEN];

    neo::console::set_tab_completion(tab_complete);

    kprintf("Welcome to NeoShell v1.0 - Type 'help' for commands.\n\n");

    s_running = true;
    while (s_running) {
        show_prompt();
        uint32 len = neo::console::getline(line_buf, CMD_BUF_LEN, nullptr);
        if (len > 0) {
            neo::console::history_add(line_buf);
            execute(line_buf);
        }
    }
}

} /* namespace shell */
} /* namespace neo */
