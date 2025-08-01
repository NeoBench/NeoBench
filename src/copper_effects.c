/**
 * NeoBench Copper Effects Engine
 *
 * This module provides Amiga-style copper list effects for the NeoBench ROM.
 * The copper was a dedicated co-processor in the Amiga's chipset that could
 * perform timed writes to the chipset registers, allowing for sophisticated
 * visual effects synchronized with the display raster.
 */

#include <stdint.h>
#include <string.h>

// Copper instruction types
#define COPPER_WAIT   0   // Wait for beam position
#define COPPER_MOVE   1   // Move data to register
#define COPPER_SKIP   2   // Skip if beam position reached
#define COPPER_END    3   // End of copper list

// Maximum copper list size
#define MAX_COPPER_INSTRUCTIONS 256

// Screen dimensions (PAL)
#define SCREEN_WIDTH  640
#define SCREEN_HEIGHT 256
#define MAX_LINES     312  // PAL total scanlines including vertical blanking

// Copper instruction structure
typedef struct {
    uint8_t  type;         // Instruction type
    uint16_t reg_addr;     // Register address (for MOVE)
    uint16_t data;         // Data value (for MOVE) or position (for WAIT/SKIP)
    uint16_t mask;         // Mask for SKIP comparison
} Copper_Instruction;

// Copper list structure
typedef struct {
    Copper_Instruction instructions[MAX_COPPER_INSTRUCTIONS];
    int count;             // Number of instructions
    int current;           // Current instruction being executed
    uint8_t active;        // Is copper list active?
} Copper_List;

// Current raster position
static uint16_t beam_x = 0;
static uint16_t beam_y = 0;

// Global copper lists
static Copper_List copper_lists[2];
static Copper_List *active_list = NULL;

// Function prototypes
void copper_init(void);
void copper_reset(void);
void copper_set_active_list(int list_num);
int copper_add_wait(int list_num, uint16_t x, uint16_t y);
int copper_add_move(int list_num, uint16_t reg, uint16_t data);
int copper_add_skip(int list_num, uint16_t x, uint16_t y, uint16_t mask);
int copper_add_end(int list_num);
void copper_clear_list(int list_num);
void copper_execute_cycle(void);
void copper_create_gradient_background(int list_num);
void copper_create_color_bars(int list_num);
void copper_create_split_screen(int list_num, uint16_t split_y);
void copper_create_parallax_effect(int list_num);

/**
 * Initialize copper lists
 */
void copper_init(void) {
    copper_reset();
}

/**
 * Reset copper lists
 */
void copper_reset(void) {
    // Clear both copper lists
    memset(&copper_lists[0], 0, sizeof(Copper_List));
    memset(&copper_lists[1], 0, sizeof(Copper_List));
    
    active_list = NULL;
    beam_x = 0;
    beam_y = 0;
}

/**
 * Set the active copper list
 */
void copper_set_active_list(int list_num) {
    if (list_num < 0 || list_num > 1) {
        return;
    }
    
    active_list = &copper_lists[list_num];
    active_list->active = 1;
    active_list->current = 0;
}

/**
 * Add a WAIT instruction to a copper list
 */
int copper_add_wait(int list_num, uint16_t x, uint16_t y) {
    if (list_num < 0 || list_num > 1) {
        return -1;
    }
    
    Copper_List *list = &copper_lists[list_num];
    
    if (list->count >= MAX_COPPER_INSTRUCTIONS) {
        return -1;
    }
    
    // Create WAIT instruction
    list->instructions[list->count].type = COPPER_WAIT;
    list->instructions[list->count].data = (y << 8) | (x & 0xFF);
    list->count++;
    
    return list->count - 1;
}

/**
 * Add a MOVE instruction to a copper list
 */
int copper_add_move(int list_num, uint16_t reg, uint16_t data) {
    if (list_num < 0 || list_num > 1) {
        return -1;
    }
    
    Copper_List *list = &copper_lists[list_num];
    
    if (list->count >= MAX_COPPER_INSTRUCTIONS) {
        return -1;
    }
    
    // Create MOVE instruction
    list->instructions[list->count].type = COPPER_MOVE;
    list->instructions[list->count].reg_addr = reg;
    list->instructions[list->count].data = data;
    list->count++;
    
    return list->count - 1;
}

/**
 * Add a SKIP instruction to a copper list
 */
int copper_add_skip(int list_num, uint16_t x, uint16_t y, uint16_t mask) {
    if (list_num < 0 || list_num > 1) {
        return -1;
    }
    
    Copper_List *list = &copper_lists[list_num];
    
    if (list->count >= MAX_COPPER_INSTRUCTIONS) {
        return -1;
    }
    
    // Create SKIP instruction
    list->instructions[list->count].type = COPPER_SKIP;
    list->instructions[list->count].data = (y << 8) | (x & 0xFF);
    list->instructions[list->count].mask = mask;
    list->count++;
    
    return list->count - 1;
}

