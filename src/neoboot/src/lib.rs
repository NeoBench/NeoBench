#![no_std]

#[no_mangle]
pub extern "C" fn boot() {
    let msg = b"NeoROM booting...\n";
    for &b in msg {
        unsafe {
            core::ptr::write_volatile(0xDFF180 as *mut u8, b); // Serial output
        }
    }
}
