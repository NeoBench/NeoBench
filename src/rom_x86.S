.section .text
    .globl _start

# NeoBench Boot ROM for x86 (Enhanced Version v2)
# Compact implementation with SCSI, Floppy and USB boot options
# Includes BIOS setup interface for configuration
_start:
    # Initialize stack
    movl $0x7FFE, %esp
    
    # Initialize status registers
    xorl %eax, %eax
    movl %eax, boot_status
    
    # Check for special boot keys
    call check_boot_keys
    
    # Determine boot mode (normal, debug, or setup)
    cmpl $0x4445425F, %eax   # Check for "DEB_" debug flag
    je debug_mode
    cmpl $0x1, setup_key_pressed
    je bios_setup_mode
    
normal_boot:
    # Normal boot sequence
    movl $boot_message, %eax     # Store boot message pointer
    
    # Core initialization
    call init_hardware
    call detect_hardware
    call init_display
    call set_colors
    call draw_logo
    call play_sound
    
    # Check for available boot devices and boot priority
    call scan_boot_devices
    call boot_from_priority
    
    # If we reach here, no bootable device was found
    call no_bootable_device_error
    
    # Signal boot complete but failed
    movl $0xB007FA17, %eax    # BOOT_FAIL magic number
    jmp halt_loop

debug_mode:
    # Debug mode initialization
    movl $debug_message, %eax
    
    # Skip logo and sound in debug mode
    call init_hardware
    call detect_hardware
    call init_display
    call set_colors
    
    # Set debug flag for hardware
    movl $0x1, debug_flag
    
    # Display debug info
    call show_debug_info
    
    # Signal debug mode active
    movl $0xDEBF1A6, %eax    # DEBUG_FLAG magic number
    jmp halt_loop

bios_setup_mode:
    # BIOS Setup mode - display configuration interface
    movl $setup_message, %eax
    
    # Initialize display for setup interface
    call init_hardware
    call init_display
    call setup_display_mode
    
    # Display BIOS setup interface
    call display_bios_setup
    
    # Signal BIOS setup mode
    movl $0x5E7B105, %eax    # SETUP magic number

check_boot_keys:
    # Check for special keys pressed during boot
    # F2 for BIOS setup
    movl $0, setup_key_pressed
    
    # Simulated key detection (would be hardware dependent)
    movl boot_key_check, %eax
    cmpl $0xF2, %eax
    jne no_setup_key
    movl $1, setup_key_pressed
    
no_setup_key:
    ret

scan_boot_devices:
    # Scan for bootable devices based on detected hardware
    movl $0, bootable_device_count
    
    # Check boot priority from NVRAM
    movl boot_priority, %eax
    movl %eax, current_boot_priority
    
    # Scan USB devices first if in boot priority
    cmpl $1, usb_controller_present
    je scan_usb_devices
    
    # Skip USB scan if no controller
    jmp scan_scsi_devices
    
scan_usb_devices:
    # Scan for bootable USB devices
    movl usb_device_count, %ecx
    test %ecx, %ecx
    jz scan_scsi_devices
    
    movl $0, %edi
    leal usb_devices, %esi
    
usb_scan_loop:
    # Check if device is bootable
    cmpb $1, 8(%esi)
    jne usb_next_device
    
    # Add to bootable device list
    movl bootable_device_count, %eax
    movl $2, bootable_devices(,%eax,4)  # Type 2 = USB
    movl %edi, bootable_device_ids(,%eax,4)
    incl bootable_device_count
    
usb_next_device:
    addl $12, %esi
    incl %edi
    loop usb_scan_loop
    
scan_scsi_devices:
    # Scan for bootable SCSI devices
    cmpl $1, scsi_controller_present
    jne scan_floppy_devices
    
    movl scsi_device_count, %ecx
    test %ecx, %ecx
    jz scan_floppy_devices
    
    movl $0, %edi
    leal scsi_devices, %esi
    
scsi_scan_loop:
    # Check if device is bootable
    cmpb $1, 8(%esi)
    jne scsi_next_device
    
    # Add to bootable device list
    movl bootable_device_count, %eax
    movl $1, bootable_devices(,%eax,4)  # Type 1 = SCSI
    movl %edi, bootable_device_ids(,%eax,4)
    incl bootable_device_count
    
scsi_next_device:
    addl $12, %esi
    incl %edi
    loop scsi_scan_loop

scan_floppy_devices:
    # Scan for bootable floppy devices
    cmpl $1, floppy_controller_present
    jne scan_complete
    
    movl floppy_drive_count, %ecx
    test %ecx, %ecx
    jz scan_complete
    
    movl $0, %edi
    leal floppy_drives, %esi
    
