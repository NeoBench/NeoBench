// nbench.cpp - corrected
// Bugs fixed:
//
//  1. neo::cpu::detect() CALLED WITH WRONG ARGUMENT TYPE.
//     The original passes a CpuInfo value (stack object) directly:
//       neo::cpu::CpuInfo cpu;
//       neo::cpu::detect(cpu);
//     The function signature in neobench.h is:
//       void detect(CpuInfo* info);
//     and the inline wrapper is:
//       inline void detect(CpuInfo& info) { detect(&info); }
//     So passing by reference via the inline wrapper is correct.
//     Actually, passing 'cpu' (not &cpu) invokes the inline overload
//     detect(CpuInfo&) which calls detect(&info).  This is fine.
//     No bug here — the reference overload handles it.
//
//  2. bench_chip_ram CALLS profile_start() TWICE.
//     Two consecutive profile_start() calls: the first starts the timer,
//     the second restarts it (resetting the start time).  Since they're
//     back-to-back with no work between them, the effect is just one
//     profile_start() call.  Not a measurement correctness issue, but
//     a code smell.  Removed the duplicate.
//
//  3. KB/S BANDWIDTH SCORE OVERFLOWS uint32 ON FAST MACHINES.
//     For bench_memory():
//       total_bytes = block_size * passes * 2 = 65536 * 200 * 2 = 26214400
//       (total_bytes / 1024) * 1000 / elapsed
//     If elapsed is 1 ms:  INODE_SIZE00 * 1000 = 25,600,000 which fits in uint32.
//     But if elapsed is 0 (profile_stop can return 0 if sub-millisecond):
//       we guard with: if (elapsed == 0) elapsed = 1;
//     So max value is 25,600,000 which is within uint32 range (4,294,967,295).
//     No overflow here.
//
//  4. Mandelbrot FIXED-POINT SHIFT IS INCONSISTENT.
//     The coordinate system uses scale 1024 = 1.0 (10-bit fractional).
//     cx ranges: -2048 to +1024 = -2.0 to +1.0 in scaled units.
//     cy ranges: -1024 to +1024 = -1.0 to +1.0.
//     The iteration:
//       zx2 = (zx * zx) >> 10;   correct: (zx/1024)^2 = zx^2/1048576 = zx^2>>20
//     Wait: if zx is in units of 1/1024, then zx*zx is in units of 1/1048576.
//     To get back to 1/1024 units: divide by 1024 = >> 10.
//     So zx2 = (zx*zx)>>10 is correct for this scale.
//     The escape check: zx2 + zy2 > 4096 means (|z|^2 > 4 in real units)
//     since 4 * 1024 = 4096. Correct.
//     The cross term: zxy = (zx * zy) >> 9
//     2*zx*zy / 1024 = zx*zy*2 / 1024 = (zx*zy) >> 9. Correct.
//     Then: zx = zx2 - zy2 + cx
//     zx2 and zy2 are in 1/1024 units, cx is in 1/1024 units. Correct.
//     No bug in the Mandelbrot math.
//
//  5. result_count CAN OVERFLOW results[] IF run_single IS CALLED MANY TIMES.
//     result_count starts at 0 and increments each time a benchmark runs.
//     MAX_RESULTS = 8, benchmarks = 5.  If the user runs single tests
//     repeatedly (beyond 8 total calls) result_count exceeds MAX_RESULTS
//     and results[] is written out of bounds.
//     Fixed: check result_count < MAX_RESULTS before adding a result.
//
//  6. view_results RANGE CHECK BUG: ch <= '0' + result_count IS WRONG.
//     The check `ch >= '1' && ch <= '0' + result_count` is correct only
//     when result_count <= 9.  If result_count == 8:
//       ch <= '0' + 8 = '8'  -> accepts '1'..'8'.  Correct.
//     No off-by-one here.

#include "../include/neobench.h"
#include "../lib/string.h"

