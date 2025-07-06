#![no_std]
#![no_main]

use core::panic::PanicInfo;

/// Your entry‐point for the splash screen
#[no_mangle]
pub extern "C" fn neosplash_main() -> ! {
    // e.g. write_serial or VGA etc…
    loop {}
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! { loop {} }
