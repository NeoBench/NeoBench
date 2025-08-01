/**
 * NeoBench Desktop Backgrounds
 *
 * This file contains the data and generation functions for the
 * six NeoBench desktop backgrounds with a Windows Vista-inspired look.
 */

#include <stdint.h>
#include <stdlib.h>
#include <math.h>

// Screen dimensions (AGA high-res)
#define SCREEN_WIDTH  800
#define SCREEN_HEIGHT 600
#define COLOR_DEPTH   24    // 24-bit color (AGA)

// Background types
#define BG_VISTA_AURORA       0
#define BG_NEOBENCH_TECH      1
#define BG_NEOBENCH_ABSTRACT  2
#define BG_NEOBENCH_DARK      3
#define BG_NEOBENCH_RETRO     4
#define BG_NEOBENCH_FUTURE    5
#define NUM_BACKGROUNDS       6

// Structure to hold a bitmap
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t *pixels;  // 32-bit RGBA format
} Bitmap;

// Bitmaps for backgrounds
static Bitmap backgrounds[NUM_BACKGROUNDS];

// Logo dimensions
#define LOGO_WIDTH   300
#define LOGO_HEIGHT  100

// Function prototypes
void generate_backgrounds(void);
void generate_vista_aurora(void);
void generate_neobench_tech(void);
void generate_neobench_abstract(void);
void generate_neobench_dark(void);
void generate_neobench_retro(void);
void generate_neobench_future(void);
void draw_neobench_logo(Bitmap *bitmap, int x, int y, uint32_t color);
void gradient_fill(Bitmap *bitmap, uint32_t top_color, uint32_t bottom_color);
uint32_t blend_colors(uint32_t c1, uint32_t c2, float t);
void add_blur(Bitmap *bitmap, int radius);

/**
 * Generate all background images
 */
void generate_backgrounds(void) {
    // Initialize bitmaps
    for (int i = 0; i < NUM_BACKGROUNDS; i++) {
        backgrounds[i].width = SCREEN_WIDTH;
        backgrounds[i].height = SCREEN_HEIGHT;
        backgrounds[i].pixels = (uint32_t*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
    }
    
    // Generate each background
    generate_vista_aurora();
    generate_neobench_tech();
    generate_neobench_abstract();
    generate_neobench_dark();
    generate_neobench_retro();
    generate_neobench_future();
}

/**
 * Generate the Vista Aurora background
 * This creates a blue gradient with light glow at bottom
 */
void generate_vista_aurora(void) {
    Bitmap *bitmap = &backgrounds[BG_VISTA_AURORA];
    
    // Create blue gradient from top to bottom
    gradient_fill(bitmap, 0x0066CC, 0x003366);
    
    // Add aurora effect at bottom (light glow)
    for (int y = bitmap->height - 200; y < bitmap->height; y++) {
        for (int x = 0; x < bitmap->width; x++) {
            float dist = (bitmap->height - y) / 200.0f;
            float intensity = sinf(x * 0.01f) * 0.5f + 0.5f;
            intensity *= (1.0f - dist);
            
            // Get current pixel color
            uint32_t cur_color = bitmap->pixels[y * bitmap->width + x];
            
            // Blend with white based on intensity
            uint32_t glow_color = blend_colors(cur_color, 0xFFFFFF, intensity * 0.3f);
            bitmap->pixels[y * bitmap->width + x] = glow_color;
        }
    }
    
    // Add slight blur to glow
    add_blur(bitmap, 3);
    
    // Add NeoBench logo centered at the bottom third of the screen
    draw_neobench_logo(bitmap, 
                      (bitmap->width - LOGO_WIDTH) / 2,
                      (bitmap->height * 2) / 3,
                      0xFFFFFF);
}

/**
 * Generate the NeoBench Tech background
 * This creates a circuit board pattern with NeoBench logo
 */
void generate_neobench_tech(void) {
    Bitmap *bitmap = &backgrounds[BG_NEOBENCH_TECH];
    
    // Fill with dark background
    for (int i = 0; i < bitmap->width * bitmap->height; i++) {
        bitmap->pixels[i] = 0x102030;
    }
    
    // Draw circuit board pattern
    uint32_t circuit_color = 0x00FF00; // Green circuits
    
    // Draw horizontal and vertical lines
    for (int i = 0; i < 30; i++) {
        int x = rand() % bitmap->width;
        int y = rand() % bitmap->height;
        int length = 50 + rand() % 300;
        int thickness = 1 + rand() % 2;
        
        if (rand() % 2) {
            // Horizontal line
            for (int j = 0; j < length; j++) {
                if (x + j < bitmap->width) {
                    for (int t = 0; t < thickness; t++) {
                        if (y + t < bitmap->height) {
                            bitmap->pixels[(y + t) * bitmap->width + (x + j)] = circuit_color;
                        }
                    }
                }
            }
        } else {
            // Vertical line
            for (int j = 0; j < length; j++) {
                if (y + j < bitmap->height) {
                    for (int t = 0; t < thickness; t++) {
                        if (x + t < bitmap->width) {
                            bitmap->pixels[(y + j) * bitmap->width + (x + t)] = circuit_color;
                        }
                    }
                }
            }
        }
    }
    
    // Draw connection nodes
    for (int i = 0; i < 100; i++) {
        int x = rand() % bitmap->width;
        int y = rand() % bitmap->height;
        int radius = 2 + rand() % 5;
        
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (x + dx >= 0 && x + dx < bitmap->width && 
                    y + dy >= 0 && y + dy < bitmap->height) {
                    if (dx*dx + dy*dy <= radius*radius) {
                        bitmap->pixels[(y + dy) * bitmap->width + (x + dx)] = circuit_color;
                    }
                }
            }
        }
    }
    
    // Add some glow to the circuits
    add_blur(bitmap, 2);
    
    // Add NeoBench logo
    draw_neobench_logo(bitmap, 
                      (bitmap->width - LOGO_WIDTH) / 2,
                      (bitmap->height - LOGO_HEIGHT) / 2,
                      0xFFFFFF);
}

