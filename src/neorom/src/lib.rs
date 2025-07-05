#![no_std]
#![no_main]

use core::panic::PanicInfo;
use core::ptr::write_volatile;

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

fn write_serial(byte: u8) {
    unsafe {
        // Wait for the serial port to be ready
        while (core::ptr::read_volatile((0x3F8 + 5) as *const u8) & 0x20) == 0 {}
        core::ptr::write_volatile(0x3F8 as *mut u8, byte);
    }
}

fn print(s: &str) {
    for byte in s.bytes() {
        write_serial(byte);
    }
}

#[no_mangle]
pub extern "C" fn rom_boot_check() -> ! {
    print("\n\n>>> NEOBENCH ROM BOOTING...\n");
    loop {}
}