floppy_scan_loop:
    # Check if device is bootable
    cmpb $1, 8(%esi)
    jne floppy_next_device
    
    # Add to bootable device list
    movl bootable_device_count, %eax
    movl $0, bootable_devices(,%eax,4)  # Type 0 = Floppy
    movl %edi, bootable_device_ids(,%eax,4)
    incl bootable_device_count
    
floppy_next_device:
    addl $12, %esi
    incl %edi
    loop floppy_scan_loop

scan_complete:
    ret

boot_from_priority:
    # Try to boot from devices according to priority
    movl bootable_device_count, %ecx
    test %ecx, %ecx
    jz no_bootable_devices
    
    # Display boot device selection
    call display_boot_devices
    
    # Try to boot from first bootable device (in real implementation, try all in order)
    movl $0, %edi
    movl bootable_devices(,%edi,4), %eax  # Device type
    movl bootable_device_ids(,%edi,4), %ebx  # Device ID
    
    # Call appropriate boot handler based on device type
    cmpl $0, %eax
    je boot_from_floppy
    
    cmpl $1, %eax
    je boot_from_scsi
    
    cmpl $2, %eax
    je boot_from_usb
    
    # No matching handler, try next device
    incl %edi
    cmpl bootable_device_count, %edi
    jb try_next_boot_device
    
    # No more devices to try
    jmp no_bootable_devices
    
try_next_boot_device:
    # In a real implementation, this would loop through all devices
    jmp no_bootable_devices
    
boot_from_floppy:
    # Boot from floppy drive
    call boot_message_floppy
    # Here would be code to read boot sector from floppy
    # and transfer control to it
    ret
    
boot_from_scsi:
    # Boot from SCSI device
    call boot_message_scsi
    # Here would be code to read boot sector from SCSI
    # and transfer control to it
    ret
    
boot_from_usb:
    # Boot from USB device
    call boot_message_usb
    # Here would be code to read boot sector from USB
    # and transfer control to it
    ret

no_bootable_devices:
    # No bootable devices found
    movl $0, %eax
    ret

no_bootable_device_error:
    # Display error message when no bootable devices found
    # This would display a message on screen
    movl $no_boot_message, %eax
    ret

boot_message_floppy:
    movl $floppy_boot_message, %eax
    ret
    
boot_message_scsi:
    movl $scsi_boot_message, %eax
    ret
    
boot_message_usb:
    movl $usb_boot_message, %eax
    ret

display_boot_devices:
    # Display list of bootable devices
    # This would show a menu on screen
    ret

display_bios_setup:
    # Display BIOS setup interface
    call clear_screen
    call draw_setup_frame
    call draw_setup_menu
    
    # Enter setup interaction loop
    call setup_interaction
    ret

setup_display_mode:
    # Set display mode appropriate for BIOS setup
    # Typically 80x25 text mode
    movl $80, display_width
    movl $25, display_height
    movl $1, text_mode_active
    ret
    
draw_setup_frame:
    # Draw frame for BIOS setup
    ret
    
draw_setup_menu:
    # Draw main menu for BIOS setup
    # Menu items:
    # - System Information
    # - Boot Options
    # - Advanced Settings
    # - Exit
    ret

setup_interaction:
    # Handle user interaction in setup menu
    # This would be a loop waiting for key presses
    ret

show_debug_info:
    # Show detailed hardware information in debug mode
    call clear_screen
    
    # Display CPU and memory info
    call display_cpu_info
    call display_memory_info
    
    # Display detected boot devices
    call display_boot_devices_debug
    ret
    
display_cpu_info:
    # Display CPU information for debug
    ret
    
display_memory_info:
    # Display memory information for debug
    ret
    
display_boot_devices_debug:
    # Display detailed boot device information
    ret
    
clear_screen:
    # Clear the display for fresh output
    ret

halt_loop:
    # ROM execution ends at halt loop
    # In real hardware, control would pass to OS
    jmp halt_loop        # Infinite loop

init_hardware:
    # Initialize essential hardware components
    movl $0x7FFF, %eax     # Value to disable all interrupts
    
    # Check debug flag to modify initialization
    cmpl $0x1, debug_flag
    je debug_hardware_init
    
    # Normal hardware init
    movl $0x00, %edx       # Standard init level
    jmp hardware_init_done
    
debug_hardware_init:
    # Debug hardware init (more verbose, fewer components)
    movl $0x01, %edx       # Debug init level

hardware_init_done:
    ret

detect_hardware:
    # Detect installed hardware components
    # CPU detection
    call detect_cpu
    # Memory detection
    call detect_memory
    # Peripheral detection
    call detect_peripherals
    ret

