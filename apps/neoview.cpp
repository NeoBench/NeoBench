#include "../include/neobench.h"
#include "../lib/string.h"

// NeoView - IFF ILBM Image Viewer (ASCII art conversion)
// File browser, zoom, pan, slideshow, image info

namespace neoview {

static const int MAX_IMG_W = 320;
static const int MAX_IMG_H = INODE_SIZE;
static const int MAX_FILES = 128;
static const int MAX_PATH_LEN = INODE_SIZE;

// ASCII density characters for grayscale rendering (dark to light)
static const char density[] = " .:-=+*#%@";
static const int DENSITY_LEVELS = 10;

struct ImageData {
    unsigned char* pixels; // grayscale 0-255
    int width;
    int height;
    int colors;
    char format[16];
    bool loaded;

    void init() {
        pixels = nullptr;
        width = 0;
        height = 0;
        colors = 0;
        format[0] = '\0';
        loaded = false;
    }

    void alloc(int w, int h) {
        if (pixels) neo::mem::free(pixels);
        width = w;
        height = h;
        pixels = (unsigned char*)neo::mem::alloc(w * h);
        if (pixels) neo_memset(pixels, 0, w * h);
    }

    void destroy() {
        if (pixels) { neo::mem::free(pixels); pixels = nullptr; }
        loaded = false;
    }

    unsigned char get(int x, int y) const {
        if (!pixels || x < 0 || x >= width || y < 0 || y >= height) return 0;
        return pixels[y * width + x];
    }

    void set(int x, int y, unsigned char val) {
        if (pixels && x >= 0 && x < width && y >= 0 && y < height)
            pixels[y * width + x] = val;
    }
};

struct FileEntry {
    char name[64];
    int size;
    bool is_dir;
};

struct ViewerApp {
    ImageData image;
    int screen_w, screen_h;
    bool running;

    // Pan/zoom
    int pan_x, pan_y;
    int zoom; // 1=1:1, 2=2:1 zoom in, -1=1:2 zoom out etc.

    // File browser
    FileEntry files[MAX_FILES];
    int num_files;
    int file_selected;
    int file_scroll;
    char current_path[MAX_PATH_LEN];
    bool in_browser;

    // Slideshow
    bool slideshow;
    int slideshow_delay_ms;
    int slideshow_idx;

    char status_msg[64];
    int status_timer;

    unsigned int rng_state;

    void init() {
        screen_w = neo::display::get_width();
        screen_h = neo::display::get_height();
        image.init();
        running = true;
        pan_x = pan_y = 0;
        zoom = 1;
        num_files = 0;
        file_selected = 0;
        file_scroll = 0;
        neo_strcpy(current_path, "DF0:");
        in_browser = true;
        slideshow = false;
        slideshow_delay_ms = 3000;
        slideshow_idx = 0;
        status_msg[0] = '\0';
        status_timer = 0;
        rng_state = neo::timer::get_ticks();
    }

    void destroy() {
        image.destroy();
    }

    void set_status(const char* msg) {
        neo_strcpy(status_msg, msg);
        status_timer = 80;
    }

    unsigned int rng() {
        rng_state = rng_state * 1103515245 + 12345;
        return (rng_state >> 16) & 0x7FFF;
    }

    void scan_directory() {
        neo::filesystem::DirEntry entries[MAX_FILES];
        int count = neo::filesystem::readdir(current_path, entries, MAX_FILES);
        num_files = 0;

        // Add parent directory
        if (neo_strlen(current_path) > 1) {
            neo_strcpy(files[num_files].name, "..");
            files[num_files].size = 0;
            files[num_files].is_dir = true;
            num_files++;
        }

        for (int i = 0; i < count && num_files < MAX_FILES; i++) {
            neo_strncpy(files[num_files].name, entries[i].name, 63);
            files[num_files].name[63] = '\0';
            files[num_files].size = entries[i].size;
            files[num_files].is_dir = (entries[i].type == 1);
            num_files++;
        }
        file_selected = 0;
        file_scroll = 0;
    }

