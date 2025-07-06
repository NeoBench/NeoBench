#![no_std]
#![no_main]

use core::panic::PanicInfo;

// Link in the other stages:
extern "C" {
    fn neoboot_main()  -> ();
    fn neofs_init()    -> ();
    fn neosplash_main()-> !;
}

// If we ever panic, just hang
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

// This is the ROM’s reset vector entry point.
// QEMU (or real hardware) jumps here on reset.
#[no_mangle]
pub extern "C" fn stage2_entry() -> ! {
 unsafe {
      neoboot_main();    // stage 1
      neofs_init();      // stage 2
      neosplash_main();  // finally diverges into your splash
 }
}
