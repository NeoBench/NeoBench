#include "../include/neobench.h"
#include "../lib/string.h"

// NeoZip - Archive Manager
// RLE compression, simple archive format, list/extract/create

namespace neozip {

static const int MAX_ENTRIES = 128;
static const int MAX_PATH_LEN = INODE_SIZE;

// NeoZip archive format (.nza)
// Header: "NZA1" (4 bytes) + entry_count (4 bytes)
// Per entry: name (INODE_SIZE bytes) + original_size (4 bytes) + compressed_size (4 bytes) + offset (4 bytes) + method (1 byte)
// Data: compressed file data

static const unsigned long NZA_MAGIC = 0x4E5A4131;  // "NZA1"

enum CompressMethod {
    METHOD_STORE = 0,
    METHOD_RLE = 1
};

struct ArchiveEntry {
    char name[MAX_PATH_LEN];
    unsigned long original_size;
    unsigned long compressed_size;
    unsigned long offset;
    unsigned char method;
};

static ArchiveEntry entries[MAX_ENTRIES];
static int entry_count = 0;
static char archive_path[MAX_PATH_LEN];

// --- RLE Compression ---
// Format: literal bytes as-is, runs of 3+ same byte as: ESC count byte
static const unsigned char RLE_ESCAPE = 0x90;

static unsigned long rle_compress(const unsigned char* src, unsigned long src_len,
                                   unsigned char* dst, unsigned long dst_max) {
    unsigned long si = 0, di = 0;

    while (si < src_len && di < dst_max - 3) {
        unsigned char b = src[si];
        unsigned long run = 1;

        while (si + run < src_len && src[si + run] == b && run < 255) {
            run++;
        }

        if (run >= 3) {
            // RLE encode
            if (di + 3 > dst_max) break;
            dst[di++] = RLE_ESCAPE;
            dst[di++] = (unsigned char)run;
            dst[di++] = b;
            si += run;
        } else {
            // Literal
            if (b == RLE_ESCAPE) {
                if (di + 3 > dst_max) break;
                dst[di++] = RLE_ESCAPE;
                dst[di++] = 1;
                dst[di++] = b;
                si++;
            } else {
                dst[di++] = b;
                si++;
            }
        }
    }

    return di;
}

static unsigned long rle_decompress(const unsigned char* src, unsigned long src_len,
                                     unsigned char* dst, unsigned long dst_max) {
    unsigned long si = 0, di = 0;

    while (si < src_len && di < dst_max) {
        if (src[si] == RLE_ESCAPE) {
            si++;
            if (si >= src_len) break;
            unsigned long count = src[si++];
            if (si >= src_len) break;
            unsigned char val = src[si++];
            for (unsigned long c = 0; c < count && di < dst_max; c++) {
                dst[di++] = val;
            }
        } else {
            dst[di++] = src[si++];
        }
    }

    return di;
}

// --- Progress bar ---
static void draw_progress(const char* label, int percent, int row) {
    neo::display::set_cursor(2, row);
    neo::display::printf("%-20s [", label);

    int bar_w = 30;
    int filled = (percent * bar_w) / 100;
    neo::display::set_fg(10);
    for (int i = 0; i < bar_w; i++) {
        neo::display::putchar(i < filled ? '#' : '.');
    }
    neo::display::set_fg(7);
    neo::display::printf("] %3d%%", percent);
    neo::display::clear_eol();
}

// --- UI ---
static void draw_header() {
    neo::display::clear();
    neo::display::set_color(14, 1);
    int w = neo::display::get_width();
    neo::display::set_cursor(0, 0);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(2, 0);
    neo::display::printf("NeoZip v1.0 - Archive Manager");
    neo::display::set_color(7, 0);
    neo::display::set_cursor(0, 1);
    for (int i = 0; i < w; i++) neo::display::putchar('-');
}

static void wait_key() {
    neo::display::printf("\n  Press any key...");
    while (!neo::keyboard::key_available()) neo::proc::yield();
    neo::keyboard::read_scancode();
}

static char read_key() {
    while (!neo::keyboard::key_available()) neo::proc::yield();
    unsigned char sc = neo::keyboard::read_scancode();
    return neo::keyboard::translate(sc, neo::keyboard::is_shift_down());
}

// --- List archive contents ---
static void list_archive(const char* path) {
    draw_header();
    neo::display::set_cursor(0, 3);

    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, path, neo::filesystem::MODE_READ) != 0) {
        neo::display::set_fg(12);
        neo::display::printf("  Error: Cannot open '%s'\n", path);
        neo::display::set_fg(7);
        wait_key();
        return;
    }

    // Read header
    unsigned char header[8];
    neo::filesystem::read(fh, header, 8);

    unsigned long magic = ((unsigned long)header[0] << 24) |
                          ((unsigned long)header[1] << 16) |
                          ((unsigned long)header[2] << 8) |
                          header[3];

    if (magic != NZA_MAGIC) {
        neo::display::set_fg(12);
        neo::display::printf("  Error: '%s' is not a valid NeoZip archive.\n", path);
        neo::display::set_fg(7);
        neo::filesystem::close(fh);
        wait_key();
        return;
    }

    entry_count = ((int)header[4] << 24) | ((int)header[5] << 16) |
                  ((int)header[6] << 8) | header[7];

    if (entry_count > MAX_ENTRIES) entry_count = MAX_ENTRIES;

    // Read directory
    for (int i = 0; i < entry_count; i++) {
        unsigned char ebuf[269];  // INODE_SIZE + 4 + 4 + 4 + 1
        neo::filesystem::read(fh, ebuf, 269);
        neo_memcpy(entries[i].name, ebuf, INODE_SIZE);
        entries[i].original_size = ((unsigned long)ebuf[INODE_SIZE] << 24) |
                                   ((unsigned long)ebuf[257] << 16) |
                                   ((unsigned long)ebuf[258] << 8) | ebuf[259];
        entries[i].compressed_size = ((unsigned long)ebuf[260] << 24) |
                                     ((unsigned long)ebuf[261] << 16) |
                                     ((unsigned long)ebuf[262] << 8) | ebuf[263];
        entries[i].offset = ((unsigned long)ebuf[264] << 24) |
                            ((unsigned long)ebuf[265] << 16) |
                            ((unsigned long)ebuf[266] << 8) | ebuf[267];
        entries[i].method = ebuf[268];
    }

    neo::filesystem::close(fh);

    // Display
    neo::display::set_color(15, 0);
    neo::display::printf("  Archive: %s  (%d entries)\n\n", path, entry_count);
    neo::display::set_color(7, 0);

    neo::display::printf("  +-----+----------------------------------+----------+----------+-------+------+\n");
    neo::display::printf("  |  #  | Name                             | Original | Compress | Ratio | Meth |\n");
    neo::display::printf("  +-----+----------------------------------+----------+----------+-------+------+\n");

    unsigned long total_orig = 0, total_comp = 0;

    for (int i = 0; i < entry_count; i++) {
        ArchiveEntry& e = entries[i];
        int ratio = e.original_size > 0 ?
            (int)((e.compressed_size * 100) / e.original_size) : 100;
        const char* method = e.method == METHOD_RLE ? "RLE" : "STORE";

        neo::display::printf("  | %3d | %-32s | %8lu | %8lu | %3d%% | %-4s |\n",
            i + 1, e.name, e.original_size, e.compressed_size, ratio, method);

        total_orig += e.original_size;
        total_comp += e.compressed_size;
    }

    neo::display::printf("  +-----+----------------------------------+----------+----------+-------+------+\n");

    int total_ratio = total_orig > 0 ? (int)((total_comp * 100) / total_orig) : 100;
    neo::display::printf("  Total: %lu -> %lu bytes (%d%% ratio)\n",
        total_orig, total_comp, total_ratio);

    wait_key();
}

