#![no_std]
#![no_main]

use core::panic::PanicInfo;
use core::arch::asm;

/// Panic handler: loops forever if something goes wrong
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

/// Read a byte from an I/O port
#[inline(always)]
fn inb(port: u16) -> u8 {
    let value: u8;
    unsafe {
        asm!("in al, dx", out("al") value, in("dx") port);
    }
    value
}

/// Write a byte to an I/O port
#[inline(always)]
fn outb(port: u16, value: u8) {
    unsafe {
        asm!("out dx, al", in("dx") port, in("al") value);
    }
}

/// Write a byte to COM1 (port 0x3F8)
fn write_serial(byte: u8) {
    // Poll the Line Status Register at 0x3FD until bit 5 (THR empty) is set
    while inb(0x3FD) & 0x20 == 0 {}
    outb(0x3F8, byte);
}

/// Send a string over serial
fn print(s: &str) {
    for &b in s.as_bytes() {
        write_serial(b);
    }
}

// External chain‐load entrypoints from the other crates
extern "C" {
    fn neoboot_main();
    fn neofs_init();
}

/// MBR entry point at 0x0000 in the flat binary
#[no_mangle]
pub extern "C" fn mbr_entry() -> ! {
    // Greeting
    print("\n\n>>> NEOBENCH MBR BOOTING...\n");

    // Chain into the next stages
    unsafe {
        neoboot_main();
        neofs_init();
    }

    // Spin forever
    loop {}
}
