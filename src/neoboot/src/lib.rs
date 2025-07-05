#![no_std]
#![no_main]

use core::panic::PanicInfo;

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! { loop {} }

/// Called by the ROM entry
#[no_mangle]
pub extern "C" fn neoboot_main() {
    // e.g. serial_print("Bootloader running...\n");
    loop {}
}
