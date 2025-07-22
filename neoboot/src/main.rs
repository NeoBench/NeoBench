#![no_std]
#![no_main]

use core::fmt::Write;
use core::ptr::read_unaligned;
use core::panic::PanicInfo;

const VGA_BUFFER: *mut u8 = 0xb8000 as *mut u8;

#[no_mangle]
pub extern "C" fn _start() -> ! {
    let mut writer = VGABuffer::new();
    writer.write_str("NeoBoot Kernel (Rust)\n").unwrap();

    let entries_ptr = 0x9000 as *const u8;
    let count = unsafe { *(0x8FFC as *const u16) };

    for i in 0..count {
        let entry = unsafe { entries_ptr.add(i as usize * 24) };

        let base_addr = unsafe { read_unaligned(entry as *const u64) };
        let length    = unsafe { read_unaligned(entry.add(8) as *const u64) };
        let mem_type  = unsafe { read_unaligned(entry.add(16) as *const u32) };

        writer.write_fmt(format_args!(
            "Region {:016x} - {:016x} [type {}]\n",
            base_addr,
            base_addr + length,
            mem_type
        )).unwrap();
    }

    loop {}
}

struct VGABuffer {
    ptr: *mut u8,
    col: usize,
}

impl VGABuffer {
    fn new() -> Self {
        Self { ptr: VGA_BUFFER, col: 0 }
    }
}

impl Write for VGABuffer {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        for b in s.bytes() {
            unsafe {
                *self.ptr.offset((self.col * 2) as isize) = b;
                *self.ptr.offset((self.col * 2 + 1) as isize) = 0x0F;
            }
            self.col += 1;
        }
        Ok(())
    }
}

#[panic_handler]
fn panic(_: &PanicInfo) -> ! {
    loop {}
}
