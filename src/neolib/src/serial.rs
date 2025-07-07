#![no_std]

// A simple serial interface for bare-metal systems

pub struct Serial;

impl Serial {
    /// Writes a single byte to the serial output.
    pub fn write_byte(byte: u8) {
        const SERIAL_PORT: *mut u8 = 0xDE00_0000 as *mut u8; // Replace with your actual I/O address

        unsafe {
            core::ptr::write_volatile(SERIAL_PORT, byte);
        }
    }

    /// Writes a full string to the serial output.
    pub fn write_str(s: &str) {
        for byte in s.bytes() {
            Self::write_byte(byte);
        }
    }
}

pub fn puts(s: &str) {
    for &b in s.as_bytes() {
        putchar(b);
    }
}

pub fn putchar(byte: u8) {
    unsafe { core::ptr::write_volatile(0xFFFF_FFF0 as *mut u8, byte); }
}
