#![no_std]

use neolib::serial::puts;

pub fn boot() -> ! {
    puts("NeoROM booting...\n");

    loop {}
}