/**
 * Generate the NeoBench Abstract background
 * This creates a modern abstract design with NeoBench color scheme
 */
void generate_neobench_abstract(void) {
    Bitmap *bitmap = &backgrounds[BG_NEOBENCH_ABSTRACT];
    
    // Fill with gradient background
    gradient_fill(bitmap, 0x2C3E50, 0x4CA1AF);
    
    // Draw abstract shapes
    for (int i = 0; i < 15; i++) {
        // Draw translucent circles
        int x = rand() % bitmap->width;
        int y = rand() % bitmap->height;
        int radius = 50 + rand() % 200;
        uint32_t color = rand() % 2 ? 0x3498DB : 0x1ABC9C; // Blue or teal
        float alpha = 0.2f + (rand() % 6) / 10.0f; // 0.2 to 0.7
        
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (x + dx >= 0 && x + dx < bitmap->width && 
                    y + dy >= 0 && y + dy < bitmap->height) {
                    float dist = sqrtf(dx*dx + dy*dy) / radius;
                    if (dist <= 1.0f) {
                        // Apply radial gradient
                        float fade = 1.0f - dist;
                        uint32_t cur_color = bitmap->pixels[(y + dy) * bitmap->width + (x + dx)];
                        bitmap->pixels[(y + dy) * bitmap->width + (x + dx)] = 
                            blend_colors(cur_color, color, fade * alpha);
                    }
                }
            }
        }
    }
    
    // Add NeoBench logo
    draw_neobench_logo(bitmap, 
                      (bitmap->width - LOGO_WIDTH) / 2,
                      (bitmap->height - LOGO_HEIGHT) / 2,
                      0xFFFFFF);
}

/**
 * Generate the NeoBench Dark background
 * This creates a dark theme with subtle NeoBench accents
 */
