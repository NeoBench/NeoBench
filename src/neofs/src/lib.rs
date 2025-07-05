#![no_std]

pub mod fs;
use fs::header::NeoFsHeader;

pub fn rom_boot_check() -> bool {
    let data = [0u8; 512]; // Simulated ROM data

    match NeoFsHeader::from_bytes(&data) {
        Some(header) => header.magic == *b"NEOFS1.0",
        None => false,
    }
}