// --- Extract archive ---
static void extract_archive(const char* path, const char* dest_dir) {
    draw_header();
    neo::display::set_cursor(0, 3);

    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, path, neo::filesystem::MODE_READ) != 0) {
        neo::display::set_fg(12);
        neo::display::printf("  Error: Cannot open '%s'\n", path);
        neo::display::set_fg(7);
        wait_key();
        return;
    }

    // Read header
    unsigned char header[8];
    neo::filesystem::read(fh, header, 8);

    unsigned long magic = ((unsigned long)header[0] << 24) |
                          ((unsigned long)header[1] << 16) |
                          ((unsigned long)header[2] << 8) |
                          header[3];

    if (magic != NZA_MAGIC) {
        neo::display::set_fg(12);
        neo::display::printf("  Not a valid NeoZip archive.\n");
        neo::display::set_fg(7);
        neo::filesystem::close(fh);
        wait_key();
        return;
    }

    int count = ((int)header[4] << 24) | ((int)header[5] << 16) |
                ((int)header[6] << 8) | header[7];
    if (count > MAX_ENTRIES) count = MAX_ENTRIES;

    // Read directory entries
    for (int i = 0; i < count; i++) {
        unsigned char ebuf[269];
        neo::filesystem::read(fh, ebuf, 269);
        neo_memcpy(entries[i].name, ebuf, INODE_SIZE);
        entries[i].original_size = ((unsigned long)ebuf[INODE_SIZE] << 24) |
                                   ((unsigned long)ebuf[257] << 16) |
                                   ((unsigned long)ebuf[258] << 8) | ebuf[259];
        entries[i].compressed_size = ((unsigned long)ebuf[260] << 24) |
                                     ((unsigned long)ebuf[261] << 16) |
                                     ((unsigned long)ebuf[262] << 8) | ebuf[263];
        entries[i].offset = ((unsigned long)ebuf[264] << 24) |
                            ((unsigned long)ebuf[265] << 16) |
                            ((unsigned long)ebuf[266] << 8) | ebuf[267];
        entries[i].method = ebuf[268];
    }

    neo::display::set_color(15, 0);
    neo::display::printf("  Extracting %d files to %s\n\n", count, dest_dir);
    neo::display::set_color(7, 0);

    unsigned long buf_size = 65536;
    unsigned char* comp_buf = (unsigned char*)neo::mem::alloc(buf_size);
    unsigned char* decomp_buf = (unsigned char*)neo::mem::alloc(buf_size);

    if (!comp_buf || !decomp_buf) {
        if (comp_buf) neo::mem::free(comp_buf);
        if (decomp_buf) neo::mem::free(decomp_buf);
        neo::display::set_fg(12);
        neo::display::printf("  Error: Out of memory.\n");
        neo::display::set_fg(7);
        neo::filesystem::close(fh);
        wait_key();
        return;
    }

    for (int i = 0; i < count; i++) {
        ArchiveEntry& e = entries[i];
        int pct = ((i + 1) * 100) / count;
        draw_progress(e.name, pct, 6);

        neo::display::set_cursor(2, 7);
        neo::display::printf("  Extracting: %s (%lu bytes)", e.name, e.original_size);
        neo::display::clear_eol();

        // Build output path
        char out_path[MAX_PATH_LEN];
        int dlen = neo_strlen(dest_dir);
        neo_strcpy(out_path, dest_dir);
        if (dlen > 0 && dest_dir[dlen-1] != '/') {
            out_path[dlen] = '/';
            out_path[dlen+1] = 0;
        }
        neo_strcat(out_path, e.name);

        // Read compressed data
        if (e.compressed_size <= buf_size) {
            neo::filesystem::read(fh, comp_buf, e.compressed_size);

            // Decompress
            unsigned long out_len;
            if (e.method == METHOD_RLE) {
                out_len = rle_decompress(comp_buf, e.compressed_size, decomp_buf, buf_size);
            } else {
                neo_memcpy(decomp_buf, comp_buf, e.compressed_size);
                out_len = e.compressed_size;
            }

            // Write output file
            neo::filesystem::FileHandle ofh;
            if (neo::filesystem::open(ofh, out_path, neo::filesystem::MODE_WRITE | neo::filesystem::MODE_CREATE) == 0) {
                neo::filesystem::write(ofh, decomp_buf, out_len);
                neo::filesystem::close(ofh);
            }
        }

        neo::timer::delay_ms(50);  // Visual feedback
    }

    neo::mem::free(comp_buf);
    neo::mem::free(decomp_buf);
    neo::filesystem::close(fh);

    draw_progress("Complete!", 100, 6);
    neo::display::set_cursor(2, 9);
    neo::display::set_fg(10);
    neo::display::printf("  Extraction complete! %d files extracted.\n", count);
    neo::display::set_fg(7);
    wait_key();
}

