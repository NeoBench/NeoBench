// src/neofs/src/fs/header.rs

use core::mem::size_of;
use core::option::Option::{self, Some, None};
use core::slice::Iter;
use core::iter::Iterator;

pub const NEOFS_MAGIC: &[u8; 8] = b"NEOFS1.0";

#[repr(C, packed)]
#[derive(Copy, Clone, Debug)]
pub struct NeoFsHeader {
    /* ... */
}

impl NeoFsHeader {
    pub fn from_bytes(buf: &[u8]) -> Option<Self> {
        if buf.len() != size_of::<NeoFsHeader>() {
            return None;
        }
        // SAFETY: we checked length
        let hdr = unsafe { *(buf.as_ptr() as *const NeoFsHeader) };
        if &hdr.magic == NEOFS_MAGIC {
            Some(hdr)
        } else {
            None
        }
    }
}
