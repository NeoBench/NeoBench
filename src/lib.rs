#![no_std]
#![no_main]

#[no_mangle]
pub extern "C" fn _start() -> ! {
    unsafe {
        core::arch::asm!("stop #0x2700");
    }
    loop {}
}
