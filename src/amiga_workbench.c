/**
 * NeoBench Amiga Workbench Emulation
 *
 * This module provides a basic Workbench-like graphical environment
 * for the NeoBench ROM, inspired by AmigaOS Workbench.
 */

#include <stdint.h>
#include <string.h>

// Constants
#define MAX_WINDOWS 8
#define MAX_ICONS 64
#define MAX_MENU_ITEMS 32
#define MAX_STRING_LENGTH 64

// Screen resolution
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 256

// Amiga OS Version
#define OS_VERSION "AmigaOS 3.9"
#define WB_VERSION "Workbench 3.9"

// Workbench colors (default 8-color palette)
#define COLOR_BACKGROUND    0
#define COLOR_DETAIL        1
#define COLOR_BLOCK         2
#define COLOR_TEXT          3
#define COLOR_SHINE         4
#define COLOR_SHADOW        5
#define COLOR_FILL          6
#define COLOR_HIGHLIGHT     7

// Window flags
#define WFLAG_DRAGGABLE     0x0001
#define WFLAG_SIZEABLE      0x0002
#define WFLAG_CLOSABLE      0x0004
#define WFLAG_DEPTHABLE     0x0008
#define WFLAG_ZOOMED        0x0010
#define WFLAG_BACKDROP      0x0020
#define WFLAG_BORDERLESS    0x0040
#define WFLAG_ACTIVE        0x0080

// Icon types
#define ICON_DISK           0
#define ICON_DRAWER         1
#define ICON_TOOL           2
#define ICON_PROJECT        3
#define ICON_TRASHCAN       4
#define ICON_DEVICE         5
#define ICON_KICKDISK       6
#define ICON_RAM            7

// Forward declarations for structures
typedef struct Window Window;
typedef struct Icon Icon;
typedef struct MenuItem MenuItem;
typedef struct Menu Menu;
typedef struct Screen Screen;

// Window structure
struct Window {
    uint16_t id;                     // Window identifier
    char title[MAX_STRING_LENGTH];   // Window title
    uint16_t x, y;                   // Window position
    uint16_t width, height;          // Window size
    uint16_t min_width, min_height;  // Minimum window size
    uint16_t flags;                  // Window flags
    uint8_t  active;                 // Is window active?
    void (*refresh)(Window*);        // Refresh function
    void (*close)(Window*);          // Close function
    void *user_data;                 // User data pointer
};

// Icon structure
struct Icon {
    uint16_t id;                     // Icon identifier
    char name[MAX_STRING_LENGTH];    // Icon name
    uint16_t x, y;                   // Icon position
    uint8_t  type;                   // Icon type
    uint8_t  selected;               // Is icon selected?
    uint32_t image_data;             // Pointer to icon image data
    uint16_t width, height;          // Icon size
    void (*activate)(Icon*);         // Double-click function
    void *user_data;                 // User data pointer
};

// Menu item structure
struct MenuItem {
    uint16_t id;                     // Menu item identifier
    char text[MAX_STRING_LENGTH];    // Menu item text
    uint8_t  enabled;                // Is menu item enabled?
    uint8_t  checked;                // Is menu item checked?
    char shortcut;                   // Keyboard shortcut
    void (*activate)(MenuItem*);     // Activation function
};

// Menu structure
struct Menu {
    uint16_t id;                     // Menu identifier
    char title[MAX_STRING_LENGTH];   // Menu title
    MenuItem items[MAX_MENU_ITEMS];  // Menu items
    uint16_t item_count;             // Number of items in menu
};

// Workbench screen structure
struct Screen {
    char title[MAX_STRING_LENGTH];   // Screen title
    uint16_t width, height;          // Screen dimensions
    uint8_t  depth;                  // Screen depth (bitplanes)
    Window  *windows[MAX_WINDOWS];   // Windows on screen
    uint16_t window_count;           // Number of windows
    Icon    *icons[MAX_ICONS];       // Icons on screen
    uint16_t icon_count;             // Number of icons
    Menu    *menus[8];               // Screen menus
    uint16_t menu_count;             // Number of menus
    uint32_t backdrop_pattern;       // Backdrop pattern pointer
};

