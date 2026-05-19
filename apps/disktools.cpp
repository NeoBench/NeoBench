// disktools.cpp - corrected
// Bugs fixed:
//
//  1. FREE PARTITION REMAINING BLOCKS UNDERFLOW.
//     load_partitions() computed:
//       pfree.block_count = total_blocks - pfree.start_block;
//     pfree.start_block = 2 + total_blocks/2 + total_blocks/4
//     If total_blocks is small (< 8), the division results round down
//     and pfree.start_block can equal or exceed total_blocks, making
//     pfree.block_count wrap to a huge unsigned value.
//     Fixed: guard with a check and clamp to 0.
//
//  2. PARTITION NAME NUMBERING USES partition_count AFTER INCREMENT.
//     When creating a new partition from free space, the code builds a
//     name using:
//       name[2] = '0' + partition_count;
//     But partition_count has already been incremented by the free-space
//     entry, so the name would be "DH2:" for the first user partition
//     instead of "DH0:".  The name should reflect the index of this
//     particular partition, not the global count.
//     Fixed: track a separate counter for named partitions.
//
//  3. mem::alloc RETURN NOT CAST TO unsigned char*.
//     neo::mem::alloc() returns void*.  The original cast was fine in C++
//     but the comparison `if (!buf)` should check against nullptr.
//     No change needed (implicit bool conversion of pointer is fine),
//     but the cast is added for clarity and correctness.
//
//  4. buf_block DECLARED BUT NEVER USED (compiler warning / dead code).
//     Removed.

#include "../include/neobench.h"
#include "../lib/string.h"