/**
 * Add an END instruction to a copper list
 */
int copper_add_end(int list_num) {
    if (list_num < 0 || list_num > 1) {
        return -1;
    }
    
    Copper_List *list = &copper_lists[list_num];
    
    if (list->count >= MAX_COPPER_INSTRUCTIONS) {
        return -1;
    }
    
    // Create END instruction
    list->instructions[list->count].type = COPPER_END;
    list->count++;
    
    return list->count - 1;
}

/**
 * Clear a copper list
 */
void copper_clear_list(int list_num) {
    if (list_num < 0 || list_num > 1) {
        return;
    }
    
    Copper_List *list = &copper_lists[list_num];
    list->count = 0;
    list->current = 0;
}

/**
 * Execute one cycle of the copper
 * This should be called once per horizontal raster line
 */
void copper_execute_cycle(void) {
    if (!active_list || !active_list->active) {
        return;
    }
    
    int executed = 0;
    int max_instructions = 16; // Limit instruction per cycle for safety
    
    // Execute instructions until a WAIT is hit or list ends
    while (active_list->current < active_list->count && executed < max_instructions) {
        Copper_Instruction *instr = &active_list->instructions[active_list->current];
        
        switch (instr->type) {
            case COPPER_WAIT:
                {
                    uint16_t wait_y = instr->data >> 8;
                    uint16_t wait_x = instr->data & 0xFF;
                    
                    // If we haven't reached the wait position, stop execution for now
                    if (beam_y < wait_y || (beam_y == wait_y && beam_x < wait_x)) {
                        return;
                    }
                    
                    // Otherwise, move to next instruction
                    active_list->current++;
                }
                break;
                
            case COPPER_MOVE:
                // Write data to register
                // In a real implementation, this would write to actual hardware
                // Here, we would map register writes to our emulated chipset
                active_list->current++;
                break;
                
            case COPPER_SKIP:
                {
                    uint16_t skip_y = instr->data >> 8;
                    uint16_t skip_x = instr->data & 0xFF;
                    
                    // If we have reached or passed the position, skip next instruction
                    if (beam_y > skip_y || (beam_y == skip_y && beam_x >= skip_x)) {
                        active_list->current += 2;
                    } else {
                        active_list->current++;
                    }
                }
                break;
                
            case COPPER_END:
                // End of list, reset to beginning
                active_list->current = 0;
                return;
                
            default:
                // Invalid instruction, move to next
                active_list->current++;
                break;
        }
        
        executed++;
    }
}

/**
 * Update beam position for next raster line
 */
void copper_update_beam(void) {
    // Move beam to next line
    beam_x = 0;
    beam_y++;
    
    // End of frame, reset beam position
    if (beam_y >= MAX_LINES) {
        beam_y = 0;
    }
    
    // Execute copper for this line
    copper_execute_cycle();
}

/**
 * Create a background gradient using the copper
 * This demonstrates how the copper can change colors on different scanlines
 */
void copper_create_gradient_background(int list_num) {
    copper_clear_list(list_num);
    
    // Set up initial colors
    copper_add_move(list_num, 0x180, 0x000); // Background color (black)
    
    // Create gradient from blue to red
    for (int y = 0; y < SCREEN_HEIGHT; y += 4) {
        // Wait for scanline
        copper_add_wait(list_num, 0, y);
        
        // Calculate gradient color (0x000 to 0xF00)
        uint16_t blue_val = 0xF - ((y * 0xF) / SCREEN_HEIGHT);
        uint16_t red_val = (y * 0xF) / SCREEN_HEIGHT;
        uint16_t color = (red_val << 8) | blue_val;
        
        // Change background color
        copper_add_move(list_num, 0x180, color);
    }
    
    // End the list
    copper_add_end(list_num);
}

/**
 * Create horizontal color bars using the copper
 */
void copper_create_color_bars(int list_num) {
    copper_clear_list(list_num);
    
    // Array of colors for the bars
    uint16_t colors[] = {
        0x000,  // Black
        0xF00,  // Red
        0x0F0,  // Green
        0xFF0,  // Yellow
        0x00F,  // Blue
        0xF0F,  // Magenta
        0x0FF,  // Cyan
        0xFFF   // White
    };
    
    int bar_height = SCREEN_HEIGHT / 8;
    
    for (int i = 0; i < 8; i++) {
        // Wait for start of bar
        copper_add_wait(list_num, 0, i * bar_height);
        
        // Set background color to bar color
        copper_add_move(list_num, 0x180, colors[i]);
    }
    
    // End the list
    copper_add_end(list_num);
}

/**
 * Create split screen effect using the copper
 * This demonstrates using the copper to create a display with
 * different characteristics at different vertical positions
 */