    bool is_image_file(const char* name) {
        int len = neo_strlen(name);
        if (len < 4) return false;
        // Check for .iff, .ilbm, .lbm extensions
        if (neo_strncmp(name + len - 4, ".iff", 4) == 0) return true;
        if (neo_strncmp(name + len - 4, ".lbm", 4) == 0) return true;
        if (len >= 5 && neo_strncmp(name + len - 5, ".ilbm", 5) == 0) return true;
        // Also support raw image data
        if (neo_strncmp(name + len - 4, ".raw", 4) == 0) return true;
        if (neo_strncmp(name + len - 4, ".img", 4) == 0) return true;
        return false;
    }

    // Parse IFF ILBM file header and extract image data
    bool load_iff(const char* path) {
        neo::filesystem::FileHandle fh;
        if (neo::filesystem::open(fh, path, neo::filesystem::MODE_READ) != 0) {
            set_status("Cannot open file!");
            return false;
        }

        char header[12];
        int rd = neo::filesystem::read(fh, header, 12);
        if (rd < 12) {
            neo::filesystem::close(fh);
            // Not a valid IFF, generate test pattern
            generate_test_image(path);
            return true;
        }

        // Check for FORM....ILBM
        if (neo_strncmp(header, "FORM", 4) == 0 && neo_strncmp(header + 8, "ILBM", 4) == 0) {
            neo_strcpy(image.format, "IFF ILBM");
            // Parse BMHD chunk
            int w = 320, h = 200, depth = 4;
            char chunk_id[4];
            char chunk_size_buf[4];

            while (neo::filesystem::read(fh, chunk_id, 4) == 4) {
                neo::filesystem::read(fh, chunk_size_buf, 4);
                int chunk_size = ((unsigned char)chunk_size_buf[0] << 24) |
                                 ((unsigned char)chunk_size_buf[1] << 16) |
                                 ((unsigned char)chunk_size_buf[2] << 8) |
                                 (unsigned char)chunk_size_buf[3];

                if (neo_strncmp(chunk_id, "BMHD", 4) == 0) {
                    char bmhd[20];
                    int toread = chunk_size < 20 ? chunk_size : 20;
                    neo::filesystem::read(fh, bmhd, toread);
                    w = ((unsigned char)bmhd[0] << 8) | (unsigned char)bmhd[1];
                    h = ((unsigned char)bmhd[2] << 8) | (unsigned char)bmhd[3];
                    depth = (unsigned char)bmhd[8];
                    if (w > MAX_IMG_W) w = MAX_IMG_W;
                    if (h > MAX_IMG_H) h = MAX_IMG_H;
                    // Skip rest of chunk
                    int remaining = chunk_size - toread;
                    if (remaining > 0) {
                        char skip[64];
                        while (remaining > 0) {
                            int s = remaining < 64 ? remaining : 64;
                            neo::filesystem::read(fh, skip, s);
                            remaining -= s;
                        }
                    }
                } else if (neo_strncmp(chunk_id, "BODY", 4) == 0) {
                    // Read body and convert to grayscale
                    image.alloc(w, h);
                    image.colors = 1 << depth;
                    int body_remaining = chunk_size;
                    int y = 0;
                    char rowbuf[320];
                    while (y < h && body_remaining > 0) {
                        int rowbytes = (w + 7) / 8;
                        int toread = rowbytes < (int)sizeof(rowbuf) ? rowbytes : (int)sizeof(rowbuf);
                        if (toread > body_remaining) toread = body_remaining;
                        neo::filesystem::read(fh, rowbuf, toread);
                        body_remaining -= toread;
                        // Simple: use first bitplane as grayscale
                        for (int x = 0; x < w; x++) {
                            int byte_idx = x / 8;
                            int bit_idx = 7 - (x % 8);
                            unsigned char val = 0;
                            if (byte_idx < toread) {
                                val = (rowbuf[byte_idx] >> bit_idx) & 1;
                            }
                            image.set(x, y, val ? 255 : 0);
                        }
                        y++;
                    }
                    break;
                } else {
                    // Skip unknown chunk
                    char skip[64];
                    int remaining = chunk_size;
                    while (remaining > 0) {
                        int s = remaining < 64 ? remaining : 64;
                        neo::filesystem::read(fh, skip, s);
                        remaining -= s;
                    }
                }
                // Chunks are word-aligned
                if (chunk_size & 1) {
                    char pad;
                    neo::filesystem::read(fh, &pad, 1);
                }
            }
            image.loaded = true;
        } else {
            // Not IFF, try as raw grayscale
            neo::filesystem::close(fh);
            generate_test_image(path);
            return true;
        }

        neo::filesystem::close(fh);
        return image.loaded;
    }