detect_cpu:
    # Simple CPU detection routine
    # Store CPU information in cpu_info data structure
    movl $0x1, cpu_info_valid
    movl $4, cpu_info_cores
    movl $66, cpu_info_mhz
    movl $0x486, cpu_info_type     # 486 CPU type
    movl $1, cpu_info_fpu_present  # FPU present
    movl $0, cpu_info_mmu_present  # No MMU
    ret

detect_memory:
    # Memory size detection
    # Store memory information in memory_info data structure
    movl $0x1, memory_info_valid
    movl $16384, memory_info_kb    # 16MB RAM
    ret

detect_peripherals:
    # Detect storage devices
    call detect_scsi_devices
    call detect_floppy_devices
    call detect_usb_devices
    ret

detect_scsi_devices:
    # Detect SCSI controllers and devices
    movl $1, scsi_controller_present
    movl $2, scsi_device_count
    
    # Device 0: SCSI HDD
    movl $0, %edi
    leal scsi_devices, %esi
    movl $1, 0(%esi)               # Device type: HDD
    movl $2048, 4(%esi)            # Size in MB
    movb $1, 8(%esi)               # Bootable
    
    # Device 1: SCSI CD-ROM
    movl $12, %edi
    leal scsi_devices+12, %esi
    movl $5, 0(%esi)               # Device type: CD-ROM
    movl $0, 4(%esi)               # Size not applicable
    movb $1, 8(%esi)               # Bootable
    ret

detect_floppy_devices:
    # Detect floppy drives
    movl $1, floppy_controller_present
    movl $2, floppy_drive_count
    
    # Drive 0: 3.5" 1.44MB
    movl $0, %edi
    leal floppy_drives, %esi
    movl $0, 0(%esi)               # Drive number: 0
    movl $1440, 4(%esi)            # Size in KB
    movb $1, 8(%esi)               # Bootable
    
    # Drive 1: 5.25" 1.2MB
    movl $12, %edi
    leal floppy_drives+12, %esi
    movl $1, 0(%esi)               # Drive number: 1
    movl $1200, 4(%esi)            # Size in KB
    movb $1, 8(%esi)               # Bootable
    ret

detect_usb_devices:
    # Detect USB controllers and devices
    movl $1, usb_controller_present
    movl $1, usb_device_count
    
    # USB flash drive
    movl $0, %edi
    leal usb_devices, %esi
    movl $1, 0(%esi)               # Device type: Flash drive
    movl $4096, 4(%esi)            # Size in MB
    movb $1, 8(%esi)               # Bootable
    ret

init_display:
    # Set up display parameters
    movl $0x1200, %eax     # BPLCON0 value: 1 bitplane, color
    movl $0x2c81, %ebx     # DIWSTRT value: display window start
    movl $0x2cc1, %ecx     # DIWSTOP value: display window stop
    
    # Check if in debug mode for different display settings
    cmpl $0x1, debug_flag
    jne normal_display
    
    # Debug mode uses higher contrast settings
    movl $0x3000, %eax     # Enhanced contrast
    
normal_display:
    ret

set_colors:
    # Set up color palette
    movl $0x0000, %eax     # COLOR00: Black background
    movl $0xFFFF, %ebx     # COLOR01: White foreground
    
    # Check if in debug mode for different colors
    cmpl $0x1, debug_flag
    jne normal_colors
    
    # Debug mode uses blue background for visibility
    movl $0x000F, %eax     # COLOR00: Deep blue background
    
normal_colors:
    ret

draw_logo:
    # Display the NeoBench logo
    movl $neobench_logo, %eax  # Logo bitmap pointer
    movl $320, %ebx            # Logo width
    movl $256, %ecx            # Logo height
    
    # If debug mode, skip detailed drawing
    cmpl $0x1, debug_flag
    je simple_logo
    
    # Normal logo drawing with all effects
    movl $1, %edx             # Full detail rendering
    ret
    
simple_logo:
    # Simplified logo for debug mode
    movl $0, %edx             # Simple rendering
    ret

play_sound:
    # Play the boot sound
    movl $boot_sound, %eax    # Sound data pointer
    movl $2000, %ebx          # Length in words
    movl $64, %ecx            # Volume (max)
    
    # Check if in debug mode - quiet boot
    cmpl $0x1, debug_flag
    jne normal_sound
    
    # Debug mode uses lower volume
    movl $16, %ecx            # Lower volume in debug mode
    
normal_sound:
    ret

.section .data
# Global variables
debug_flag:
    .long   0              # Debug mode flag: 0=normal, 1=debug
setup_key_pressed:
    .long   0              # Setup key (F2) pressed flag