void generate_neobench_dark(void) {
    Bitmap *bitmap = &backgrounds[BG_NEOBENCH_DARK];
    
    // Fill with dark gradient
    gradient_fill(bitmap, 0x111111, 0x222222);
    
    // Add subtle pattern
    for (int y = 0; y < bitmap->height; y++) {
        for (int x = 0; x < bitmap->width; x++) {
            // Create a subtle noise pattern
            float noise = (rand() % 10) / 100.0f;
            bitmap->pixels[y * bitmap->width + x] = 
                blend_colors(bitmap->pixels[y * bitmap->width + x], 0x333333, noise);
        }
    }
    
    // Add accent lines (blue with glow)
    uint32_t accent_color = 0x0078D7; // Windows blue
    
    // Horizontal accent line
    int line_y = bitmap->height / 4;
    int thickness = 2;
    
    for (int y = line_y - thickness; y <= line_y + thickness; y++) {
        if (y >= 0 && y < bitmap->height) {
            for (int x = 0; x < bitmap->width; x++) {
                float dist = fabsf(y - line_y) / (float)thickness;
                float intensity = 1.0f - dist;
                bitmap->pixels[y * bitmap->width + x] = 
                    blend_colors(bitmap->pixels[y * bitmap->width + x], accent_color, intensity * 0.7f);
            }
        }
    }
    
    // Vertical accent line
    int line_x = bitmap->width * 3 / 4;
    
    for (int x = line_x - thickness; x <= line_x + thickness; x++) {
        if (x >= 0 && x < bitmap->width) {
            for (int y = 0; y < bitmap->height; y++) {
                float dist = fabsf(x - line_x) / (float)thickness;
                float intensity = 1.0f - dist;
                bitmap->pixels[y * bitmap->width + x] = 
                    blend_colors(bitmap->pixels[y * bitmap->width + x], accent_color, intensity * 0.7f);
            }
        }
    }
    
    // Add glow to accent lines
    add_blur(bitmap, 5);
    
    // Add NeoBench logo with blue accent color
    draw_neobench_logo(bitmap, 
                      (bitmap->width - LOGO_WIDTH) / 2,
                      (bitmap->height - LOGO_HEIGHT) / 2,
                      accent_color);
}

/**
 * Generate the NeoBench Retro background
 * This creates a retro computing inspired background
 */
void generate_neobench_retro(void) {
    Bitmap *bitmap = &backgrounds[BG_NEOBENCH_RETRO];
    
    // Fill with gradient background (old CRT green)
    gradient_fill(bitmap, 0x003300, 0x002200);
    
    // Create scanlines for CRT effect
    for (int y = 0; y < bitmap->height; y += 2) {
        for (int x = 0; x < bitmap->width; x++) {
            bitmap->pixels[y * bitmap->width + x] = 
                blend_colors(bitmap->pixels[y * bitmap->width + x], 0x000000, 0.4f);
        }
    }
    
    // Add a grid pattern
    uint32_t grid_color = 0x00FF00; // Bright green
    for (int y = 0; y < bitmap->height; y += 20) {
        for (int x = 0; x < bitmap->width; x++) {
            bitmap->pixels[y * bitmap->width + x] = 
                blend_colors(bitmap->pixels[y * bitmap->width + x], grid_color, 0.2f);
        }
    }
    
    for (int x = 0; x < bitmap->width; x += 20) {
        for (int y = 0; y < bitmap->height; y++) {
            bitmap->pixels[y * bitmap->width + x] = 
                blend_colors(bitmap->pixels[y * bitmap->width + x], grid_color, 0.2f);
        }
    }
    
    // Add some "code" text (represented by green dots)
    for (int i = 0; i < 1000; i++) {
        int x = rand() % bitmap->width;
        int y = rand() % bitmap->height;
        
        if ((x / 20) % 3 != 0 || (y / 20) % 5 != 0) {  // Avoid grid intersections
            bitmap->pixels[y * bitmap->width + x] = 0x00FF00;
        }
    }
    
    // Add NeoBench logo with green retro color
    draw_neobench_logo(bitmap, 
                      (bitmap->width - LOGO_WIDTH) / 2,
                      (bitmap->height - LOGO_HEIGHT) / 2,
                      0x00FF00);
}

/**
 * Generate the NeoBench Future background
 * This creates a futuristic tech visualization
 */
