#![no_std]
#![no_main]

use core::panic::PanicInfo;
use core::ptr::{read_volatile, write_volatile};

/// Panic handler: loops forever
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

/// Write a byte to COM1 (0x3F8)
fn write_serial(byte: u8) {
    unsafe {
        // Wait until the transmit buffer is empty (bit 5 of Line Status Register at 0x3FD)
        while read_volatile((0x3F8 + 5) as *const u8) & 0x20 == 0 {}
        // Send the byte to the transmit register at 0x3F8
        write_volatile(0x3F8 as *mut u8, byte);
    }
}

/// Print a string to COM1
fn print(s: &str) {
    for byte in s.bytes() {
        write_serial(byte);
    }
}

/// Entry point for the ROM, marked so the linker can find it
#[no_mangle]
pub extern "C" fn rom_boot_check() -> ! {
    print("\n\n>>> NEOBENCH ROM BOOTING...\n");
    loop {}
}
