#![no_std]

use neolib::serial::puts;

pub fn boot() {
    puts("Hello from NeoROM!\n");
}

