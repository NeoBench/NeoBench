use core::mem::size_of;

/// Magic signature for identifying a NeoFS volume
pub const NEOFS_MAGIC: &[u8; 8] = b"NEOFS1.0";

/// The volume header block is always exactly 512 bytes
#[repr(C, packed)]
#[derive(Copy, Clone)]
pub struct NeoFsHeader {
    pub magic: [u8; 8],         // Always "NEOFS1.0"
    pub total_blocks: u32,      // Total number of usable blocks on the volume
    pub root_block: u32,        // Starting block number of the root directory
    pub bitmap_block: u32,      // Starting block of the block bitmap
    pub volume_name: [u8; 32],  // UTF-8 name, null-terminated if shorter
    pub reserved: [u8; 460],    // Padding to make header exactly 512 bytes
}

impl NeoFsHeader {
    /// Create a new NeoFsHeader with a volume name and total block count
    pub fn new(volume_name: &str, total_blocks: u32) -> Self {
        let mut name_bytes = [0u8; 32];
        let name = volume_name.as_bytes();
        let len = name.len().min(31);
        name_bytes[..len].copy_from_slice(&name[..len]);

        Self {
            magic: *NEOFS_MAGIC,
            total_blocks,
            root_block: 10,
            bitmap_block: 2,
            volume_name: name_bytes,
            reserved: [0u8; 460],
        }
    }

    /// Try to parse a NeoFsHeader from raw 512-byte data
    pub fn from_bytes(bytes: &[u8]) -> Option<Self> {
        if bytes.len() < size_of::<NeoFsHeader>() {
            return None;
        }

        let ptr = bytes.as_ptr() as *const NeoFsHeader;

        // SAFETY: We're reading from aligned raw memory.
        unsafe { Some(core::ptr::read_unaligned(ptr)) }
    }
}
