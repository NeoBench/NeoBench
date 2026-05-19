/*
 * NeoBench Kernel - NeoCLI System Administration Tool
 * Bare-metal Amiga 68030/040/060
 *
 * Advanced system diagnostics, benchmarking, and configuration.
 * Launched from NeoShell via the "neocli" command.
 *
 * Corrections vs v1.0:
 *
 *  1. bench_timer_read HAS A LATCH RACE.
 *     CIA timers are 16-bit and decrement at ~709KHz.  Reading the
 *     low byte then the high byte (or vice versa) can give a torn value
 *     if the timer counts down between the two reads.  The CIA 8520 does
 *     NOT have a hardware latch for timer reads like it does for TOD.
 *     The standard technique for a racefree 16-bit CIA timer read is:
 *       1. Read hi byte.
 *       2. Read lo byte.
 *       3. Read hi byte again.
 *       4. If hi changed, repeat from step 1 (the timer rolled over
 *          between reads; retry).
 *     The original reads lo then hi, which means if the timer crosses
 *     from 0x??FF to 0x??00 between the reads, hi appears incremented
 *     (actually decremented for a countdown timer) while lo=0x00.  This
 *     gives a value ~INODE_SIZE ticks too high or too low.
 *     Fixed: read hi, lo, hi; if hi changed, re-read all.
 *
 *  2. bench_timer_start CIA CRA WRITE WRONG.
 *     "CIAB_CRA_ &= 0xC0" uses a read-modify-write.  CIA-B CRA at
 *     0xBFDE00 is a write-only register on the 8520 (reading returns
 *     the last-written value or undefined).  In practice on real
 *     hardware the 8520 does return the last written value, so RMW
 *     should work.  However the mask 0xC0 zeros bits 0-5, which:
 *       bit 0: START = 0 (stop timer)
 *       bit 1: PBON  = 0 (timer output disabled on PB6)
 *       bit 2: OUTMODE = 0
 *       bit 3: RUNMODE = 0 (continuous)
 *       bit 4: LOAD = 0
 *       bit 5: INMODE0 = 0 (count E-clock)
 *       bit 6: SPMODE = preserved
 *       bit 7: TODIN = preserved
 *     This is correct for stopping the timer.  No change needed.
 *
 *  3. ticks_to_us OVERFLOWS FOR LARGE TICK COUNTS.
 *     ticks * 1410 can overflow uint32 when ticks > ~3,045,454.
 *     At 709KHz timer rate, 3,045,454 ticks = ~4.3 seconds.
 *     Our benchmark iterations take much less than 4 seconds, but
 *     the memory bandwidth test runs 10 x 1MB copies which could
 *     approach this.  Fixed: use 64-bit intermediate.
 *
 *  4. cmd_benchmark COMBINED SCORE DIVISION CAN GIVE WRONG RESULT.
 *     combined = (int_score + fpu_score + mem_score) / 3.
 *     If fpu_score == 0 (no FPU), the combined score is pulled down
 *     unfairly.  More meaningfully: if the FPU test was skipped, we
 *     should only average the tests that ran.  Fixed: count active tests.
 *
 *  5. CACR movec SYNTAX.
 *     "movec %%cacr, %0" is correct AT&T syntax for:
 *       movec cacr,d0
 *     The CACR register code for MOVEC is 0x0002 (matches movec Rc,Dn
 *     encoding: 0x4E7A_Dn_02).  This is correct for 68030/040/060.
 *     No bug; verified.
 */

#include "../include/neobench.h"
#include "../include/types.h"
#include "../lib/string.h"

