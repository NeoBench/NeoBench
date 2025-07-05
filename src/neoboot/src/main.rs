#![no_std]
#![no_main]

use neorom::rom_boot_check;

/// Panic handler for no_std
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}

/// Entry point: like BIOS _start
#[no_mangle]
pub extern "C" fn _start() -> ! {
    let result = rom_boot_check();

    // This is placeholder logic – you would replace with real ROM init
    if result {
        unsafe { *(0xB8000 as *mut u8) = b'Y'; }
    } else {
        unsafe { *(0xB8000 as *mut u8) = b'N'; }
    }

    loop {}
}
