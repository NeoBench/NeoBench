use crate::fs::block::{BLOCK_SIZE};
use crate::fs::dir::{DirectoryBlock, DirEntry, EntryType};
use crate::fs::header::NeoFsHeader;

#[link_section = ".volume"]
#[no_mangle]
pub static mut VOLUME: [u8; BLOCK_SIZE * 1024] = [0u8; BLOCK_SIZE * 1024];

pub fn format_volume(volume_name: &str) {
    unsafe {
        // --- Block 0: Write Header
        let header = NeoFsHeader::new(volume_name, 1024);
        let header_bytes = core::slice::from_raw_parts(
            &header as *const _ as *const u8,
            core::mem::size_of::<NeoFsHeader>(),
        );
        VOLUME[0..header_bytes.len()].copy_from_slice(header_bytes);

        // --- Block 1: Write Root Directory
        let mut root = DirectoryBlock::new();

        let mut system = DirEntry::empty();
        let name = b"System";
        system.name[..name.len()].copy_from_slice(name);
        system.entry_type = EntryType::Directory;
        system.first_block = 10;
        system.size_bytes = 0;

        root.add_entry(system);

        let root_bytes = core::slice::from_raw_parts(
            &root as *const _ as *const u8,
            core::mem::size_of::<DirectoryBlock>(),
        );

        let offset = BLOCK_SIZE; // block 1
        VOLUME[offset..offset + root_bytes.len()].copy_from_slice(root_bytes);
    }
}
