#![no_std]
#![no_main]

use core::panic::PanicInfo;
use core::ptr::{read_volatile, write_volatile};  // ← pull in both

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

#[no_mangle]
pub extern "C" fn neoboot_main() {
    // Example serial write:
    let byte = b'N';
    unsafe {
        // wait until transmitter empty
        while read_volatile((0x3F8 + 5) as *const u8) & 0x20 == 0 {}
        write_volatile(0x3F8 as *mut u8, byte);
    }
    // return to ROM stub
}