namespace neo {
namespace cli {

/* ======================================================================
 * Timer helpers for benchmarking
 * CIA-B Timer A, E-clock input (~709379 Hz PAL)
 * ====================================================================== */

namespace {

/* CIA-B timer registers */
static inline volatile uint8 &CIAB_TALO() { return *(volatile uint8 *)0xBFD400; }
static inline volatile uint8 &CIAB_TAHI() { return *(volatile uint8 *)0xBFD500; }
static inline volatile uint8 &CIAB_CRA()  { return *(volatile uint8 *)0xBFDE00; }
static inline volatile uint8 &CIAB_ICR()  { return *(volatile uint8 *)0xBFDD00; }

/* Custom chip read register */
static inline volatile uint16 &CUSTOM_DMACONR() {
    return *(volatile uint16 *)(0xDFF000 + 0x002);
}

void bench_timer_start()
{
    CIAB_CRA() &= 0xC0;     /* Stop timer A (clear START bit) */
    CIAB_ICR()  = 0x7F;     /* Clear all CIA-B interrupts */
    CIAB_TALO() = 0xFF;     /* Load latch low byte */
    CIAB_TAHI() = 0xFF;     /* Load latch high byte */
    CIAB_CRA()  = 0x11;     /* Load + start: LOAD=1 | START=1, continuous mode */
}

/*
 * Read CIA timer A without latch races.
 * The 8520 has no atomic latch for timers (unlike TOD).
 * Strategy: read hi, read lo, read hi again.
 * If hi changed between reads, retry (timer rolled through 0 mid-read).
 */
uint32 bench_timer_read()
{
    uint8  hi0, lo, hi1;
    do {
        hi0 = CIAB_TAHI();
        lo  = CIAB_TALO();
        hi1 = CIAB_TAHI();
    } while (hi0 != hi1);

    uint16 val = ((uint16)hi0 << 8) | lo;
    return (uint32)(0xFFFFu - val);
}

void bench_timer_stop()
{
    CIAB_CRA() &= 0xC0; /* Clear START bit */
}

/* Convert ticks to microseconds.
 * E-clock = 709379 Hz (PAL).
 * us = ticks * 1000000 / 709379
 * To avoid uint32 overflow use 64-bit intermediate.
 */
uint32 ticks_to_us(uint32 ticks)
{
    return (uint32)(((uint64)ticks * 1000000ull) / 709379ull);
}

/* Print a colour bar */
void print_bar(const char *label, uint32 value, uint32 max_val, uint32 bar_width)
{
    kprintf("  %-20s ", label);
    uint32 filled = (max_val > 0) ? (value * bar_width) / max_val : 0;
    if (filled > bar_width) filled = bar_width;

    neo::display::set_fg(2);
    kprintf("[");
    for (uint32 i = 0; i < bar_width; i++) {
        if (i < filled) {
            if      (i < bar_width / 3)     neo::display::set_fg(2);
            else if (i < 2 * bar_width / 3) neo::display::set_fg(3);
            else                            neo::display::set_fg(1);
            kprintf("\xe2\x96\x88"); /* UTF-8 full block █ */
        } else {
            neo::display::set_fg(7);
            kprintf("\xe2\x96\x91"); /* UTF-8 light shade ░ */
        }
    }
    neo::display::set_fg(7);
    kprintf("] %u\n", value);
}

} /* anonymous namespace */

/* ======================================================================
 * Command implementations
 * ====================================================================== */

/* ---- status ---- */
static void cmd_status()
{
    kprintf("\n╔══════════════════════════════════════════════════╗\n");
    kprintf("║            NeoCLI System Dashboard               ║\n");
    kprintf("╚══════════════════════════════════════════════════╝\n\n");

    neo::cpu::CpuInfo cpu;   neo::cpu::detect(&cpu);
    neo::memory::Stats mem;  neo::memory::get_stats(&mem);

    uint32 ticks = neo::timer::get_ticks();
    uint32 hz    = neo::timer::get_is_pal() ? 50 : 60;
    uint32 secs  = ticks / hz;

    kprintf("  CPU:     Motorola 680%s @ %u MHz\n",
            (cpu.type == neo::cpu::CPU_68060) ? "60" :
            (cpu.type == neo::cpu::CPU_68040) ? "40" : "30",
            cpu.clock_mhz);
    kprintf("  FPU:     %s\n",
            (cpu.fpu_type == neo::cpu::FPU_INTERNAL) ? "Internal" :
            (cpu.fpu_type == neo::cpu::FPU_68882)    ? "68882"    :
            (cpu.fpu_type == neo::cpu::FPU_68881)    ? "68881"    : "None");
    kprintf("  Uptime:  %u:%02u:%02u\n\n",
            secs / 3600, (secs / 60) % 60, secs % 60);

    uint32 total = mem.chip_total + mem.fast_total;
    uint32 used  = total - mem.chip_free - mem.fast_free;
    kprintf("  Memory Usage:\n");
    print_bar("Chip RAM", mem.chip_total - mem.chip_free, mem.chip_total, 30);
    print_bar("Fast RAM", mem.fast_total - mem.fast_free, mem.fast_total, 30);
    kprintf("\n  Total: %u KB used / %u KB total\n\n", used / 1024, total / 1024);
}

/* ---- sysinfo ---- */
static void cmd_sysinfo()
{
    kprintf("\n=== Detailed System Information ===\n\n");

    neo::cpu::CpuInfo cpu; neo::cpu::detect(&cpu);

    kprintf("  Processor:\n");
    kprintf("    Type:         Motorola 680%s\n",
            (cpu.type == neo::cpu::CPU_68060) ? "60" :
            (cpu.type == neo::cpu::CPU_68040) ? "40" : "30");
    kprintf("    Clock:        %u MHz\n", cpu.clock_mhz);
    kprintf("    FPU:          %s\n",
            (cpu.fpu_type == neo::cpu::FPU_INTERNAL) ? "Internal" :
            (cpu.fpu_type == neo::cpu::FPU_68882)    ? "68882"    :
            (cpu.fpu_type == neo::cpu::FPU_68881)    ? "68881"    : "None");
    kprintf("    MMU:          %s\n", cpu.has_mmu   ? "Present" : "Not detected");
    kprintf("    Data Cache:   %s\n", cpu.dcache_on ? "ON"      : "OFF");
    kprintf("    Inst Cache:   %s\n", cpu.icache_on ? "ON"      : "OFF");
    kprintf("    Burst Mode:   %s\n", cpu.burst_mode? "ON"      : "OFF");
    kprintf("\n");

    kprintf("  Chipset:\n");
    kprintf("    Type:         %s\n", neo::display::get_chipset_name());
    kprintf("    Agnus/Alice:  %s\n", neo::display::get_agnus_name());
    kprintf("    Denise/Lisa:  %s\n", neo::display::get_denise_name());
    kprintf("    Paula:        8364\n");
    kprintf("    CIA-A:        8520 @ 0xBFE001\n");
    kprintf("    CIA-B:        8520 @ 0xBFD000\n");
    kprintf("    Video:        %s\n", neo::display::is_pal() ? "PAL" : "NTSC");
    kprintf("\n");

    neo::memory::Stats mem; neo::memory::get_stats(&mem);
    kprintf("  Memory:\n");
    kprintf("    Chip RAM:     %u KB total, %u KB free\n",
            mem.chip_total / 1024, mem.chip_free / 1024);
    kprintf("    Fast RAM:     %u KB total, %u KB free\n",
            mem.fast_total / 1024, mem.fast_free / 1024);
    kprintf("    Largest Chip: %u KB\n", mem.largest_chip / 1024);
    kprintf("    Largest Fast: %u KB\n", mem.largest_fast / 1024);
    kprintf("\n");

    kprintf("  Storage:\n");
    neo::filesystem::MountInfo mounts[8];
    uint32 mcnt = neo::filesystem::list_mounts(mounts, 8);
    for (uint32 i = 0; i < mcnt; i++) {
        uint32 mb = (mounts[i].total_blocks * mounts[i].block_size / 1024) / 1024;
        kprintf("    %s: %s (%u MB)\n", mounts[i].device, mounts[i].fs_type, mb);
    }
    kprintf("\n");
}

/* ---- benchmark ---- */
static void cmd_benchmark()
{
    kprintf("\n╔══════════════════════════════════════════════════╗\n");
    kprintf("║         NeoBench System Benchmark Suite          ║\n");
    kprintf("╚══════════════════════════════════════════════════╝\n\n");

    neo::cpu::CpuInfo cpu; neo::cpu::detect(&cpu);

    const uint32 ITERATIONS = 100000;
    uint32 int_score = 0;
    uint32 fpu_score = 0;
    uint32 mem_score = 0;
    int    active_tests = 0;

    /* ---- Integer benchmark ---- */
    kprintf("  [1/3] Integer operations (%u iterations)...\n", ITERATIONS);
    bench_timer_start();
    volatile int32 acc = 0;
    for (uint32 i = 0; i < ITERATIONS; i++) {
        acc += (int32)i;
        acc *= 3;
        acc /= 2;
        acc -= (int32)(i >> 1);
    }
    uint32 int_ticks = bench_timer_read();
    bench_timer_stop();
    uint32 int_us = ticks_to_us(int_ticks);
    int_score = (int_us > 0) ? (ITERATIONS * 100) / int_us : 9999;
    active_tests++;
    kprintf("        Time: %u us  Score: %u\n\n", int_us, int_score);

    /* ---- FPU benchmark ---- */
    kprintf("  [2/3] FPU operations (%u iterations)...\n", ITERATIONS / 10);
    if (cpu.fpu_type != neo::cpu::FPU_NONE) {
        bench_timer_start();
        volatile float64 facc = 1.0;
        for (uint32 i = 1; i < ITERATIONS / 10; i++) {
            facc *= 1.0001;
            facc /= 1.00005;
            asm volatile(
                "fmove.d %0, %%fp0  \n\t"
                "fsqrt.x %%fp0      \n\t"
                "fmove.d %%fp0, %0  \n\t"
                : "+m"(facc) : : "fp0"
            );
        }
        uint32 fpu_ticks = bench_timer_read();
        bench_timer_stop();
        uint32 fpu_us = ticks_to_us(fpu_ticks);
        fpu_score = (fpu_us > 0) ? ((ITERATIONS / 10) * 100) / fpu_us : 9999;
        active_tests++;
        kprintf("        Time: %u us  Score: %u\n\n", fpu_us, fpu_score);
    } else {
        kprintf("        SKIPPED (no FPU detected)\n\n");
    }

    /* ---- Memory bandwidth ---- */
    kprintf("  [3/3] Memory bandwidth (1MB block copy x10)...\n");
    void *blk_src = neo::memory::alloc(1024 * 1024, neo::memory::NB_MEMF_FAST);
    void *blk_dst = neo::memory::alloc(1024 * 1024, neo::memory::NB_MEMF_FAST);
    if (blk_src && blk_dst) {
        memset(blk_src, 0xAA, 1024 * 1024);
        bench_timer_start();
        for (int pass = 0; pass < 10; pass++)
            memcpy(blk_dst, blk_src, 1024 * 1024);
        uint32 mem_ticks = bench_timer_read();
        bench_timer_stop();
        uint32 mem_us = ticks_to_us(mem_ticks);
        mem_score = (mem_us > 0) ? (uint32)(10000000ull / mem_us) : 9999;
        active_tests++;
        kprintf("        Time: %u us  Throughput: %u MB/s\n\n", mem_us, mem_score);
        neo::memory::free(blk_src);
        neo::memory::free(blk_dst);
    } else {
        kprintf("        FAILED (insufficient memory)\n\n");
        if (blk_src) neo::memory::free(blk_src);
        if (blk_dst) neo::memory::free(blk_dst);
    }

    /* ---- Results ---- */
    kprintf("  ═══════════════════════════════════════════\n");
    kprintf("  Benchmark Results:\n\n");

    uint32 max_score = 1;
    if (int_score > max_score) max_score = int_score;
    if (fpu_score > max_score) max_score = fpu_score;
    if (mem_score > max_score) max_score = mem_score;

    print_bar("Integer (ops/us)", int_score, max_score, 30);
    if (cpu.fpu_type != neo::cpu::FPU_NONE)
        print_bar("FPU (ops/us)",     fpu_score, max_score, 30);
    if (mem_score > 0)
        print_bar("Memory (MB/s)",    mem_score, max_score, 30);

    /* Average only the tests that actually ran */
    uint32 combined = (active_tests > 0)
        ? (int_score + fpu_score + mem_score) / (uint32)active_tests
        : 0;
    kprintf("\n  Combined Score: %u  (from %d test%s)\n",
            combined, active_tests, active_tests == 1 ? "" : "s");
    kprintf("  ═══════════════════════════════════════════\n\n");
}

/* ---- services ---- */
static void cmd_services()
{
    kprintf("\n  Running Kernel Services:\n\n");
    kprintf("  %-20s %-10s %s\n", "Service", "Status", "Info");
    kprintf("  ────────────────────────────────────────────\n");
    kprintf("  %-20s %-10s %s\n", "Timer",       "ACTIVE", "CIA-B Timer A+B");
    kprintf("  %-20s %-10s %s\n", "Keyboard",    "ACTIVE", "CIA-A SP interface");
    kprintf("  %-20s %-10s %s\n", "Mouse",       "ACTIVE", "Gameport 0");
    kprintf("  %-20s %-10s %s\n", "Display",     "ACTIVE", neo::display::get_chipset_name());
    kprintf("  %-20s %-10s %s\n", "Audio",       "ACTIVE", "Paula 4-ch DMA");
    kprintf("  %-20s %-10s %s\n", "Serial",      "ACTIVE", "9600 8N1");
    kprintf("  %-20s %-10s %s\n", "Memory Mgr",  "ACTIVE", "Buddy allocator");
    kprintf("  %-20s %-10s %s\n", "Process Mgr", "ACTIVE", "Round-robin");
    kprintf("  %-20s %-10s %s\n", "VFS",         "ACTIVE", "/dev /proc /sys");
    kprintf("\n");
}

/* ---- config show / set ---- */
static void cmd_config_show()
{
    kprintf("\n  Kernel Configuration:\n\n");
    kprintf("  %-24s = %s\n", "kernel.version",    "1.0.0");
    kprintf("  %-24s = %s\n", "kernel.codename",   "Denise");
    kprintf("  %-24s = %u\n", "mmu.page_size",     4096);
    kprintf("  %-24s = %s\n", "cache.data",        "copyback");
    kprintf("  %-24s = %s\n", "cache.instruction", "enabled");
    kprintf("  %-24s = %u\n", "process.max_tasks", 64);
    kprintf("  %-24s = %u\n", "serial.baud",       9600);
    kprintf("  %-24s = %s\n", "display.mode",      "text 80x25");
    kprintf("  %-24s = %s\n", "audio.channels",    "4 (Paula)");
    kprintf("\n");
}

static void cmd_config_set(const char *key, const char *value)
{
    kprintf("  Set '%s' = '%s'\n", key, value);
    kprintf("  Note: runtime config changes not yet persistent\n");
}

/* ---- mem dump ---- */
static void cmd_mem_dump(uint32 addr, uint32 len)
{
    if (len > 512) len = 512;
    kprintf("\n  Memory dump: 0x%08x - 0x%08x (%u bytes)\n\n",
            addr, addr + len - 1, len);

    const uint8 *p = (const uint8 *)addr;
    for (uint32 off = 0; off < len; off += 16) {
        kprintf("  %08x: ", addr + off);
        for (uint32 i = 0; i < 16 && (off + i) < len; i++) {
            kprintf("%02x ", p[off + i]);
            if (i == 7) kprintf(" ");
        }
        uint32 remaining = (off + 16 <= len) ? 16 : len - off;
        for (uint32 i = remaining; i < 16; i++) {
            kprintf("   ");
            if (i == 7) kprintf(" ");
        }
        kprintf(" |");
        for (uint32 i = 0; i < 16 && (off + i) < len; i++) {
            char c = (char)p[off + i];
            kprintf("%c", (c >= 0x20 && c <= 0x7E) ? c : '.');
        }
        kprintf("|\n");
    }
    kprintf("\n");
}

/* ---- mem test ---- */
static void cmd_mem_test()
{
    kprintf("\n  Memory Test (64KB Fast RAM)\n\n");

    const uint32 TEST_SIZE = 64 * 1024;
    void *block = neo::memory::alloc(TEST_SIZE, neo::memory::NB_MEMF_FAST);
    if (!block) { kprintf("  FAIL: Could not allocate test block\n\n"); return; }

    uint8 *tb       = (uint8 *)block;
    bool   all_pass = true;

    struct { uint8 pat; const char *name; } patterns[] = {
        { 0x00, "Zero fill    " },
        { 0xFF, "Ones fill    " },
        { 0xAA, "0xAA pattern " },
        { 0x55, "0x55 pattern " },
    };

    for (int p = 0; p < 4; p++) {
        memset(tb, patterns[p].pat, TEST_SIZE);
        bool pass = true;
        for (uint32 i = 0; i < TEST_SIZE && pass; i++)
            if (tb[i] != patterns[p].pat) pass = false;
        neo::display::set_fg(pass ? 2 : 1);
        kprintf("  [%s] %s (%u KB)\n", pass ? "PASS" : "FAIL",
                patterns[p].name, TEST_SIZE / 1024);
        neo::display::set_fg(7);
        if (!pass) all_pass = false;
    }

    /* Address-as-data */
    uint32 *tb32   = (uint32 *)tb;
    uint32  count32 = TEST_SIZE / 4;
    for (uint32 i = 0; i < count32; i++)
        tb32[i] = (uint32)&tb32[i];
    bool addr_pass = true;
    for (uint32 i = 0; i < count32 && addr_pass; i++)
        if (tb32[i] != (uint32)&tb32[i]) addr_pass = false;
    neo::display::set_fg(addr_pass ? 2 : 1);
    kprintf("  [%s] Address-as-data\n", addr_pass ? "PASS" : "FAIL");
    neo::display::set_fg(7);
    if (!addr_pass) all_pass = false;

    neo::memory::free(block);
    kprintf("\n  Result: %s\n\n", all_pass ? "ALL TESTS PASSED" : "FAILURES DETECTED");
}

/* ---- cpu test ---- */
static void cmd_cpu_test()
{
    kprintf("\n  CPU Instruction Test\n\n");
    bool all_pass = true;

    {   int32 r;
        asm volatile("move.l #100,%%d0; add.l #200,%%d0; move.l %%d0,%0"
                     : "=d"(r) : : "d0");
        bool p = (r == 300);
        kprintf("  [%s] ADD.L: 100+200=%d\n", p ? "PASS":"FAIL", r);
        if (!p) all_pass = false;
    }
    {   int32 r;
        asm volatile("move.l #12,%%d0; muls.w #13,%%d0; move.l %%d0,%0"
                     : "=d"(r) : : "d0");
        bool p = (r == 156);
        kprintf("  [%s] MULS.W: 12*13=%d\n", p ? "PASS":"FAIL", r);
        if (!p) all_pass = false;
    }
    {   int32 r;
        asm volatile("move.l #1000,%%d0; divs.w #7,%%d0; ext.l %%d0; move.l %%d0,%0"
                     : "=d"(r) : : "d0");
        int16 q = (int16)(r & 0xFFFF);
        bool p = (q == 142);
        kprintf("  [%s] DIVS.W: 1000/7=%d\n", p ? "PASS":"FAIL", q);
        if (!p) all_pass = false;
    }
    {   uint32 r;
        asm volatile("move.l #0xF0F0F0F0,%%d0; and.l #0x0F0F0F0F,%%d0; move.l %%d0,%0"
                     : "=d"(r) : : "d0");
        bool p = (r == 0);
        kprintf("  [%s] AND.L: 0x%08x\n", p ? "PASS":"FAIL", r);
        if (!p) all_pass = false;
    }
    {   uint32 r;
        asm volatile("move.l #1,%%d0; lsl.l #8,%%d0; move.l %%d0,%0"
                     : "=d"(r) : : "d0");
        bool p = (r == INODE_SIZE);
        kprintf("  [%s] LSL.L: 1<<8=%u\n", p ? "PASS":"FAIL", r);
        if (!p) all_pass = false;
    }
    kprintf("\n  Result: %s\n\n", all_pass ? "ALL TESTS PASSED" : "FAILURES DETECTED");
}

/* ---- fpu test ---- */
static void cmd_fpu_test()
{
    kprintf("\n  FPU Test\n\n");
    neo::cpu::CpuInfo cpu; neo::cpu::detect(&cpu);
    if (cpu.fpu_type == neo::cpu::FPU_NONE) {
        kprintf("  No FPU detected - skipping\n\n");
        return;
    }

    bool all_pass = true;
    {   float64 a=3.14159265358979, b=2.0, r;
        asm volatile("fmove.d %1,%%fp0; fmul.d %2,%%fp0; fmove.d %%fp0,%0"
                     : "=m"(r) : "m"(a), "m"(b) : "fp0");
        bool p = (r > 6.28 && r < 6.29);
        kprintf("  [%s] FMUL: pi*2 = %d.%04d\n", p?"PASS":"FAIL",
                (int32)r, (int32)((r-(int32)r)*10000));
        if (!p) all_pass = false;
    }
    {   float64 a=100.0, b=3.0, r;
        asm volatile("fmove.d %1,%%fp0; fdiv.d %2,%%fp0; fmove.d %%fp0,%0"
                     : "=m"(r) : "m"(a), "m"(b) : "fp0");
        bool p = (r > 33.33 && r < 33.34);
        kprintf("  [%s] FDIV: 100/3 = %d.%04d\n", p?"PASS":"FAIL",
                (int32)r, (int32)((r-(int32)r)*10000));
        if (!p) all_pass = false;
    }
    {   float64 a=144.0, r;
        asm volatile("fmove.d %1,%%fp0; fsqrt.x %%fp0; fmove.d %%fp0,%0"
                     : "=m"(r) : "m"(a) : "fp0");
        bool p = (r > 11.99 && r < 12.01);
        kprintf("  [%s] FSQRT: sqrt(144) = %d.%04d\n", p?"PASS":"FAIL",
                (int32)r, (int32)((r-(int32)r)*10000));
        if (!p) all_pass = false;
    }
    {   float64 a=1.5707963267948966, r;  /* pi/2 */
        asm volatile("fmove.d %1,%%fp0; fsin.x %%fp0; fmove.d %%fp0,%0"
                     : "=m"(r) : "m"(a) : "fp0");
        bool p = (r > 0.999 && r < 1.001);
        kprintf("  [%s] FSIN: sin(pi/2) = %d.%04d\n", p?"PASS":"FAIL",
                (int32)r, (int32)((r-(int32)r)*10000));
        if (!p) all_pass = false;
    }
    kprintf("\n  Result: %s\n\n", all_pass ? "ALL TESTS PASSED" : "FAILURES DETECTED");
}

/* ---- irq stats ---- */
static void cmd_irq_stats()
{
    neo::interrupts::Stats stats;
    neo::interrupts::get_stats(&stats);

    kprintf("\n  Interrupt Statistics:\n\n");
    kprintf("  %-8s %-20s %10s\n", "Level", "Source", "Count");
    kprintf("  ──────── ──────────────────── ──────────\n");
    kprintf("  %-8s %-20s %10u\n", "IRQ 1", "TBE/DSKBLK/SOFT",  stats.level_counts[1]);
    kprintf("  %-8s %-20s %10u\n", "IRQ 2", "PORTS (CIA-A/KB)",  stats.level_counts[2]);
    kprintf("  %-8s %-20s %10u\n", "IRQ 3", "COPPER/VERTB/BLIT", stats.level_counts[3]);
    kprintf("  %-8s %-20s %10u\n", "IRQ 4", "AUDIO (Ch 0-3)",    stats.level_counts[4]);
    kprintf("  %-8s %-20s %10u\n", "IRQ 5", "RBF/DSKSYN",        stats.level_counts[5]);
    kprintf("  %-8s %-20s %10u\n", "IRQ 6", "EXTER (CIA-B)",     stats.level_counts[6]);

    uint32 total = 0;
    for (int i = 1; i <= 6; i++) total += stats.level_counts[i];
    kprintf("\n  Total interrupts: %u  Spurious: %u\n\n", total, stats.spurious);
}

/* ---- dma stats ---- */
static void cmd_dma_stats()
{
    kprintf("\n  DMA Channel Usage:\n\n");
    uint16 dmaconr = CUSTOM_DMACONR();
    kprintf("  %-16s %s\n", "Channel",   "Status");
    kprintf("  ──────────────── ──────\n");
    kprintf("  %-16s %s\n", "Master",    (dmaconr & 0x0200) ? "ON"     : "OFF");
    kprintf("  %-16s %s\n", "Blitter",   (dmaconr & 0x0040) ? "ACTIVE" : "idle");
    kprintf("  %-16s %s\n", "Copper",    (dmaconr & 0x0080) ? "ACTIVE" : "idle");
    kprintf("  %-16s %s\n", "Bitplane",  (dmaconr & 0x0100) ? "ACTIVE" : "idle");
    kprintf("  %-16s %s\n", "Sprite",    (dmaconr & 0x0020) ? "ACTIVE" : "idle");
    kprintf("  %-16s %s\n", "Disk",      (dmaconr & 0x0010) ? "ACTIVE" : "idle");
    kprintf("  %-16s %s\n", "Audio 0-3",
            (dmaconr & 0x000F) ? "ACTIVE" : "idle");
    kprintf("\n  DMACONR raw: 0x%04x\n\n", dmaconr);
}

/* ---- cache stats ---- */
static void cmd_cache_stats()
{
    neo::cpu::CpuInfo cpu; neo::cpu::detect(&cpu);

    kprintf("\n  Cache Information:\n\n");
    kprintf("  %-20s %s\n", "Data Cache:",        cpu.dcache_on  ? "Enabled"  : "Disabled");
    kprintf("  %-20s %s\n", "Instruction Cache:", cpu.icache_on  ? "Enabled"  : "Disabled");
    kprintf("  %-20s %s\n", "Write Mode:",        cpu.dcache_on  ? "Copyback" : "N/A");

    if (cpu.type == neo::cpu::CPU_68060) {
        kprintf("  %-20s %s\n", "Branch Cache:", "Enabled");
        kprintf("  %-20s %s\n", "Store Buffer:", "Enabled");
        kprintf("  %-20s %s\n", "Superscalar:",  "Yes (2-way)");
    } else if (cpu.type == neo::cpu::CPU_68040) {
        kprintf("  %-20s %s\n", "D-Cache Size:", "4 KB");
        kprintf("  %-20s %s\n", "I-Cache Size:", "4 KB");
    } else {
        /* 68030 */
        kprintf("  %-20s %s\n", "D-Cache Size:", "INODE_SIZE bytes");
        kprintf("  %-20s %s\n", "I-Cache Size:", "INODE_SIZE bytes");
    }

    uint32 cacr;
    asm volatile("movec %%cacr, %0" : "=d"(cacr));
    kprintf("\n  CACR: 0x%08x\n\n", cacr);
}

/* ---- help ---- */
static void cmd_help()
{
    kprintf("\nNeoCLI System Administration Tool v1.0\n\n");
    kprintf("  %-30s %s\n", "status",              "System dashboard");
    kprintf("  %-30s %s\n", "sysinfo",             "Detailed hardware info");
    kprintf("  %-30s %s\n", "benchmark",           "CPU/FPU/Memory benchmark");
    kprintf("  %-30s %s\n", "services",            "Running kernel services");
    kprintf("  %-30s %s\n", "config show",         "Show configuration");
    kprintf("  %-30s %s\n", "config set K V",      "Set config value");
    kprintf("  %-30s %s\n", "mem dump <addr> <n>", "Hex dump memory");
    kprintf("  %-30s %s\n", "mem test",            "Memory pattern test");
    kprintf("  %-30s %s\n", "cpu test",            "CPU instruction test");
    kprintf("  %-30s %s\n", "fpu test",            "FPU calculation test");
    kprintf("  %-30s %s\n", "irq stats",           "Interrupt statistics");
    kprintf("  %-30s %s\n", "dma stats",           "DMA channel usage");
    kprintf("  %-30s %s\n", "cache stats",         "Cache information");
    kprintf("  %-30s %s\n", "help",                "This help text");
    kprintf("  %-30s %s\n", "exit / quit",         "Return to NeoShell");
    kprintf("\n");
}

/* ======================================================================
 * Command dispatch
 * ====================================================================== */

static bool dispatch(uint32 argc, char **argv)
{
    if (argc < 2) { cmd_help(); return true; }
    const char *sub = argv[1];

    if (strcmp(sub, "help")      == 0) { cmd_help();      return true; }
    if (strcmp(sub, "exit")      == 0) return false;
    if (strcmp(sub, "quit")      == 0) return false;
    if (strcmp(sub, "status")    == 0) { cmd_status();    return true; }
    if (strcmp(sub, "sysinfo")   == 0) { cmd_sysinfo();   return true; }
    if (strcmp(sub, "benchmark") == 0) { cmd_benchmark(); return true; }
    if (strcmp(sub, "services")  == 0) { cmd_services();  return true; }

    if (strcmp(sub, "config") == 0) {
        if (argc >= 3 && strcmp(argv[2], "show") == 0)
            cmd_config_show();
        else if (argc >= 5 && strcmp(argv[2], "set") == 0)
            cmd_config_set(argv[3], argv[4]);
        else
            kprintf("Usage: config show | config set <key> <value>\n");
        return true;
    }

    if (strcmp(sub, "mem") == 0) {
        if (argc >= 3 && strcmp(argv[2], "test") == 0) {
            cmd_mem_test();
        } else if (argc >= 5 && strcmp(argv[2], "dump") == 0) {
            uint32 addr = 0;
            const char *p = argv[3];
            if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
            while (*p) {
                addr <<= 4;
                if      (*p >= '0' && *p <= '9') addr |= (uint32)(*p - '0');
                else if (*p >= 'a' && *p <= 'f') addr |= (uint32)(*p - 'a' + 10);
                else if (*p >= 'A' && *p <= 'F') addr |= (uint32)(*p - 'A' + 10);
                p++;
            }
            uint32 len = (uint32)atoi(argv[4]);
            cmd_mem_dump(addr, len);
        } else {
            kprintf("Usage: mem dump <addr> <len> | mem test\n");
        }
        return true;
    }

    if (strcmp(sub, "cpu") == 0   && argc >= 3 && strcmp(argv[2], "test")  == 0)
        { cmd_cpu_test();    return true; }
    if (strcmp(sub, "fpu") == 0   && argc >= 3 && strcmp(argv[2], "test")  == 0)
        { cmd_fpu_test();    return true; }
    if (strcmp(sub, "irq") == 0   && argc >= 3 && strcmp(argv[2], "stats") == 0)
        { cmd_irq_stats();   return true; }
    if (strcmp(sub, "dma") == 0   && argc >= 3 && strcmp(argv[2], "stats") == 0)
        { cmd_dma_stats();   return true; }
    if (strcmp(sub, "cache") == 0 && argc >= 3 && strcmp(argv[2], "stats") == 0)
        { cmd_cache_stats(); return true; }

    kprintf("neocli: unknown command '%s'. Type 'help'.\n", sub);
    return true;
}

/* ======================================================================
 * Entry point (called from NeoShell)
 * ====================================================================== */

void main(uint32 argc, char **argv)
{
    if (argc > 1) { dispatch(argc, argv); return; }

    kprintf("\nNeoCLI v1.0 - System Administration Tool\n");
    kprintf("Type 'help' for commands, 'exit' to return.\n\n");

    char    line_buf[INODE_SIZE];
    char   *line_argv[16];
    bool    running = true;

    while (running) {
        neo::display::set_fg(3); kprintf("neocli> ");
        neo::display::set_fg(7);

        uint32 len = neo::console::getline(line_buf, sizeof(line_buf), nullptr);
        if (len == 0) continue;

        uint32 ac = 0;
        line_argv[ac++] = (char *)"neocli";
        char *p = line_buf;
        while (*p && ac < 15) {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            line_argv[ac++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
        running = dispatch(ac, line_argv);
    }

    kprintf("Returning to NeoShell.\n\n");
}

void init() {}
void run()  { main(0, nullptr); }

} /* namespace cli */
} /* namespace neo */
