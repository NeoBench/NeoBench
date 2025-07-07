#![no_std]
#![no_main]

extern crate neoboot;

#[no_mangle]
pub extern "C" fn _start() -> ! {
    neoboot::boot();
    loop {}
}

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
