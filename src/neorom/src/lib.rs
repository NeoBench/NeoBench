#![no_std]

#[no_mangle]
pub extern "C" fn boot() {
    let msg = b"NEOROM!";
    let mut screen = 0x00200000 as *mut u8;

    for &ch in msg {
        unsafe {
            core::ptr::write_volatile(screen, ch);
            screen = screen.offset(2); // Planar gap
        }
    }
}
