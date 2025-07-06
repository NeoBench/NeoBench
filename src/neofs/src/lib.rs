// src/neofs/src/lib.rs

#![no_std]
#![no_main]

use core::panic::PanicInfo;

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

// Your public init symbol for linking:
#[no_mangle]
pub extern "C" fn neofs_init() {
    // ... your FS init code ...
}
