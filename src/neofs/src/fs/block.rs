#![allow(dead_code)]

/// One 512-byte block.
pub struct Block {
    data: [u8; 512],
}

impl Block {
    pub fn new() -> Self {
        Block { data: [0; 512] }
    }

    pub fn from_slice(slice: &[u8]) -> Self {
        let mut blk = Block::new();
        blk.data[..slice.len()].copy_from_slice(slice);
        blk
    }

    pub fn as_bytes(&self) -> &[u8] {
        &self.data
    }

    pub fn as_mut_bytes(&mut self) -> &mut [u8] {
        &mut self.data
    }
}
