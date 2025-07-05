pub const BLOCK_SIZE: usize = 512;

/// A NeoFS disk block (fixed size)
pub struct Block {
    pub data: [u8; BLOCK_SIZE],
}

impl Block {
    pub fn new() -> Self {
        Self {
            data: [0; BLOCK_SIZE],
        }
    }

    pub fn from_slice(slice: &[u8]) -> Self {
        let mut block = Block::new();
        block.data.copy_from_slice(&slice[..BLOCK_SIZE]);
        block
    }

    pub fn as_bytes(&self) -> &[u8] {
        &self.data
    }

    pub fn as_mut_bytes(&mut self) -> &mut [u8] {
        &mut self.data
    }
}