// Global workbench state
static Screen workbench;

// Function prototypes
void wb_init(void);
void wb_reset(void);
Window* wb_create_window(const char *title, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t flags);
void wb_close_window(Window *window);
void wb_activate_window(Window *window);
void wb_refresh_window(Window *window);
void wb_move_window(Window *window, uint16_t x, uint16_t y);
void wb_resize_window(Window *window, uint16_t width, uint16_t height);
Icon* wb_create_icon(const char *name, uint16_t x, uint16_t y, uint8_t type, uint32_t image_data);
void wb_select_icon(Icon *icon);
void wb_deselect_all_icons(void);
void wb_move_icon(Icon *icon, uint16_t x, uint16_t y);
void wb_activate_icon(Icon *icon);
Menu* wb_create_menu(const char *title);
MenuItem* wb_add_menu_item(Menu *menu, const char *text, char shortcut, void (*activate)(MenuItem*));
void wb_enable_menu_item(MenuItem *item);
void wb_disable_menu_item(MenuItem *item);
void wb_check_menu_item(MenuItem *item);
void wb_uncheck_menu_item(MenuItem *item);
void wb_refresh_screen(void);
void wb_handle_input(uint16_t input_event);
void wb_draw_backdrop(void);

/**
 * Initialize the Workbench environment
 */
void wb_init(void) {
    wb_reset();
    
    // Set screen properties
    strncpy(workbench.title, WB_VERSION, MAX_STRING_LENGTH);
    workbench.width = SCREEN_WIDTH;
    workbench.height = SCREEN_HEIGHT;
    workbench.depth = 3; // 8 colors (3 bitplanes)
    
    // Create standard menus
    Menu *workbench_menu = wb_create_menu("Workbench");
    Menu *disk_menu = wb_create_menu("Disk");
    Menu *tools_menu = wb_create_menu("Tools");
    Menu *window_menu = wb_create_menu("Window");
    
    // Add standard Workbench menu items
    wb_add_menu_item(workbench_menu, "About...", 'A', NULL);
    wb_add_menu_item(workbench_menu, "Backdrop", 'B', NULL);
    wb_add_menu_item(workbench_menu, "Execute Command...", 'E', NULL);
    wb_add_menu_item(workbench_menu, "Redraw All", 'R', NULL);
    wb_add_menu_item(workbench_menu, "Update All", 'U', NULL);
    wb_add_menu_item(workbench_menu, "Last Message", 'L', NULL);
    wb_add_menu_item(workbench_menu, "About NeoBench...", 'N', NULL);
    wb_add_menu_item(workbench_menu, "Quit...", 'Q', NULL);
    
    // Create standard icons
    wb_create_icon("RAM Disk", 20, 20, ICON_RAM, 0);
    wb_create_icon("Workbench", 80, 20, ICON_DISK, 0);
    wb_create_icon("NeoBench", 140, 20, ICON_DISK, 0);
    wb_create_icon("Trashcan", 200, 20, ICON_TRASHCAN, 0);
    
    // Create Workbench window
    Window *wb_window = wb_create_window("Workbench", 60, 40, 400, 150, 
                                        WFLAG_DRAGGABLE | WFLAG_SIZEABLE | 
                                        WFLAG_CLOSABLE | WFLAG_DEPTHABLE);
    wb_activate_window(wb_window);
    
    // Draw the backdrop
    wb_draw_backdrop();
}

/**
 * Reset the Workbench environment
 */
void wb_reset(void) {
    // Clear the Workbench screen structure
    memset(&workbench, 0, sizeof(Screen));
}

/**
 * Create a new window
 */