void generate_neobench_future(void) {
    Bitmap *bitmap = &backgrounds[BG_NEOBENCH_FUTURE];
    
    // Fill with dark blue-black gradient
    gradient_fill(bitmap, 0x000030, 0x000015);
    
    // Add "tech grid" effect
    for (int y = 0; y < bitmap->height; y++) {
        for (int x = 0; x < bitmap->width; x++) {
            // Create perspective grid effect
            float center_x = x - bitmap->width / 2;
            float center_y = y - bitmap->height / 2;
            float distance = sqrtf(center_x * center_x + center_y * center_y);
            float angle = atan2f(center_y, center_x);
            
            // Grid pattern based on angle and distance
            if ((int)(distance / 20 + 10 * angle) % 40 < 2) {
                bitmap->pixels[y * bitmap->width + x] = 
                    blend_colors(bitmap->pixels[y * bitmap->width + x], 0x4488FF, 0.3f);
            }
        }
    }
    
    // Add random "data points" - small glowing dots
    for (int i = 0; i < 200; i++) {
        int x = rand() % bitmap->width;
        int y = rand() % bitmap->height;
        int radius = 1 + rand() % 3;
        uint32_t dot_color = rand() % 2 ? 0x00FFFF : 0x0088FF; // Cyan or blue
        
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (x + dx >= 0 && x + dx < bitmap->width && 
                    y + dy >= 0 && y + dy < bitmap->height) {
                    float dist = sqrtf(dx*dx + dy*dy) / radius;
                    if (dist <= 1.0f) {
                        float fade = 1.0f - dist;
                        bitmap->pixels[(y + dy) * bitmap->width + (x + dx)] = 
                            blend_colors(bitmap->pixels[(y + dy) * bitmap->width + (x + dx)], 
                                        dot_color, fade);
                    }
                }
            }
        }
    }
    
    // Add light streaks
    for (int i = 0; i < 10; i++) {
        int x1 = rand() % bitmap->width;
        int y1 = rand() % bitmap->height;
        int x2 = rand() % bitmap->width;
        int y2 = rand() % bitmap->height;
        uint32_t streak_color = 0x00FFFF; // Cyan
        
        // Draw a line between (x1,y1) and (x2,y2)
        int dx = abs(x2 - x1);
        int dy = abs(y2 - y1);
        int sx = x1 < x2 ? 1 : -1;
        int sy = y1 < y2 ? 1 : -1;
        int err = dx - dy;
        
        while (1) {
            // Draw glowing point
            for (int r = -2; r <= 2; r++) {
                for (int c = -2; c <= 2; c++) {
                    int px = x1 + c;
                    int py = y1 + r;
                    if (px >= 0 && px < bitmap->width && py >= 0 && py < bitmap->height) {
                        float dist = sqrtf(c*c + r*r) / 2.0f;
                        if (dist <= 1.0f) {
                            float fade = 1.0f - dist;
                            bitmap->pixels[py * bitmap->width + px] = 
                                blend_colors(bitmap->pixels[py * bitmap->width + px], 
                                            streak_color, fade * 0.5f);
                        }
                    }
                }
            }
            
            if (x1 == x2 && y1 == y2) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x1 += sx; }
            if (e2 < dx) { err += dx; y1 += sy; }
        }
    }
    
    // Add NeoBench logo with futuristic blue glow
    draw_neobench_logo(bitmap, 
                      (bitmap->width - LOGO_WIDTH) / 2,
                      (bitmap->height - LOGO_HEIGHT) / 2,
                      0x00FFFF);
    
    // Add extra glow to logo
    add_blur(bitmap, 3);
}

/**
 * Draw the NeoBench logo on a bitmap
 */
void draw_neobench_logo(Bitmap *bitmap, int x, int y, uint32_t color) {
    // This is a simplified version that would just draw "NEOBENCH" text
    // In a real implementation, this would draw a proper logo bitmap
    
    // For now, we'll just draw a placeholder rectangle
    int padding = 10;
    int width = LOGO_WIDTH - padding * 2;
    int height = LOGO_HEIGHT - padding * 2;
    
    for (int dy = 0; dy < height; dy++) {
        for (int dx = 0; dx < width; dx++) {
            int px = x + dx + padding;
            int py = y + dy + padding;
            
            if (px >= 0 && px < bitmap->width && py >= 0 && py < bitmap->height) {
                // Create a simple text-like pattern for "NEOBENCH"
                // This is just a placeholder - a real implementation would use a bitmap
                if ((dy > height / 3 && dy < height * 2 / 3) || 
                    (dx < width / 8) || (dx > width * 7 / 8) ||
                    (dx > width * 3 / 8 && dx < width * 5 / 8)) {
                    bitmap->pixels[py * bitmap->width + px] = color;
                }
            }
        }
    }
}

