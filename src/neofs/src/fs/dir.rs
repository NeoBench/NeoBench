use core::mem::size_of;

pub const MAX_NAME_LEN: usize = 32;

#[repr(u8)]
#[derive(Copy, Clone, PartialEq)]
pub enum EntryType {
    File = 0x01,
    Directory = 0x02,
}

#[repr(C, packed)]
#[derive(Copy, Clone)]
pub struct DirEntry {
    pub name: [u8; MAX_NAME_LEN],
    pub entry_type: EntryType,
    pub first_block: u32,
    pub size_bytes: u32,
}

impl DirEntry {
    pub fn empty() -> Self {
        Self {
            name: [0u8; MAX_NAME_LEN],
            entry_type: EntryType::File,
            first_block: 0,
            size_bytes: 0,
        }
    }

    pub fn name_as_str(&self) -> &str {
        let len = self.name.iter().position(|&c| c == 0).unwrap_or(MAX_NAME_LEN);
        core::str::from_utf8(&self.name[..len]).unwrap_or("INVALID")
    }
}

#[repr(C, packed)]
#[derive(Copy, Clone)]
pub struct DirectoryBlock {
    pub entries: [DirEntry; DirectoryBlock::ENTRY_COUNT],
}

impl DirectoryBlock {
    pub const ENTRY_COUNT: usize = 512 / size_of::<DirEntry>();

    pub fn new() -> Self {
        Self {
            entries: [DirEntry::empty(); Self::ENTRY_COUNT],
        }
    }

    pub fn add_entry(&mut self, entry: DirEntry) -> bool {
        for e in self.entries.iter_mut() {
            if e.first_block == 0 {
                *e = entry;
                return true;
            }
        }
        false
    }
}