Window* wb_create_window(const char *title, uint16_t x, uint16_t y, 
                        uint16_t width, uint16_t height, uint16_t flags) {
    if (workbench.window_count >= MAX_WINDOWS) {
        return NULL; // Too many windows
    }
    
    // Allocate window memory
    Window *window = (Window*)malloc(sizeof(Window));
    if (!window) {
        return NULL;
    }
    
    // Initialize window properties
    window->id = workbench.window_count;
    strncpy(window->title, title, MAX_STRING_LENGTH);
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    window->min_width = 100;
    window->min_height = 40;
    window->flags = flags;
    window->active = 0;
    window->refresh = NULL;
    window->close = NULL;
    window->user_data = NULL;
    
    // Add window to screen
    workbench.windows[workbench.window_count++] = window;
    
    // Refresh screen
    wb_refresh_screen();
    
    return window;
}

/**
 * Close a window
 */
void wb_close_window(Window *window) {
    if (!window) return;
    
    // Find window in screen array
    for (int i = 0; i < workbench.window_count; i++) {
        if (workbench.windows[i] == window) {
            // Call window close function if defined
            if (window->close) {
                window->close(window);
            }
            
            // Remove window from array by shifting remaining windows
            for (int j = i; j < workbench.window_count - 1; j++) {
                workbench.windows[j] = workbench.windows[j+1];
            }
            workbench.window_count--;
            
            // Free window memory
            free(window);
            
            // Refresh screen
            wb_refresh_screen();
            break;
        }
    }
}

/**
 * Activate a window (bring to front)
 */
void wb_activate_window(Window *window) {
    if (!window) return;
    
    // Deactivate all windows
    for (int i = 0; i < workbench.window_count; i++) {
        workbench.windows[i]->active = 0;
    }
    
    // Find window in screen array
    for (int i = 0; i < workbench.window_count; i++) {
        if (workbench.windows[i] == window) {
            // Move window to front of array
            Window *temp = workbench.windows[i];
            for (int j = i; j > 0; j--) {
                workbench.windows[j] = workbench.windows[j-1];
            }
            workbench.windows[0] = temp;
            
            // Activate window
            temp->active = 1;
            temp->flags |= WFLAG_ACTIVE;
            
            // Refresh screen
            wb_refresh_screen();
            break;
        }
    }
}

/**
 * Refresh a window
 */
void wb_refresh_window(Window *window) {
    if (!window) return;
    
    // Call window refresh function if defined
    if (window->refresh) {
        window->refresh(window);
    }
}

/**
 * Move a window
 */
void wb_move_window(Window *window, uint16_t x, uint16_t y) {
    if (!window) return;
    
    window->x = x;
    window->y = y;
    
    // Refresh screen
    wb_refresh_screen();
}

/**
 * Resize a window
 */
void wb_resize_window(Window *window, uint16_t width, uint16_t height) {
    if (!window) return;
    
    // Enforce minimum size
    if (width < window->min_width) width = window->min_width;
    if (height < window->min_height) height = window->min_height;
    
    window->width = width;
    window->height = height;
    
    // Refresh screen
    wb_refresh_screen();
}

/**
 * Create a new icon
 */
Icon* wb_create_icon(const char *name, uint16_t x, uint16_t y, uint8_t type, uint32_t image_data) {
    if (workbench.icon_count >= MAX_ICONS) {
        return NULL; // Too many icons
    }
    
    // Allocate icon memory
    Icon *icon = (Icon*)malloc(sizeof(Icon));
    if (!icon) {
        return NULL;
    }
    
    // Initialize icon properties
    icon->id = workbench.icon_count;
    strncpy(icon->name, name, MAX_STRING_LENGTH);
    icon->x = x;
    icon->y = y;
    icon->type = type;
    icon->selected = 0;
    icon->image_data = image_data;
    icon->width = 32;  // Default icon size
    icon->height = 32;
    icon->activate = NULL;
    icon->user_data = NULL;
    
    // Add icon to screen
    workbench.icons[workbench.icon_count++] = icon;
    
    // Refresh screen
    wb_refresh_screen();
    
    return icon;
}