void copper_create_split_screen(int list_num, uint16_t split_y) {
    copper_clear_list(list_num);
    
    // Set up initial display configuration
    copper_add_move(list_num, 0x100, 0x1200); // BPLCON0: 1 bitplane
    copper_add_move(list_num, 0x180, 0x00F);  // COLOR00: Dark blue
    copper_add_move(list_num, 0x182, 0xFFF);  // COLOR01: White
    
    // Wait for split line
    copper_add_wait(list_num, 0, split_y);
    
    // Change configuration for bottom half
    copper_add_move(list_num, 0x100, 0x3200); // BPLCON0: 2 bitplanes
    copper_add_move(list_num, 0x180, 0x300);  // COLOR00: Dark red
    copper_add_move(list_num, 0x182, 0xF80);  // COLOR01: Orange
    copper_add_move(list_num, 0x184, 0xFF0);  // COLOR02: Yellow
    copper_add_move(list_num, 0x186, 0xFFF);  // COLOR03: White
    
    // End the list
    copper_add_end(list_num);
}

/**
 * Create a parallax scrolling effect using the copper
 * This demonstrates how the copper can be used to create more complex effects
 * by manipulating the scroll registers at different vertical positions
 */
void copper_create_parallax_effect(int list_num) {
    copper_clear_list(list_num);
    
    // Set up initial bitplane pointers and modulos
    copper_add_move(list_num, 0x0E0, 0x0000); // BPL1PTH (high word of bitplane 1 pointer)
    copper_add_move(list_num, 0x0E2, 0x0000); // BPL1PTL (low word of bitplane 1 pointer)
    copper_add_move(list_num, 0x0E4, 0x0000); // BPL2PTH (high word of bitplane 2 pointer)
    copper_add_move(list_num, 0x0E6, 0x0000); // BPL2PTL (low word of bitplane 2 pointer)
    copper_add_move(list_num, 0x108, 0x0000); // BPL1MOD (bitplane 1 modulo)
    copper_add_move(list_num, 0x10A, 0x0000); // BPL2MOD (bitplane 2 modulo)
    
    // Set up initial colors
    copper_add_move(list_num, 0x180, 0x000); // Background
    copper_add_move(list_num, 0x182, 0x00A); // Far mountains (dark blue)
    copper_add_move(list_num, 0x184, 0x050); // Middle mountains (green)
    copper_add_move(list_num, 0x186, 0x830); // Foreground (brown)
    
    // Scroll registers for different layers
    for (int y = 40; y < 100; y += 2) {
        // Far mountains (slow scroll)
        copper_add_wait(list_num, 0, y);
        copper_add_move(list_num, 0x102, 0x0001); // BPLCON1: Scroll 1 pixel
    }
    
    for (int y = 100; y < 160; y += 2) {
        // Middle mountains (medium scroll)
        copper_add_wait(list_num, 0, y);
        copper_add_move(list_num, 0x102, 0x0002); // BPLCON1: Scroll 2 pixels
    }
    
    for (int y = 160; y < 220; y += 2) {
        // Foreground (fast scroll)
        copper_add_wait(list_num, 0, y);
        copper_add_move(list_num, 0x102, 0x0003); // BPLCON1: Scroll 3 pixels
    }
    
    // Reset scroll at bottom of screen
    copper_add_wait(list_num, 0, 220);
    copper_add_move(list_num, 0x102, 0x0000); // BPLCON1: No scroll
    
    // End the list
    copper_add_end(list_num);
}

/**
 * Create a classic Amiga-style copperbars effect
 */
void copper_create_copperbars(int list_num) {
    copper_clear_list(list_num);
    
    // Set up initial colors
    copper_add_move(list_num, 0x180, 0x000); // Background (black)
    
    // Create moving copper bars with gradients
    for (int bar = 0; bar < 3; bar++) {
        int bar_start = 40 + (bar * 60);
        int bar_width = 20;
        
        // Create gradient up
        for (int i = 0; i < bar_width; i++) {
            copper_add_wait(list_num, 0, bar_start + i);
            
            // Calculate gradient color
            uint16_t intensity = (i * 15) / bar_width;
            uint16_t color = 0;
            
            switch (bar) {
                case 0: color = intensity << 8; break; // Red gradient
                case 1: color = intensity << 4; break; // Green gradient
                case 2: color = intensity;      break; // Blue gradient
            }
            
            copper_add_move(list_num, 0x180, color);
        }
        
        // Create gradient down
        for (int i = 0; i < bar_width; i++) {
            copper_add_wait(list_num, 0, bar_start + bar_width + i);
            
            // Calculate gradient color
            uint16_t intensity = 15 - ((i * 15) / bar_width);
            uint16_t color = 0;
            
            switch (bar) {
                case 0: color = intensity << 8; break; // Red gradient
                case 1: color = intensity << 4; break; // Green gradient
                case 2: color = intensity;      break; // Blue gradient
            }
            
            copper_add_move(list_num, 0x180, color);
        }
    }
    
    // End the list
    copper_add_end(list_num);
}