boot_status:
    .long   0              # Boot status code
boot_key_check:
    .long   0              # Last key pressed during boot
display_width:
    .long   80             # Display width in characters/pixels
display_height:
    .long   25             # Display height in characters/pixels
text_mode_active:
    .long   0              # Text mode flag: 0=graphics, 1=text

# Hardware detection info
cpu_info_valid:
    .long   0              # CPU info valid flag
cpu_info_cores:
    .long   1              # Number of CPU cores
cpu_info_mhz:
    .long   0              # CPU speed in MHz
cpu_info_type:
    .long   0              # CPU type (386, 486, etc.)
cpu_info_fpu_present:
    .long   0              # FPU present flag
cpu_info_mmu_present:
    .long   0              # MMU present flag

memory_info_valid:
    .long   0              # Memory info valid flag
memory_info_kb:
    .long   0              # Memory size in KB

# Boot device information
scsi_controller_present:
    .long   0              # SCSI controller present flag
scsi_device_count:
    .long   0              # Number of SCSI devices
scsi_devices:
    .space  120            # Space for up to 10 SCSI devices (12 bytes each)

floppy_controller_present:
    .long   0              # Floppy controller present flag
floppy_drive_count:
    .long   0              # Number of floppy drives
floppy_drives:
    .space  48             # Space for up to 4 floppy drives (12 bytes each)

usb_controller_present:
    .long   0              # USB controller present flag
usb_device_count:
    .long   0              # Number of USB devices
usb_devices:
    .space  120            # Space for up to 10 USB devices (12 bytes each)

# Boot configuration
boot_priority:
    .long   0x00010200     # Boot priority: USB(2), SCSI(1), Floppy(0)
current_boot_priority:
    .long   0              # Current boot priority

# Bootable device list
bootable_device_count:
    .long   0              # Number of bootable devices found
bootable_devices:
    .space  40             # Device types (10 entries)
bootable_device_ids:
    .space  40             # Device IDs (10 entries)

# Messages for boot and debug modes
boot_message:
    .ascii "NeoBench OS v1.0 - Normal Boot\0"
    
debug_message:
    .ascii "NeoBench OS v1.0 - DEBUG MODE\0"
    
setup_message:
    .ascii "NeoBench OS v1.0 - BIOS Setup\0"

no_boot_message:
    .ascii "No bootable device found. Press F2 for setup.\0"

floppy_boot_message:
    .ascii "Booting from Floppy Drive...\0"
    
scsi_boot_message:
    .ascii "Booting from SCSI Device...\0"
    
usb_boot_message:
    .ascii "Booting from USB Device...\0"

# Hardware control data structures
copper_list:
    .word   0x0100, 0x1200   # BPLCON0: 1 bitplane
    .word   0x0102, 0x0000   # BPLCON1: no scroll  
    .word   0x0104, 0x0024   # BPLCON2: normal priority
    .word   0x008e, 0x2c81   # DIWSTRT: display window start
    .word   0x0090, 0x2cc1   # DIWSTOP: display window stop
    .word   0x0092, 0x0038   # DDFSTRT: data fetch start
    .word   0x0094, 0x00d0   # DDFSTOP: data fetch stop
    .word   0x0180, 0x0000   # COLOR00: black background
    .word   0x0182, 0x0fff   # COLOR01: white foreground
    .word   0xFFFF, 0xFFFE   # End copper list

# Debug mode copper list - uses different color scheme
debug_copper_list:
    .word   0x0100, 0x1200   # BPLCON0: 1 bitplane
    .word   0x0102, 0x0000   # BPLCON1: no scroll  
    .word   0x0104, 0x0024   # BPLCON2: normal priority
    .word   0x008e, 0x2c81   # DIWSTRT: display window start
    .word   0x0090, 0x2cc1   # DIWSTOP: display window stop
    .word   0x0092, 0x0038   # DDFSTRT: data fetch start
    .word   0x0094, 0x00d0   # DDFSTOP: data fetch stop
    .word   0x0180, 0x000F   # COLOR00: deep blue background
    .word   0x0182, 0x0FFF   # COLOR01: bright white foreground
    .word   0xFFFF, 0xFFFE   # End copper list

# ROM identification and version
rom_id:
    .ascii "NEOBENCH_ROM_v1.0\0"
    .align  4              # Align to 32-bit boundary

# System assets
    .align  4              # Align to 32-bit boundary
    .globl neobench_logo
neobench_logo:
    .incbin "build/neobench_logo.raw"

    .align  4              # Align to 32-bit boundary  
    .globl boot_sound
boot_sound:
    .incbin "build/boot_sound.raw"