namespace disktools {

static const int MAX_PARTITIONS = 16;
static const int MAX_DRIVES     = 4;

struct Partition {
    char          name[32];
    unsigned long start_block;
    unsigned long block_count;
    char          fs_type[16];
    bool          active;
};

static neo::storage::DeviceInfo drives[MAX_DRIVES];
static int                       drive_count     = 0;
static Partition                 partitions[MAX_PARTITIONS];
static int                       partition_count = 0;
static int                       current_drive   = 0;

static void draw_header()
{
    neo::display::clear();
    neo::display::set_color(14, 1);
    int w = neo::display::get_width();
    neo::display::set_cursor(0, 0);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(2, 0);
    neo::display::printf("DiskTools v1.0 - Disk Management Suite");
    neo::display::set_color(7, 0);
    neo::display::set_cursor(0, 1);
    for (int i = 0; i < w; i++) neo::display::putchar('-');
}

static void wait_key()
{
    neo::display::printf("\n  Press any key...");
    while (!neo::keyboard::key_available()) neo::proc::yield();
    neo::keyboard::read_scancode();
}

static char read_key()
{
    while (!neo::keyboard::key_available()) neo::proc::yield();
    unsigned char sc = neo::keyboard::read_scancode();
    return neo::keyboard::translate(sc, neo::keyboard::is_shift_down());
}

static void probe_drives()
{
    neo::storage::ide::probe();
    drive_count = (int)neo::storage::ide::detect_drives(drives, MAX_DRIVES);

    if (drive_count == 0) {
        neo::storage::scsi::probe();
        drive_count = (int)neo::storage::scsi::detect_drives(drives, MAX_DRIVES);
    }
}

/* Build a default partition layout from a detected drive.
 *
 * BUG FIX 1: pfree.block_count underflow.
 * The free space remaining must be clamped to 0 when the two named
 * partitions already consume all available blocks (can happen on small
 * volumes where integer division rounds aggressively).
 *
 * BUG FIX 2: partition name numbering.
 * We keep a separate named_count that only increments for non-FREE
 * entries so the device names reflect their own partition index.
 */
static void load_partitions(int drv)
{
    partition_count = 0;
    if (drv < 0 || drv >= drive_count) return;

    /* Convert size_mb to 512-byte blocks (1 MB = 2048 sectors) */
    unsigned long total_blocks = drives[drv].size_mb * 2048UL;

    int named_count = 0; /* counts only active (non-FREE) partitions */

    Partition& p0 = partitions[partition_count++];
    neo_strcpy(p0.name, "DH0:");
    p0.start_block  = 2;
    p0.block_count  = total_blocks / 2;
    neo_strcpy(p0.fs_type, "NBFS");
    p0.active = true;
    named_count++;

    if (total_blocks > 4096) {
        Partition& p1 = partitions[partition_count++];
        /* Use named_count for the device letter so it reads "DH1:" */
        char name[8];
        name[0] = 'D'; name[1] = 'H';
        name[2] = (char)('0' + named_count);
        name[3] = ':'; name[4] = '\0';
        neo_strcpy(p1.name, name);
        p1.start_block  = 2 + total_blocks / 2;
        p1.block_count  = total_blocks / 4;
        neo_strcpy(p1.fs_type, "FFS");
        p1.active = true;
        named_count++;

        /* Free space: whatever is left after the two named partitions */
        unsigned long free_start = p1.start_block + p1.block_count;
        if (free_start < total_blocks) {
            Partition& pfree = partitions[partition_count++];
            neo_strcpy(pfree.name, "FREE");
            pfree.start_block = free_start;
            pfree.block_count = total_blocks - free_start;
            neo_strcpy(pfree.fs_type, "FREE");
            pfree.active = false;
        }
    }
    (void)named_count;
}

/* -----------------------------------------------------------------------
 * Partition Editor
 * ----------------------------------------------------------------------- */
static void partition_editor()
{
    int selected = 0;

    while (true) {
        draw_header();
        neo::display::set_cursor(0, 3);
        neo::display::set_color(15, 0);

        if (drive_count == 0) {
            neo::display::printf("  No drives detected.\n");
            wait_key();
            return;
        }

        neo::display::printf("  Partition Editor - %s (%lu MB)\n\n",
            drives[current_drive].model,
            (unsigned long)drives[current_drive].size_mb);
        neo::display::set_color(7, 0);

        int map_width = neo::display::get_width() - 8;
        if (map_width > 60) map_width = 60;

        unsigned long total_blocks = 0;
        for (int i = 0; i < partition_count; i++)
            total_blocks += partitions[i].block_count;
        if (total_blocks == 0) total_blocks = 1;

        neo::display::printf("  Disk Map:\n  [");
        for (int i = 0; i < partition_count; i++) {
            int w = (int)((partitions[i].block_count * (unsigned long)map_width) / total_blocks);
            if (w < 1) w = 1;

            if (i == selected)
                neo::display::set_color(0, 14);
            else if (neo_strcmp(partitions[i].fs_type, "FREE") == 0)
                neo::display::set_fg(8);
            else if (neo_strcmp(partitions[i].fs_type, "NBFS") == 0)
                neo::display::set_fg(10);
            else
                neo::display::set_fg(11);

            for (int j = 0; j < w; j++)
                neo::display::putchar(neo_strcmp(partitions[i].fs_type, "FREE") == 0 ? '.' : '#');

            neo::display::set_color(7, 0);
            if (i < partition_count - 1) neo::display::putchar('|');
        }
        neo::display::printf("]\n\n");

        neo::display::printf("  +---+--------+------------+------------+--------+--------+\n");
        neo::display::printf("  | # | Name   | Start      | Blocks     | Size   | Type   |\n");
        neo::display::printf("  +---+--------+------------+------------+--------+--------+\n");

        for (int i = 0; i < partition_count; i++) {
            Partition& p = partitions[i];
            unsigned long size_mb = (p.block_count * 512UL) / (1024UL * 1024UL);
            if (size_mb == 0) size_mb = 1;

            if (i == selected) neo::display::set_color(0, 14);
            neo::display::printf("  | %d | %-6s | %10lu | %10lu | %4luMB | %-6s |\n",
                i + 1, p.name, p.start_block, p.block_count, size_mb, p.fs_type);
            if (i == selected) neo::display::set_color(7, 0);
        }
        neo::display::printf("  +---+--------+------------+------------+--------+--------+\n\n");
        neo::display::printf("  [Up/Down] Select  [C] Create  [D] Delete  [F] Format  [Q] Back\n");

        char ch = read_key();
        switch (ch) {
        case 'k': case 'K':
            if (selected > 0) selected--;
            break;
        case 'j': case 'J':
            if (selected < partition_count - 1) selected++;
            break;
        case 'c': case 'C':
            if (selected >= 0 && selected < partition_count &&
                neo_strcmp(partitions[selected].fs_type, "FREE") == 0) {
                neo_strcpy(partitions[selected].fs_type, "NBFS");
                /* Name this partition after its index among active partitions */
                int active_idx = 0;
                for (int i = 0; i < selected; i++)
                    if (neo_strcmp(partitions[i].fs_type, "FREE") != 0) active_idx++;
                char name[8];
                name[0] = 'D'; name[1] = 'H';
                name[2] = (char)('0' + active_idx);
                name[3] = ':'; name[4] = '\0';
                neo_strcpy(partitions[selected].name, name);
                partitions[selected].active = true;
            } else {
                neo::display::set_cursor(2, 22);
                neo::display::set_fg(12);
                neo::display::printf("  Select free space to create partition.");
                neo::display::set_fg(7);
                neo::timer::delay_ms(1000);
            }
            break;
        case 'd': case 'D':
            if (selected >= 0 && selected < partition_count &&
                neo_strcmp(partitions[selected].fs_type, "FREE") != 0) {
                neo::display::set_cursor(2, 22);
                neo::display::printf("  Delete partition %s? [Y/N] ", partitions[selected].name);
                char confirm = read_key();
                if (confirm == 'y' || confirm == 'Y') {
                    neo_strcpy(partitions[selected].fs_type, "FREE");
                    neo_strcpy(partitions[selected].name, "FREE");
                    partitions[selected].active = false;
                }
            }
            break;
        case 'f': case 'F':
            if (selected >= 0 && selected < partition_count &&
                neo_strcmp(partitions[selected].fs_type, "FREE") != 0) {
                neo::display::set_cursor(2, 22);
                neo::display::printf("  Format %s as NBFS? [Y/N] ", partitions[selected].name);
                char confirm = read_key();
                if (confirm == 'y' || confirm == 'Y') {
                    neo::display::set_cursor(2, 23);
                    neo::display::printf("  Formatting...");
                    int total = 20;
                    for (int f = 0; f <= total; f++) {
                        neo::display::set_cursor(18, 23);
                        neo::display::printf("[");
                        for (int b = 0; b < total; b++)
                            neo::display::putchar(b < f ? '#' : '.');
                        neo::display::printf("] %d%%", (f * 100) / total);
                        neo::timer::delay_ms(100);
                    }
                    neo_strcpy(partitions[selected].fs_type, "NBFS");
                    neo::display::set_cursor(2, 24);
                    neo::display::set_fg(10);
                    neo::display::printf("  Format complete!");
                    neo::display::set_fg(7);
                    neo::timer::delay_ms(1000);
                }
            }
            break;
        case 'q': case 'Q': return;
        }
    }
}

/* -----------------------------------------------------------------------
 * Disk Usage Viewer
 * ----------------------------------------------------------------------- */
static void disk_usage()
{
    draw_header();
    neo::display::set_cursor(0, 3);
    neo::display::set_color(15, 0);
    neo::display::printf("  Disk Usage\n\n");
    neo::display::set_color(7, 0);

    neo::filesystem::MountInfo mounts[8];
    int mount_count = (int)neo::filesystem::list_mounts(mounts, 8);

    neo::display::printf("  +----------+----------+---------+---------+------+------+\n");
    neo::display::printf("  | Mount    | Device   | Total   | Free    | Used | Bar  |\n");
    neo::display::printf("  +----------+----------+---------+---------+------+------+\n");

    for (int i = 0; i < mount_count; i++) {
        neo::filesystem::MountInfo& m = mounts[i];
        unsigned long total_kb = (m.total_blocks * m.block_size) / 1024;
        unsigned long free_kb  = (m.free_blocks  * m.block_size) / 1024;
        unsigned long used_kb  = total_kb > free_kb ? total_kb - free_kb : 0;
        int pct = (total_kb > 0) ? (int)((used_kb * 100UL) / total_kb) : 0;

        neo::display::printf("  | %-8s | %-8s | %5luKB | %5luKB | %3d%% | ",
            m.mount_point, m.device, total_kb, free_kb, pct);

        int bar_w  = 10;
        int filled = (pct * bar_w) / 100;
        neo::display::set_fg(pct > 90 ? 12 : pct > 70 ? 14 : 10);
        for (int b = 0; b < bar_w; b++)
            neo::display::putchar(b < filled ? '#' : '.');
        neo::display::set_fg(7);
        neo::display::printf(" |\n");
    }
    neo::display::printf("  +----------+----------+---------+---------+------+------+\n\n");

    neo::display::set_color(15, 0);
    neo::display::printf("  Memory Usage:\n");
    neo::display::set_color(7, 0);

    unsigned long total_mem = neo::mem::get_total_mem();
    unsigned long free_mem  = neo::mem::get_free_mem();
    unsigned long used_mem  = total_mem > free_mem ? total_mem - free_mem : 0;
    int mem_pct = (total_mem > 0) ? (int)((used_mem * 100UL) / total_mem) : 0;

    neo::display::printf("  Total: %lu KB  Free: %lu KB  Used: %lu KB (%d%%)\n",
        total_mem / 1024, free_mem / 1024, used_mem / 1024, mem_pct);
    neo::display::printf("  Chip RAM: %lu KB free  |  Fast RAM: %lu KB free\n",
        neo::mem::get_free_chip() / 1024, neo::mem::get_free_fast() / 1024);

    neo::display::printf("\n  RAM: [");
    int bar_w  = 40;
    int filled = (mem_pct * bar_w) / 100;
    neo::display::set_fg(mem_pct > 90 ? 12 : mem_pct > 70 ? 14 : 10);
    for (int b = 0; b < bar_w; b++)
        neo::display::putchar(b < filled ? '#' : '.');
    neo::display::set_fg(7);
    neo::display::printf("] %d%%\n", mem_pct);

    wait_key();
}

/* -----------------------------------------------------------------------
 * Disk Cloner
 * ----------------------------------------------------------------------- */
static void disk_cloner()
{
    draw_header();
    neo::display::set_cursor(0, 3);
    neo::display::set_color(15, 0);
    neo::display::printf("  Disk Cloner (Block-by-Block Copy)\n\n");
    neo::display::set_color(7, 0);

    if (drive_count < 2) {
        neo::display::set_fg(12);
        neo::display::printf("  Need at least 2 drives for cloning.\n");
        neo::display::printf("  Detected drives: %d\n\n", drive_count);
        neo::display::set_fg(7);

        if (drive_count > 0) {
            neo::display::printf("  Available drives:\n");
            for (int i = 0; i < drive_count; i++)
                neo::display::printf("    %d. %s (%lu MB)\n",
                    i + 1, drives[i].model, (unsigned long)drives[i].size_mb);
        }

        neo::display::printf("\n  Running in simulation mode...\n\n");

        unsigned long blocks = drive_count > 0 ? drives[0].size_mb * 2048UL : 10240UL;
        if (blocks > 5000) blocks = 5000;

        neo::display::printf("  Simulating clone of %lu blocks:\n\n", blocks);

        /* BUG FIX 3: buf_block removed (unused variable warning).
         * The original declared it but never used it. */
        unsigned char* buf = (unsigned char*)neo::mem::alloc(512);
        if (!buf) {
            neo::display::printf("  ERROR: Cannot allocate buffer\n");
            wait_key();
            return;
        }

        for (unsigned long b = 0; b < blocks; b += 100) {
            int pct = (int)((b * 100UL) / blocks);
            neo::display::set_cursor(2, 14);
            neo::display::printf("  Cloning: [");
            int bar = 40;
            int fill = (pct * bar) / 100;
            neo::display::set_fg(11);
            for (int i = 0; i < bar; i++)
                neo::display::putchar(i < fill ? '=' : '-');
            neo::display::set_fg(7);
            neo::display::printf("] %3d%%  Block %lu/%lu", pct, b, blocks);
            neo::display::clear_eol();
            neo::timer::delay_ms(20);
        }

        neo::display::set_cursor(2, 14);
        neo::display::printf("  Cloning: [");
        neo::display::set_fg(10);
        for (int i = 0; i < 40; i++) neo::display::putchar('=');
        neo::display::set_fg(7);
        neo::display::printf("] 100%%  Complete!           ");

        neo::mem::free(buf);
    } else {
        neo::display::printf("  Source drives:\n");
        for (int i = 0; i < drive_count; i++)
            neo::display::printf("    %d. %s (%lu MB)%s\n",
                i + 1, drives[i].model, (unsigned long)drives[i].size_mb,
                drives[i].is_master ? " [Master]" : "");

        neo::display::printf("\n  Select source drive [1-%d]: ", drive_count);
        char src_ch = read_key();
        int src = src_ch - '1';
        if (src < 0 || src >= drive_count) return;

        neo::display::printf("%d\n", src + 1);
        neo::display::printf("  Select destination drive [1-%d]: ", drive_count);
        char dst_ch = read_key();
        int dst = dst_ch - '1';
        if (dst < 0 || dst >= drive_count || dst == src) {
            neo::display::printf("\n  Invalid selection.\n");
            wait_key();
            return;
        }

        neo::display::printf("%d\n\n", dst + 1);
        neo::display::set_fg(12);
        neo::display::printf("  WARNING: This will OVERWRITE all data on %s!\n", drives[dst].model);
        neo::display::set_fg(7);
        neo::display::printf("  Continue? [Y/N] ");

        char confirm = read_key();
        if (confirm != 'y' && confirm != 'Y') return;

        unsigned long blocks = drives[src].size_mb * 2048UL;
        unsigned char* buf = (unsigned char*)neo::mem::alloc(512);
        if (!buf) {
            neo::display::printf("\n  ERROR: Cannot allocate buffer\n");
            wait_key();
            return;
        }

        neo::display::printf("\n\n  Cloning %lu blocks...\n", blocks);

        for (unsigned long b = 0; b < blocks; b++) {
            neo::storage::ide::read_block_cb(drives[src].partition_start + b, buf);
            neo::storage::ide::write_block_cb(drives[dst].partition_start + b, buf);

            if ((b & 0xFF) == 0) {
                int pct = (int)((b * 100UL) / blocks);
                neo::display::set_cursor(2, 16);
                neo::display::printf("  Progress: [");
                int bar  = 40;
                int fill = (pct * bar) / 100;
                for (int i = 0; i < bar; i++)
                    neo::display::putchar(i < fill ? '=' : '-');
                neo::display::printf("] %3d%%", pct);
            }
        }

        neo::mem::free(buf);
        neo::display::set_cursor(2, 18);
        neo::display::set_fg(10);
        neo::display::printf("  Clone complete!\n");
        neo::display::set_fg(7);
    }

    wait_key();
}

/* -----------------------------------------------------------------------
 * Drive Info
 * ----------------------------------------------------------------------- */
static void drive_info()
{
    draw_header();
    neo::display::set_cursor(0, 3);
    neo::display::set_color(15, 0);
    neo::display::printf("  Drive Information\n\n");
    neo::display::set_color(7, 0);

    if (drive_count == 0) {
        neo::display::printf("  No drives detected.\n");
        wait_key();
        return;
    }

    for (int i = 0; i < drive_count; i++) {
        neo::storage::DeviceInfo& d = drives[i];
        neo::display::printf("  Drive %d:\n", i + 1);
        neo::display::printf("  +--------------------------------------+\n");
        neo::display::printf("  | Model:  %-28s |\n", d.model);
        neo::display::printf("  | Size:   %-24lu MB |\n", (unsigned long)d.size_mb);
        neo::display::printf("  | Type:   %-28s |\n", d.is_master ? "Master" : "Slave");
        neo::display::printf("  | Start:  Block %-22lu |\n", (unsigned long)d.partition_start);
        neo::display::printf("  +--------------------------------------+\n\n");
    }

    wait_key();
}

/* -----------------------------------------------------------------------
 * Main Menu
 * ----------------------------------------------------------------------- */
static void main_menu()
{
    while (true) {
        draw_header();
        neo::display::set_cursor(2, 3);
        neo::display::set_color(15, 0);
        neo::display::printf("Main Menu\n\n");
        neo::display::set_color(7, 0);

        neo::display::printf("  [1] Partition Editor\n");
        neo::display::printf("  [2] Disk Usage Viewer\n");
        neo::display::printf("  [3] Drive Information\n");
        neo::display::printf("  [4] Disk Cloner\n");
        neo::display::printf("  [5] Rescan Drives\n");
        neo::display::printf("  [Q] Quit\n\n");
        neo::display::printf("  Drives detected: %d\n", drive_count);

        char ch = read_key();
        switch (ch) {
        case '1': partition_editor(); break;
        case '2': disk_usage(); break;
        case '3': drive_info(); break;
        case '4': disk_cloner(); break;
        case '5':
            probe_drives();
            /* BUG FIX 4: only load partitions if at least one drive found,
             * and keep current_drive in range after rescan. */
            if (drive_count > 0) {
                if (current_drive >= drive_count) current_drive = 0;
                load_partitions(current_drive);
            }
            break;
        case 'q': case 'Q': return;
        }
    }
}

} /* namespace disktools */

extern "C" void app_main(int /*argc*/, char** /*argv*/)
{
    disktools::probe_drives();
    if (disktools::drive_count > 0)
        disktools::load_partitions(0);
    disktools::main_menu();
}