namespace nbench {

static const int MAX_RESULTS = 8;

struct BenchResult {
    char          name[32];
    unsigned long score;
    unsigned long time_ms;
    char          unit[16];
    bool          completed;
};

static BenchResult results[MAX_RESULTS];
static int         result_count = 0;

static unsigned long rng_state = 12345;
static unsigned long rng_next()
{
    rng_state = rng_state * 1103515245UL + 12345UL;
    return (rng_state >> 16) & 0x7FFF;
}

static void draw_header()
{
    neo::display::clear();
    neo::display::set_color(14, 1);
    neo::display::set_cursor(0, 0);
    int w = neo::display::get_width();
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(2, 0);
    neo::display::printf("NBench v1.0 - NeoBench Benchmarking Suite");
    neo::display::set_color(7, 0);
    neo::display::set_cursor(0, 1);
    for (int i = 0; i < w; i++) neo::display::putchar('-');
}

static void draw_progress(const char* label, int percent)
{
    int w = neo::display::get_width() - 20;
    if (w > 50) w = 50;
    neo::display::set_cursor(2, 14);
    neo::display::printf("%-16s [", label);
    int filled = (percent * w) / 100;
    neo::display::set_fg(10);
    for (int i = 0; i < w; i++)
        neo::display::putchar(i < filled ? '#' : '.');
    neo::display::set_fg(7);
    neo::display::printf("] %3d%%", percent);
    neo::display::clear_eol();
}

/* Safely add a result, guarding against result_count overflow (BUG FIX 5) */
static BenchResult* alloc_result()
{
    if (result_count >= MAX_RESULTS) result_count = MAX_RESULTS - 1;
    return &results[result_count++];
}

/* -----------------------------------------------------------------------
 * Benchmark 1: CPU Integer (Dhrystone-like)
 * ----------------------------------------------------------------------- */
static void bench_cpu_integer()
{
    draw_progress("CPU Integer", 0);
    neo::timer::profile_start();

    volatile int a = 1, b = 2, c = 3, r = 0;
    const int iterations = 500000;

    for (int i = 0; i < iterations; i++) {
        r = a + b * c;
        a = b ^ c;
        b = r - a;
        c = a + b + r;
        if (c > 10000)  c = c % 97;
        if (b < -10000) b = b % 53;
        a = (a >> 1) | (b << 3);
        r = a * b + c;

        if ((i & 0xFFFF) == 0)
            draw_progress("CPU Integer", (i * 100) / iterations);
    }

    unsigned long elapsed = neo::timer::profile_stop();
    if (elapsed == 0) elapsed = 1;
    draw_progress("CPU Integer", 100);

    BenchResult* res = alloc_result();
    neo_strcpy(res->name, "CPU Integer");
    res->time_ms   = elapsed;
    res->score     = (unsigned long)((iterations * 1000UL) / elapsed);
    neo_strcpy(res->unit, "Dhrystones");
    res->completed = true;
    (void)r; /* suppress unused warning */
}

/* -----------------------------------------------------------------------
 * Benchmark 2: Fixed-point Mandelbrot
 * ----------------------------------------------------------------------- */
static void bench_fpu()
{
    draw_progress("FPU Mandelbrot", 0);
    neo::timer::profile_start();

    volatile int escape_count = 0;
    const int width    = 80;
    const int height   = 60;
    const int max_iter = 50;

    for (int py = 0; py < height; py++) {
        for (int px = 0; px < width; px++) {
            long cx = -2048L + ((long)px * 3072L) / width;
            long cy = -1024L + ((long)py * 2048L) / height;
            long zx = 0, zy = 0;
            int iter = 0;
            while (iter < max_iter) {
                long zx2 = (zx * zx) >> 10;
                long zy2 = (zy * zy) >> 10;
                if (zx2 + zy2 > 4096L) break;
                long zxy = (zx * zy) >> 9;
                zx = zx2 - zy2 + cx;
                zy = zxy + cy;
                iter++;
            }
            if (iter == max_iter) escape_count++;
        }
        if ((py & 3) == 0)
            draw_progress("FPU Mandelbrot", (py * 100) / height);
    }

    unsigned long elapsed = neo::timer::profile_stop();
    if (elapsed == 0) elapsed = 1;
    draw_progress("FPU Mandelbrot", 100);

    BenchResult* res = alloc_result();
    neo_strcpy(res->name, "FPU Mandelbrot");
    res->time_ms   = elapsed;
    res->score     = (unsigned long)(((unsigned long)width * height * max_iter * 1000UL) / elapsed);
    neo_strcpy(res->unit, "MPixIter/s");
    res->completed = true;
    (void)escape_count;
}

/* -----------------------------------------------------------------------
 * Benchmark 3: Memory Bandwidth
 * ----------------------------------------------------------------------- */
static void bench_memory()
{
    draw_progress("Memory BW", 0);

    const unsigned long block_size = 65536;
    unsigned char* src = (unsigned char*)neo::mem::alloc(block_size);
    unsigned char* dst = (unsigned char*)neo::mem::alloc(block_size);

    if (!src || !dst) {
        if (src) neo::mem::free(src);
        if (dst) neo::mem::free(dst);
        BenchResult* res = alloc_result();
        neo_strcpy(res->name, "Memory BW");
        res->time_ms = 0; res->score = 0;
        neo_strcpy(res->unit, "KB/s");
        res->completed = false;
        return;
    }

    for (unsigned long i = 0; i < block_size; i++)
        src[i] = (unsigned char)(i & 0xFF);

    neo::timer::profile_start();
    const int passes = 200;

    for (int p = 0; p < passes; p++) {
        neo_memcpy(dst, src, block_size);
        if ((p & 15) == 0) draw_progress("Memory BW", (p * 50) / passes);
    }
    for (int p = 0; p < passes; p++) {
        neo_memset(dst, (unsigned char)(p & 0xFF), block_size);
        if ((p & 15) == 0) draw_progress("Memory BW", 50 + (p * 50) / passes);
    }

    unsigned long elapsed = neo::timer::profile_stop();
    if (elapsed == 0) elapsed = 1;
    draw_progress("Memory BW", 100);

    unsigned long total_bytes  = block_size * (unsigned long)passes * 2UL;
    unsigned long kb_per_sec   = (total_bytes / 1024UL) * 1000UL / elapsed;

    neo::mem::free(src);
    neo::mem::free(dst);

    BenchResult* res = alloc_result();
    neo_strcpy(res->name, "Memory BW");
    res->time_ms   = elapsed;
    res->score     = kb_per_sec;
    neo_strcpy(res->unit, "KB/s");
    res->completed = true;
}

/* -----------------------------------------------------------------------
 * Benchmark 4: Disk I/O (sequential block read)
 * ----------------------------------------------------------------------- */
static void bench_disk()
{
    draw_progress("Disk I/O", 0);

    unsigned char* buf = (unsigned char*)neo::mem::alloc(512);
    if (!buf) {
        BenchResult* res = alloc_result();
        neo_strcpy(res->name, "Disk I/O");
        res->time_ms = 0; res->score = 0;
        neo_strcpy(res->unit, "KB/s");
        res->completed = false;
        return;
    }

    neo::storage::ide::probe();
    neo::storage::DeviceInfo devs[4];
    int ndev = (int)neo::storage::ide::detect_drives(devs, 4);

    if (ndev <= 0) {
        neo::mem::free(buf);
        BenchResult* res = alloc_result();
        neo_strcpy(res->name, "Disk I/O");
        res->time_ms = 0; res->score = 0;
        neo_strcpy(res->unit, "KB/s");
        res->completed = false;
        return;
    }

    neo::timer::profile_start();
    const int blocks_to_read = 500;

    for (int i = 0; i < blocks_to_read; i++) {
        neo::storage::ide::read_block_cb(i, buf);
        if ((i & 31) == 0)
            draw_progress("Disk I/O", (i * 100) / blocks_to_read);
    }

    unsigned long elapsed = neo::timer::profile_stop();
    if (elapsed == 0) elapsed = 1;
    draw_progress("Disk I/O", 100);

    unsigned long total_kb  = (unsigned long)(blocks_to_read * 512) / 1024UL;
    unsigned long kb_per_sec = total_kb * 1000UL / elapsed;

    neo::mem::free(buf);

    BenchResult* res = alloc_result();
    neo_strcpy(res->name, "Disk I/O Read");
    res->time_ms   = elapsed;
    res->score     = kb_per_sec;
    neo_strcpy(res->unit, "KB/s");
    res->completed = true;
}

/* -----------------------------------------------------------------------
 * Benchmark 5: Chip RAM bandwidth
 * ----------------------------------------------------------------------- */
static void bench_chip_ram()
{
    draw_progress("Chip RAM", 0);

    const unsigned long block_size = 32768;
    unsigned char* chip = (unsigned char*)neo::mem::alloc_chip(block_size);

    if (!chip) {
        BenchResult* res = alloc_result();
        neo_strcpy(res->name, "Chip RAM BW");
        res->time_ms = 0; res->score = 0;
        neo_strcpy(res->unit, "KB/s");
        res->completed = false;
        return;
    }

    /* BUG FIX 2: removed duplicate profile_start() call */
    neo::timer::profile_start();
    const int passes = 300;

    for (int p = 0; p < passes; p++) {
        neo_memset(chip, (unsigned char)(p & 0xFF), block_size);
        if ((p & 15) == 0)
            draw_progress("Chip RAM", (p * 100) / passes);
    }

    unsigned long elapsed = neo::timer::profile_stop();
    if (elapsed == 0) elapsed = 1;
    draw_progress("Chip RAM", 100);

    unsigned long total_kb  = (block_size / 1024UL) * (unsigned long)passes;
    unsigned long kb_per_sec = total_kb * 1000UL / elapsed;

    neo::mem::free(chip);

    BenchResult* res = alloc_result();
    neo_strcpy(res->name, "Chip RAM BW");
    res->time_ms   = elapsed;
    res->score     = kb_per_sec;
    neo_strcpy(res->unit, "KB/s");
    res->completed = true;
}

/* -----------------------------------------------------------------------
 * Results display
 * ----------------------------------------------------------------------- */
static void show_results()
{
    neo::display::set_cursor(0, 3);
    neo::display::set_color(15, 0);
    neo::display::printf("  Benchmark Results\n");
    neo::display::set_color(7, 0);
    neo::display::printf("  +---+--------------------+------------+----------+------------+\n");
    neo::display::printf("  | # | Test               | Score      | Unit     | Time (ms)  |\n");
    neo::display::printf("  +---+--------------------+------------+----------+------------+\n");

    unsigned long total_score = 0;
    for (int i = 0; i < result_count; i++) {
        BenchResult& r = results[i];
        if (r.completed) {
            neo::display::printf("  | %d | %-18s | %10lu | %-8s | %10lu |\n",
                i + 1, r.name, r.score, r.unit, r.time_ms);
            total_score += r.score;
        } else {
            neo::display::set_fg(12);
            neo::display::printf("  | %d | %-18s |   SKIPPED  |          |            |\n",
                i + 1, r.name);
            neo::display::set_fg(7);
        }
    }
    neo::display::printf("  +---+--------------------+------------+----------+------------+\n\n");

    neo::display::set_color(14, 0);
    neo::display::printf("  Composite Score: %lu\n\n", total_score);
    neo::display::set_color(7, 0);

    neo::display::set_color(11, 0);
    neo::display::printf("  System Information:\n");
    neo::display::set_color(7, 0);
    neo::cpu::CpuInfo cpu;
    neo::cpu::detect(cpu);
    neo::display::printf("    CPU: 680%d0", (int)cpu.type);
    if (cpu.clock_mhz > 0) neo::display::printf(" @ %u MHz", cpu.clock_mhz);
    neo::display::printf("\n");
    if (cpu.fpu_type) neo::display::printf("    FPU: Type %d\n", (int)cpu.fpu_type);
    neo::display::printf("    Free Memory: %lu KB (Chip: %lu KB, Fast: %lu KB)\n",
        neo::mem::get_free_mem()  / 1024,
        neo::mem::get_free_chip() / 1024,
        neo::mem::get_free_fast() / 1024);
    neo::display::printf("    Uptime: %lu seconds\n", neo::timer::get_uptime_seconds());
}

static void show_detail(int idx)
{
    if (idx < 0 || idx >= result_count) return;
    BenchResult& r = results[idx];

    neo::display::set_cursor(0, 16);
    neo::display::set_color(15, 0);
    neo::display::printf("  Detailed View: %s\n", r.name);
    neo::display::set_color(7, 0);
    neo::display::printf("  +---------------------------------+\n");
    neo::display::printf("  | Score:    %10lu %-8s   |\n", r.score, r.unit);
    neo::display::printf("  | Time:     %10lu ms         |\n", r.time_ms);
    if (r.completed) {
        neo::display::set_fg(10);
        neo::display::printf("  | Status:   COMPLETED              |\n");
    } else {
        neo::display::set_fg(12);
        neo::display::printf("  | Status:   SKIPPED                |\n");
    }
    neo::display::set_fg(7);
    neo::display::printf("  +---------------------------------+\n");

    unsigned long max_score = 1;
    for (int i = 0; i < result_count; i++)
        if (results[i].score > max_score) max_score = results[i].score;

    int bar_len = (int)((r.score * 40UL) / max_score);
    neo::display::printf("  Score: [");
    neo::display::set_fg(10);
    for (int i = 0; i < 40; i++) neo::display::putchar(i < bar_len ? '#' : ' ');
    neo::display::set_fg(7);
    neo::display::printf("]\n");
}

static void show_menu()
{
    draw_header();
    neo::display::set_cursor(2, 3);
    neo::display::set_color(15, 0);
    neo::display::printf("Main Menu\n\n");
    neo::display::set_color(7, 0);
    neo::display::printf("  [1] Run All Benchmarks\n");
    neo::display::printf("  [2] CPU Integer Test\n");
    neo::display::printf("  [3] FPU / Mandelbrot Test\n");
    neo::display::printf("  [4] Memory Bandwidth Test\n");
    neo::display::printf("  [5] Disk I/O Test\n");
    neo::display::printf("  [6] Chip RAM Bandwidth Test\n");
    neo::display::printf("  [7] View Results\n");
    neo::display::printf("  [Q] Quit\n\n");

    if (result_count > 0) {
        neo::display::set_fg(11);
        neo::display::printf("  %d test(s) completed. Press [7] to view.\n", result_count);
        neo::display::set_fg(7);
    }
}

static void run_single(int test)
{
    draw_header();
    neo::display::set_cursor(2, 3);
    neo::display::set_color(15, 0);
    neo::display::printf("Running benchmark...\n\n");
    neo::display::set_color(7, 0);

    switch (test) {
    case 0: bench_cpu_integer(); break;
    case 1: bench_fpu();         break;
    case 2: bench_memory();      break;
    case 3: bench_disk();        break;
    case 4: bench_chip_ram();    break;
    }

    neo::display::set_cursor(2, 16);
    neo::display::set_fg(10);
    neo::display::printf("Complete! Press any key...");
    neo::display::set_fg(7);
    while (!neo::keyboard::key_available()) neo::proc::yield();
    neo::keyboard::read_scancode();
}

static void run_all()
{
    result_count = 0;
    draw_header();
    neo::display::set_cursor(2, 3);
    neo::display::set_color(15, 0);
    neo::display::printf("Running Full Benchmark Suite...\n\n");
    neo::display::set_color(7, 0);

    bench_cpu_integer(); neo::timer::delay_ms(200);
    bench_fpu();         neo::timer::delay_ms(200);
    bench_memory();      neo::timer::delay_ms(200);
    bench_disk();        neo::timer::delay_ms(200);
    bench_chip_ram();

    draw_header();
    show_results();

    neo::display::printf("\n  Press any key to return to menu...");
    while (!neo::keyboard::key_available()) neo::proc::yield();
    neo::keyboard::read_scancode();
}

static void view_results()
{
    if (result_count == 0) {
        draw_header();
        neo::display::set_cursor(2, 3);
        neo::display::set_fg(12);
        neo::display::printf("No results yet. Run benchmarks first!\n");
        neo::display::set_fg(7);
        neo::display::printf("\n  Press any key...");
        while (!neo::keyboard::key_available()) neo::proc::yield();
        neo::keyboard::read_scancode();
        return;
    }

    draw_header();
    show_results();

    neo::display::printf("\n  Press [1-%d] for detail, [Q] to return: ", result_count);

    while (true) {
        if (neo::keyboard::key_available()) {
            unsigned char sc  = neo::keyboard::read_scancode();
            bool          shift = neo::keyboard::is_shift_down();
            char          ch    = neo::keyboard::translate(sc, shift);
            if (ch == 'q' || ch == 'Q') break;
            if (ch >= '1' && ch <= '0' + result_count)
                show_detail(ch - '1');
        }
        neo::proc::yield();
    }
}

} /* namespace nbench */

extern "C" void app_main(int argc, char** argv)
{
    nbench::result_count = 0;
    nbench::rng_state    = neo::timer::get_ticks();
    (void)nbench::rng_next; /* mark as used */

    if (argc > 1) {
        if (neo_strcmp(argv[1], "--all") == 0 || neo_strcmp(argv[1], "-a") == 0) {
            nbench::run_all();
            return;
        }
    }

    while (true) {
        nbench::show_menu();

        while (!neo::keyboard::key_available()) neo::proc::yield();
        unsigned char sc    = neo::keyboard::read_scancode();
        bool          shift = neo::keyboard::is_shift_down();
        char          ch    = neo::keyboard::translate(sc, shift);

        switch (ch) {
        case '1': nbench::run_all();       break;
        case '2': nbench::run_single(0);   break;
        case '3': nbench::run_single(1);   break;
        case '4': nbench::run_single(2);   break;
        case '5': nbench::run_single(3);   break;
        case '6': nbench::run_single(4);   break;
        case '7': nbench::view_results();  break;
        case 'q': case 'Q': return;
        }
    }
}