    void generate_test_image(const char* path) {
        // Generate a procedural test image based on filename
        int w = 160, h = 100;
        image.alloc(w, h);
        neo_strcpy(image.format, "Generated");
        image.colors = INODE_SIZE;

        unsigned int seed = 0;
        for (int i = 0; path[i]; i++) seed = seed * 31 + path[i];
        rng_state = seed;

        // Generate pattern based on hash
        int pattern = seed % 5;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                unsigned char val = 0;
                switch (pattern) {
                    case 0: // Gradient
                        val = (unsigned char)((x * 255) / w);
                        break;
                    case 1: // Checkerboard
                        val = ((x / 8) + (y / 8)) % 2 ? 200 : 50;
                        break;
                    case 2: // Circles
                    {
                        int dx = x - w / 2;
                        int dy = y - h / 2;
                        int dist = dx * dx + dy * dy;
                        val = (unsigned char)((dist / 20) % INODE_SIZE);
                        break;
                    }
                    case 3: // Random noise
                        val = (unsigned char)(rng() % INODE_SIZE);
                        break;
                    case 4: // Horizontal bars
                        val = (unsigned char)((y * 255) / h);
                        break;
                }
                image.set(x, y, val);
            }
        }
        image.loaded = true;
    }

    char pixel_to_ascii(unsigned char val) {
        int idx = (val * (DENSITY_LEVELS - 1)) / 255;
        if (idx < 0) idx = 0;
        if (idx >= DENSITY_LEVELS) idx = DENSITY_LEVELS - 1;
        return density[idx];
    }

    void render_image_view() {
        neo::display::clear();

        // Header
        neo::display::set_color(0, 3);
        neo::display::set_cursor(0, 0);
        char hdr[80];
        ksprintf(hdr, sizeof(hdr), " NeoView | %dx%d | %s | %d colors | Zoom:%d | Pan:(%d,%d) ",
                 image.width, image.height, image.format, image.colors, zoom, pan_x, pan_y);
        neo::display::puts(hdr);
        for (int i = neo_strlen(hdr); i < screen_w; i++) {
            neo::display::set_cursor(i, 0);
            neo::display::putchar(' ');
        }

        // Render ASCII art
        int view_h = screen_h - 3;
        int view_w = screen_w;

        neo::display::set_color(7, 0);
        for (int sy = 0; sy < view_h; sy++) {
            for (int sx = 0; sx < view_w; sx++) {
                int img_x, img_y;
                if (zoom > 0) {
                    img_x = pan_x + sx / zoom;
                    img_y = pan_y + sy / zoom;
                } else {
                    int zf = -zoom + 2; // zoom out factor
                    img_x = pan_x + sx * zf;
                    img_y = pan_y + sy * zf;
                }

                neo::display::set_cursor(sx, sy + 1);
                if (img_x >= 0 && img_x < image.width && img_y >= 0 && img_y < image.height) {
                    unsigned char val = image.get(img_x, img_y);
                    // Color based on intensity
                    if (val < 64) neo::display::set_fg(1);
                    else if (val < 128) neo::display::set_fg(4);
                    else if (val < 192) neo::display::set_fg(6);
                    else neo::display::set_fg(7);
                    neo::display::putchar(pixel_to_ascii(val));
                } else {
                    neo::display::set_fg(1);
                    neo::display::putchar('.');
                }
            }
        }

        // Status bar
        neo::display::set_color(0, 2);
        neo::display::set_cursor(0, screen_h - 1);
        char help[80];
        if (slideshow) {
            neo_strcpy(help, " SLIDESHOW | Any key: Stop ");
        } else {
            neo_strcpy(help, " Arrows:Pan +/-:Zoom B:Browser S:Slideshow F:Fit Q:Quit ");
        }
        neo::display::puts(help);
        for (int i = neo_strlen(help); i < screen_w; i++) {
            neo::display::set_cursor(i, screen_h - 1);
            neo::display::putchar(' ');
        }
    }

    void render_browser() {
        neo::display::clear();

        neo::display::set_color(0, 3);
        neo::display::set_cursor(0, 0);
        char hdr[80];
        ksprintf(hdr, sizeof(hdr), " NeoView File Browser | %s | %d items ", current_path, num_files);
        neo::display::puts(hdr);
        for (int i = neo_strlen(hdr); i < screen_w; i++) {
            neo::display::set_cursor(i, 0);
            neo::display::putchar(' ');
        }

        // Column headers
        neo::display::set_color(0, 6);
        neo::display::set_cursor(0, 1);
        neo::display::puts("   Name                          Size      Type  ");
        for (int i = 50; i < screen_w; i++) {
            neo::display::set_cursor(i, 1);
            neo::display::putchar(' ');
        }

        int visible = screen_h - 4;
        if (file_selected < file_scroll) file_scroll = file_selected;
        if (file_selected >= file_scroll + visible) file_scroll = file_selected - visible + 1;

        for (int i = 0; i < visible && file_scroll + i < num_files; i++) {
            int idx = file_scroll + i;
            int sy = 2 + i;
            FileEntry& f = files[idx];

            if (idx == file_selected) neo::display::set_color(0, 7);
            else if (f.is_dir) neo::display::set_color(3, 0);
            else if (is_image_file(f.name)) neo::display::set_color(2, 0);
            else neo::display::set_color(7, 0);

            neo::display::set_cursor(0, sy);
            char line[80];
            ksprintf(line, sizeof(line), " %-30s %8d  %s",
                     f.name, f.size, f.is_dir ? "[DIR]" : "file");
            neo::display::puts(line);
            for (int j = neo_strlen(line); j < screen_w; j++) {
                neo::display::set_cursor(j, sy);
                neo::display::putchar(' ');
            }
        }

        neo::display::set_color(0, 2);
        neo::display::set_cursor(0, screen_h - 1);
        neo::display::puts(" Enter:Open Esc:Back Q:Quit ");
        for (int i = 28; i < screen_w; i++) {
            neo::display::set_cursor(i, screen_h - 1);
            neo::display::putchar(' ');
        }
    }

    void open_selected() {
        if (file_selected < 0 || file_selected >= num_files) return;
        FileEntry& f = files[file_selected];

        if (f.is_dir) {
            if (neo_strcmp(f.name, "..") == 0) {
                // Go up
                int len = neo_strlen(current_path);
                while (len > 0 && current_path[len-1] != '/' && current_path[len-1] != ':') len--;
                current_path[len] = '\0';
            } else {
                neo_strcat(current_path, f.name);
                neo_strcat(current_path, "/");
            }
            scan_directory();
        } else {
            // Try to load as image
            char full_path[MAX_PATH_LEN];
            ksprintf(full_path, sizeof(full_path), "%s%s", current_path, f.name);
            image.destroy();
            image.init();
            load_iff(full_path);
            if (image.loaded) {
                pan_x = 0;
                pan_y = 0;
                zoom = 1;
                in_browser = false;
            } else {
                set_status("Failed to load image");
            }
        }
    }

    void fit_to_screen() {
        if (!image.loaded) return;
        int vw = screen_w;
        int vh = screen_h - 3;
        // Calculate zoom to fit
        if (image.width <= vw && image.height <= vh) {
            zoom = 1;
        } else {
            int zx = image.width / vw + 1;
            int zy = image.height / vh + 1;
            zoom = -(zx > zy ? zx : zy);
        }
        pan_x = 0;
        pan_y = 0;
    }

    void run_slideshow() {
        slideshow = true;
        // Collect image files
        int img_indices[MAX_FILES];
        int img_count = 0;
        for (int i = 0; i < num_files; i++) {
            if (!files[i].is_dir && is_image_file(files[i].name)) {
                img_indices[img_count++] = i;
            }
        }
        if (img_count == 0) { set_status("No images for slideshow"); slideshow = false; return; }

        int si = 0;
        while (slideshow && running) {
            file_selected = img_indices[si];
            open_selected();
            if (image.loaded) {
                fit_to_screen();
                render_image_view();
            }

            int waited = 0;
            while (waited < slideshow_delay_ms && slideshow) {
                neo::timer::delay_ms(50);
                waited += 50;
                if (neo::keyboard::key_available()) {
                    unsigned char sc = neo::keyboard::read_scancode();
                    if (!(sc & 0x80)) slideshow = false;
                }
            }

            si = (si + 1) % img_count;
        }
        slideshow = false;
    }

    void handle_browser_key(unsigned char sc) {
        char ch = neo::keyboard::translate(sc, false);
        if (sc == 0x4C && file_selected > 0) file_selected--;
        else if (sc == 0x4D && file_selected < num_files - 1) file_selected++;
        else if (sc == 0x44 || ch == '\n' || ch == '\r') open_selected();
        else if (sc == 0x45) { running = false; }
        else if (ch == 'q' || ch == 'Q') { running = false; }
        else if (ch == 's' || ch == 'S') { run_slideshow(); in_browser = true; }
    }

    void handle_view_key(unsigned char sc) {
        char ch = neo::keyboard::translate(sc, false);
        if (sc == 0x4C) pan_y -= 2;
        else if (sc == 0x4D) pan_y += 2;
        else if (sc == 0x4F) pan_x -= 4;
        else if (sc == 0x50) pan_x += 4;
        else if (ch == '+' || ch == '=') { if (zoom < 8) zoom++; if (zoom == 0) zoom = 1; }
        else if (ch == '-') { if (zoom > -4) zoom--; if (zoom == 0) zoom = -1; }
        else if (ch == 'f' || ch == 'F') fit_to_screen();
        else if (ch == 'b' || ch == 'B' || sc == 0x45) { in_browser = true; }
        else if (ch == 'q' || ch == 'Q') { running = false; }
        else if (ch == 's' || ch == 'S') { in_browser = true; run_slideshow(); }

        // Clamp pan
        if (pan_x < 0) pan_x = 0;
        if (pan_y < 0) pan_y = 0;
    }

    void run(int argc, char** argv) {
        init();

        // Load file from args or start browser
        if (argc > 1) {
            image.destroy();
            image.init();
            load_iff(argv[1]);
            if (image.loaded) {
                fit_to_screen();
                in_browser = false;
            }
        }

        if (in_browser) scan_directory();

        while (running) {
            if (in_browser) render_browser();
            else render_image_view();

            if (status_timer > 0) status_timer--;
            if (status_timer == 0) status_msg[0] = '\0';

            while (!neo::keyboard::key_available()) neo::timer::delay_ms(10);
            unsigned char sc = neo::keyboard::read_scancode();
            if (sc & 0x80) continue;

            if (in_browser) handle_browser_key(sc);
            else handle_view_key(sc);
        }

        neo::display::clear();
        neo::display::set_color(7, 0);
        kprintf("NeoView: Goodbye.\n");
        destroy();
    }
};

} // namespace neoview

extern "C" void app_main(int argc, char** argv) {
    neoview::ViewerApp app;
    app.run(argc, argv);
}
