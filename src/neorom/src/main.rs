#![no_std]
#![no_main]

use neoboot::boot;
use core::panic::PanicInfo;

#[no_mangle]
pub extern "C" fn _start() -> ! {
    boot();
    loop {} // Prevent returning
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
