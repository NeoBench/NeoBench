#![no_std]
#![no_main]

/// Very simple VGA-text splash: writes a centered message
/// into the 80×25 text buffer at 0xb8000.
#[no_mangle]
pub extern "C" fn neosplash_main() -> ! {
    const VGA_BUF: *mut u8 = 0xb8000 as *mut u8;
    const WIDTH: usize = 80;
    let splash = b"=== Welcome to NeoBench ===";
    let row = 12;
    let col = (WIDTH - splash.len()) / 2;
    for (i, &ch) in splash.iter().enumerate() {
        unsafe {
            let cell = VGA_BUF.add((row * WIDTH + col + i) * 2);
            *cell = ch;
            *cell.add(1) = 0x1f; // white on blue
        }
    }
    loop {}
}