// --- Create archive ---
static void create_archive(const char* output_path, const char* source_dir) {
    draw_header();
    neo::display::set_cursor(0, 3);

    // Scan source directory
    neo::filesystem::DirEntry dir_entries[64];
    int file_count = neo::filesystem::readdir(source_dir, dir_entries, 64);

    if (file_count <= 0) {
        neo::display::set_fg(12);
        neo::display::printf("  No files found in '%s'\n", source_dir);
        neo::display::set_fg(7);
        wait_key();
        return;
    }

    // Filter to files only
    int actual_count = 0;
    for (int i = 0; i < file_count; i++) {
        if (dir_entries[i].type == 0) {  // File
            if (actual_count != i) {
                neo_memcpy(&dir_entries[actual_count], &dir_entries[i], sizeof(neo::filesystem::DirEntry));
            }
            actual_count++;
        }
    }

    neo::display::set_color(15, 0);
    neo::display::printf("  Creating archive: %s\n", output_path);
    neo::display::printf("  Source: %s (%d files)\n\n", source_dir, actual_count);
    neo::display::set_color(7, 0);

    // Allocate buffers
    unsigned long buf_size = 65536;
    unsigned char* file_buf = (unsigned char*)neo::mem::alloc(buf_size);
    unsigned char* comp_buf = (unsigned char*)neo::mem::alloc(buf_size);

    if (!file_buf || !comp_buf) {
        if (file_buf) neo::mem::free(file_buf);
        if (comp_buf) neo::mem::free(comp_buf);
        neo::display::set_fg(12);
        neo::display::printf("  Error: Out of memory.\n");
        neo::display::set_fg(7);
        wait_key();
        return;
    }

    // Open output archive
    neo::filesystem::FileHandle afh;
    if (neo::filesystem::open(afh, output_path, neo::filesystem::MODE_WRITE | neo::filesystem::MODE_CREATE) != 0) {
        neo::mem::free(file_buf);
        neo::mem::free(comp_buf);
        neo::display::set_fg(12);
        neo::display::printf("  Error: Cannot create '%s'\n", output_path);
        neo::display::set_fg(7);
        wait_key();
        return;
    }

    // Write header (placeholder)
    unsigned char header[8];
    header[0] = (NZA_MAGIC >> 24) & 0xFF;
    header[1] = (NZA_MAGIC >> 16) & 0xFF;
    header[2] = (NZA_MAGIC >> 8) & 0xFF;
    header[3] = NZA_MAGIC & 0xFF;
    header[4] = (actual_count >> 24) & 0xFF;
    header[5] = (actual_count >> 16) & 0xFF;
    header[6] = (actual_count >> 8) & 0xFF;
    header[7] = actual_count & 0xFF;
    neo::filesystem::write(afh, header, 8);

    // First pass: compress files and record sizes
    unsigned long data_offset = 8 + (unsigned long)actual_count * 269;  // After header + directory

    for (int i = 0; i < actual_count; i++) {
        int pct = ((i + 1) * 100) / actual_count;
        draw_progress(dir_entries[i].name, pct, 7);

        neo_strncpy(entries[i].name, dir_entries[i].name, 255);
        entries[i].name[255] = 0;
        entries[i].original_size = dir_entries[i].size;
        entries[i].offset = data_offset;

        // Read source file
        char src_path[MAX_PATH_LEN];
        neo_strcpy(src_path, source_dir);
        int slen = neo_strlen(src_path);
        if (slen > 0 && src_path[slen-1] != '/') {
            src_path[slen] = '/';
            src_path[slen+1] = 0;
        }
        neo_strcat(src_path, dir_entries[i].name);

        neo::filesystem::FileHandle sfh;
        if (neo::filesystem::open(sfh, src_path, neo::filesystem::MODE_READ) == 0) {
            unsigned long read_sz = dir_entries[i].size;
            if (read_sz > buf_size) read_sz = buf_size;
            neo::filesystem::read(sfh, file_buf, read_sz);
            neo::filesystem::close(sfh);

            // Compress with RLE
            unsigned long comp_sz = rle_compress(file_buf, read_sz, comp_buf, buf_size);

            // Use RLE if smaller, otherwise store
            if (comp_sz < read_sz) {
                entries[i].compressed_size = comp_sz;
                entries[i].method = METHOD_RLE;
            } else {
                entries[i].compressed_size = read_sz;
                entries[i].method = METHOD_STORE;
                neo_memcpy(comp_buf, file_buf, read_sz);
            }
        } else {
            entries[i].compressed_size = 0;
            entries[i].method = METHOD_STORE;
        }

        data_offset += entries[i].compressed_size;
        neo::timer::delay_ms(30);
    }

    // Write directory entries
    for (int i = 0; i < actual_count; i++) {
        unsigned char ebuf[269];
        neo_memset(ebuf, 0, 269);
        neo_memcpy(ebuf, entries[i].name, INODE_SIZE);
        ebuf[INODE_SIZE] = (entries[i].original_size >> 24) & 0xFF;
        ebuf[257] = (entries[i].original_size >> 16) & 0xFF;
        ebuf[258] = (entries[i].original_size >> 8) & 0xFF;
        ebuf[259] = entries[i].original_size & 0xFF;
        ebuf[260] = (entries[i].compressed_size >> 24) & 0xFF;
        ebuf[261] = (entries[i].compressed_size >> 16) & 0xFF;
        ebuf[262] = (entries[i].compressed_size >> 8) & 0xFF;
        ebuf[263] = entries[i].compressed_size & 0xFF;
        ebuf[264] = (entries[i].offset >> 24) & 0xFF;
        ebuf[265] = (entries[i].offset >> 16) & 0xFF;
        ebuf[266] = (entries[i].offset >> 8) & 0xFF;
        ebuf[267] = entries[i].offset & 0xFF;
        ebuf[268] = entries[i].method;
        neo::filesystem::write(afh, ebuf, 269);
    }

    // Write compressed data
    for (int i = 0; i < actual_count; i++) {
        char src_path[MAX_PATH_LEN];
        neo_strcpy(src_path, source_dir);
        int slen = neo_strlen(src_path);
        if (slen > 0 && src_path[slen-1] != '/') {
            src_path[slen] = '/';
            src_path[slen+1] = 0;
        }
        neo_strcat(src_path, dir_entries[i].name);

        neo::filesystem::FileHandle sfh;
        if (neo::filesystem::open(sfh, src_path, neo::filesystem::MODE_READ) == 0) {
            unsigned long read_sz = entries[i].original_size;
            if (read_sz > buf_size) read_sz = buf_size;
            neo::filesystem::read(sfh, file_buf, read_sz);
            neo::filesystem::close(sfh);

            if (entries[i].method == METHOD_RLE) {
                unsigned long comp_sz = rle_compress(file_buf, read_sz, comp_buf, buf_size);
                neo::filesystem::write(afh, comp_buf, comp_sz);
            } else {
                neo::filesystem::write(afh, file_buf, read_sz);
            }
        }
    }

    neo::filesystem::close(afh);
    neo::mem::free(file_buf);
    neo::mem::free(comp_buf);

    draw_progress("Complete!", 100, 7);
    neo::display::set_cursor(2, 9);
    neo::display::set_fg(10);
    neo::display::printf("  Archive created: %s (%d files)\n", output_path, actual_count);
    neo::display::set_fg(7);
    wait_key();
}