/**
 * Fill a bitmap with a vertical gradient
 */
void gradient_fill(Bitmap *bitmap, uint32_t top_color, uint32_t bottom_color) {
    for (int y = 0; y < bitmap->height; y++) {
        float t = (float)y / bitmap->height;
        uint32_t color = blend_colors(top_color, bottom_color, t);
        
        for (int x = 0; x < bitmap->width; x++) {
            bitmap->pixels[y * bitmap->width + x] = color;
        }
    }
}

/**
 * Blend two colors
 * t is between 0.0 and 1.0, where 0.0 gives c1 and 1.0 gives c2
 */
uint32_t blend_colors(uint32_t c1, uint32_t c2, float t) {
    if (t <= 0.0f) return c1;
    if (t >= 1.0f) return c2;
    
    uint32_t r1 = (c1 >> 16) & 0xFF;
    uint32_t g1 = (c1 >> 8) & 0xFF;
    uint32_t b1 = c1 & 0xFF;
    
    uint32_t r2 = (c2 >> 16) & 0xFF;
    uint32_t g2 = (c2 >> 8) & 0xFF;
    uint32_t b2 = c2 & 0xFF;
    
    uint32_t r = (uint32_t)(r1 + t * (r2 - r1));
    uint32_t g = (uint32_t)(g1 + t * (g2 - g1));
    uint32_t b = (uint32_t)(b1 + t * (b2 - b1));
    
    return (r << 16) | (g << 8) | b;
}

/**
 * Apply a simple blur effect to a bitmap
 */
void add_blur(Bitmap *bitmap, int radius) {
    // This is a very simple box blur implementation
    // A real implementation would use a more efficient algorithm
    
    // Create temporary buffer for the blur
    uint32_t *temp = (uint32_t*)malloc(bitmap->width * bitmap->height * sizeof(uint32_t));
    if (!temp) return;
    
    // Horizontal blur
    for (int y = 0; y < bitmap->height; y++) {
        for (int x = 0; x < bitmap->width; x++) {
            uint32_t r_sum = 0, g_sum = 0, b_sum = 0;
            int count = 0;
            
            for (int dx = -radius; dx <= radius; dx++) {
                int px = x + dx;
                if (px >= 0 && px < bitmap->width) {
                    uint32_t color = bitmap->pixels[y * bitmap->width + px];
                    r_sum += (color >> 16) & 0xFF;
                    g_sum += (color >> 8) & 0xFF;
                    b_sum += color & 0xFF;
                    count++;
                }
            }
            
            uint32_t r = r_sum / count;
            uint32_t g = g_sum / count;
            uint32_t b = b_sum / count;
            temp[y * bitmap->width + x] = (r << 16) | (g << 8) | b;
        }
    }
    
    // Vertical blur
    for (int y = 0; y < bitmap->height; y++) {
        for (int x = 0; x < bitmap->width; x++) {
            uint32_t r_sum = 0, g_sum = 0, b_sum = 0;
            int count = 0;
            
            for (int dy = -radius; dy <= radius; dy++) {
                int py = y + dy;
                if (py >= 0 && py < bitmap->height) {
                    uint32_t color = temp[py * bitmap->width + x];
                    r_sum += (color >> 16) & 0xFF;
                    g_sum += (color >> 8) & 0xFF;
                    b_sum += color & 0xFF;
                    count++;
                }
            }
            
            uint32_t r = r_sum / count;
            uint32_t g = g_sum / count;
            uint32_t b = b_sum / count;
            bitmap->pixels[y * bitmap->width + x] = (r << 16) | (g << 8) | b;
        }
    }
    
    free(temp);
}