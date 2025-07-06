use core::mem::size_of;

/// Maximum filename length
pub const MAX_NAME_LEN: usize = 32;

/// File vs Directory
#[derive(Copy, Clone, PartialEq)]
pub enum EntryType {
    File,
    Directory,
}

#[derive(Copy, Clone)]
pub struct DirEntry {
    pub name: [u8; MAX_NAME_LEN],
    pub entry_type: EntryType,
    pub first_block: u32,
    pub size: u32,
}

impl DirEntry {
    pub fn empty() -> Self {
        Self {
            name: [0; MAX_NAME_LEN],
            entry_type: EntryType::File,
            first_block: 0,
            size: 0,
        }
    }

    pub fn name_as_str(&self) -> &str {
        let len = self
            .name
            .iter()
            .position(|&c| c == 0)
            .unwrap_or(MAX_NAME_LEN);
        core::str::from_utf8(&self.name[..len]).unwrap_or("")
    }
}

pub struct DirectoryBlock {
    pub entries: [DirEntry; 512 / size_of::<DirEntry>()],
}

impl DirectoryBlock {
    pub const ENTRY_COUNT: usize = 512 / size_of::<DirEntry>();

    pub fn new() -> Self {
        // we can use array::from_fn in Rust 1.63+
        let entries = core::array::from_fn(|_| DirEntry::empty());
        Self { entries }
    }

    pub fn add_entry(&mut self, entry: DirEntry) -> bool {
        for e in &mut self.entries {
            if e.name[0] == 0 {
                *e = entry;
                return true;
            }
        }
        false
    }
}