// --- Interactive menu ---
static void main_menu() {
    char buf[MAX_PATH_LEN];

    while (true) {
        draw_header();
        neo::display::set_cursor(2, 3);
        neo::display::set_color(15, 0);
        neo::display::printf("Main Menu\n\n");
        neo::display::set_color(7, 0);

        neo::display::printf("  [1] List archive contents\n");
        neo::display::printf("  [2] Extract archive\n");
        neo::display::printf("  [3] Create archive (RLE compressed)\n");
        neo::display::printf("  [4] Compression test\n");
        neo::display::printf("  [Q] Quit\n\n");

        char ch = read_key();
        switch (ch) {
            case '1': {
                draw_header();
                neo::display::set_cursor(2, 3);
                neo::display::printf("Archive path: ");
                neo::console::getline(buf, sizeof(buf), nullptr);
                list_archive(buf);
                break;
            }
            case '2': {
                draw_header();
                neo::display::set_cursor(2, 3);
                neo::display::printf("Archive path: ");
                neo::console::getline(buf, sizeof(buf), nullptr);
                char dest[MAX_PATH_LEN];
                neo::display::printf("  Extract to: ");
                neo::console::getline(dest, sizeof(dest), nullptr);
                if (dest[0] == 0) neo_strcpy(dest, "/");
                extract_archive(buf, dest);
                break;
            }
            case '3': {
                draw_header();
                neo::display::set_cursor(2, 3);
                char src_dir[MAX_PATH_LEN], out[MAX_PATH_LEN];
                neo::display::printf("Source directory: ");
                neo::console::getline(src_dir, sizeof(src_dir), nullptr);
                neo::display::printf("  Output archive: ");
                neo::console::getline(out, sizeof(out), nullptr);
                create_archive(out, src_dir);
                break;
            }
            case '4': {
                // Compression test
                draw_header();
                neo::display::set_cursor(2, 3);
                neo::display::set_color(15, 0);
                neo::display::printf("RLE Compression Test\n\n");
                neo::display::set_color(7, 0);

                unsigned long test_size = 4096;
                unsigned char* test_data = (unsigned char*)neo::mem::alloc(test_size);
                unsigned char* comp_data = (unsigned char*)neo::mem::alloc(test_size * 2);
                unsigned char* decomp_data = (unsigned char*)neo::mem::alloc(test_size);

                if (test_data && comp_data && decomp_data) {
                    // Fill with test patterns
                    for (unsigned long i = 0; i < test_size; i++) {
                        // Mix of runs and random-ish data
                        if (i < 1024) test_data[i] = 'A';  // Long run
                        else if (i < 2048) test_data[i] = (unsigned char)(i & 0xFF);
                        else if (i < 3072) test_data[i] = (i & 1) ? 'X' : 'Y';
                        else test_data[i] = 0;
                    }

                    unsigned long comp_sz = rle_compress(test_data, test_size, comp_data, test_size * 2);
                    unsigned long decomp_sz = rle_decompress(comp_data, comp_sz, decomp_data, test_size);

                    int ratio = (int)((comp_sz * 100) / test_size);
                    bool match = (decomp_sz == test_size);
                    if (match) {
                        for (unsigned long i = 0; i < test_size; i++) {
                            if (test_data[i] != decomp_data[i]) { match = false; break; }
                        }
                    }

                    neo::display::printf("  Original size:     %lu bytes\n", test_size);
                    neo::display::printf("  Compressed size:   %lu bytes\n", comp_sz);
                    neo::display::printf("  Compression ratio: %d%%\n", ratio);
                    neo::display::printf("  Savings:           %lu bytes\n", test_size - comp_sz);
                    neo::display::printf("  Decompress verify: %s\n",
                        match ? "PASS" : "FAIL");

                    // Visual bar
                    neo::display::printf("\n  [");
                    int bar = 40;
                    int fill = (ratio * bar) / 100;
                    neo::display::set_fg(10);
                    for (int i = 0; i < bar; i++)
                        neo::display::putchar(i < fill ? '#' : '.');
                    neo::display::set_fg(7);
                    neo::display::printf("] %d%%\n", ratio);
                }

                if (test_data) neo::mem::free(test_data);
                if (comp_data) neo::mem::free(comp_data);
                if (decomp_data) neo::mem::free(decomp_data);
                wait_key();
                break;
            }
            case 'q': case 'Q': return;
        }
    }
}

}  // namespace neozip

extern "C" void app_main(int argc, char** argv) {
    if (argc > 1) {
        if (neo_strcmp(argv[1], "list") == 0 || neo_strcmp(argv[1], "l") == 0) {
            if (argc > 2) { neozip::list_archive(argv[2]); return; }
        }
        if (neo_strcmp(argv[1], "extract") == 0 || neo_strcmp(argv[1], "x") == 0) {
            if (argc > 2) {
                const char* dest = argc > 3 ? argv[3] : "/";
                neozip::extract_archive(argv[2], dest);
                return;
            }
        }
        if (neo_strcmp(argv[1], "create") == 0 || neo_strcmp(argv[1], "c") == 0) {
            if (argc > 3) {
                neozip::create_archive(argv[2], argv[3]);
                return;
            }
        }
    }

    neozip::main_menu();
}
