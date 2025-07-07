#![no_std]
#![no_main]

extern crate neoboot;

#[no_mangle]
pub extern "C" fn _start() -> ! {
    neoboot::boot();
    loop {}
}

use core::panic::PanicInfo;

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
