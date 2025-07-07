#![no_std]
#![no_main]

extern crate neoboot;

use core::panic::PanicInfo;

#[no_mangle]
pub extern "C" fn _start() -> ! {
    unsafe {
        neoboot::boot();
    }
    loop {}
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