/**
 * Select an icon
 */
void wb_select_icon(Icon *icon) {
    if (!icon) return;
    
    // Select icon
    icon->selected = 1;
    
    // Refresh screen
    wb_refresh_screen();
}

/**
 * Deselect all icons
 */
void wb_deselect_all_icons(void) {
    // Deselect all icons
    for (int i = 0; i < workbench.icon_count; i++) {
        workbench.icons[i]->selected = 0;
    }
    
    // Refresh screen
    wb_refresh_screen();
}

/**
 * Move an icon
 */
void wb_move_icon(Icon *icon, uint16_t x, uint16_t y) {
    if (!icon) return;
    
    icon->x = x;
    icon->y = y;
    
    // Refresh screen
    wb_refresh_screen();
}

/**
 * Activate an icon (double-click)
 */
void wb_activate_icon(Icon *icon) {
    if (!icon) return;
    
    // Call icon activation function if defined
    if (icon->activate) {
        icon->activate(icon);
    }
}

/**
 * Create a new menu
 */
Menu* wb_create_menu(const char *title) {
    if (workbench.menu_count >= 8) {
        return NULL; // Too many menus
    }
    
    // Allocate menu memory
    Menu *menu = (Menu*)malloc(sizeof(Menu));
    if (!menu) {
        return NULL;
    }
    
    // Initialize menu properties
    menu->id = workbench.menu_count;
    strncpy(menu->title, title, MAX_STRING_LENGTH);
    menu->item_count = 0;
    
    // Add menu to screen
    workbench.menus[workbench.menu_count++] = menu;
    
    return menu;
}

/**
 * Add a menu item to a menu
 */
MenuItem* wb_add_menu_item(Menu *menu, const char *text, char shortcut, void (*activate)(MenuItem*)) {
    if (!menu || menu->item_count >= MAX_MENU_ITEMS) {
        return NULL; // Invalid menu or too many items
    }
    
    // Get next menu item
    MenuItem *item = &menu->items[menu->item_count++];
    
    // Initialize menu item properties
    item->id = menu->item_count - 1;
    strncpy(item->text, text, MAX_STRING_LENGTH);
    item->enabled = 1;
    item->checked = 0;
    item->shortcut = shortcut;
    item->activate = activate;
    
    return item;
}

/**
 * Enable a menu item
 */
void wb_enable_menu_item(MenuItem *item) {
    if (!item) return;
    item->enabled = 1;
}

/**
 * Disable a menu item
 */
void wb_disable_menu_item(MenuItem *item) {
    if (!item) return;
    item->enabled = 0;
}

/**
 * Check a menu item
 */
void wb_check_menu_item(MenuItem *item) {
    if (!item) return;
    item->checked = 1;
}

/**
 * Uncheck a menu item
 */
void wb_uncheck_menu_item(MenuItem *item) {
    if (!item) return;
    item->checked = 0;
}

/**
 * Refresh the entire screen
 */
void wb_refresh_screen(void) {
    // Draw backdrop
    wb_draw_backdrop();
    
    // Draw icons
    for (int i = 0; i < workbench.icon_count; i++) {
        // Draw icon (implementation would go here)
    }
    
    // Draw windows (back to front)
    for (int i = workbench.window_count - 1; i >= 0; i--) {
        // Draw window (implementation would go here)
        
        // Call window refresh function if defined
        if (workbench.windows[i]->refresh) {
            workbench.windows[i]->refresh(workbench.windows[i]);
        }
    }
}

/**
 * Handle input events
 */
void wb_handle_input(uint16_t input_event) {
    // Handle input events (implementation would go here)
}

/**
 * Draw the Workbench backdrop pattern
 */
void wb_draw_backdrop(void) {
    // Draw backdrop pattern (implementation would go here)
}