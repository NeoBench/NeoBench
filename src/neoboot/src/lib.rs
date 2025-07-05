#![no_std]

use core::ptr::{read_volatile, write_volatile};

const COM1: u16 = 0x3F8;

pub fn boot_sequence() {
    unsafe {
        write_volatile(COM1 as *mut u8, b'N');
        write_volatile(COM1 as *mut u8, b'E');
        write_volatile(COM1 as *mut u8, b'O');
        write_volatile(COM1 as *mut u8, b'B');
        write_volatile(COM1 as *mut u8, b'E');
        write_volatile(COM1 as *mut u8, b'N');
        write_volatile(COM1 as *mut u8, b'C');
        write_volatile(COM1 as *mut u8, b'H');
        write_volatile(COM1 as *mut u8, b'\n');
    }
}
