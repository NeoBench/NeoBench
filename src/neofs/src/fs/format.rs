use crate::fs::header::NeoFsHeader;

/// Format a new volume (writes header, zeroes bitmap, etc)
pub fn format_volume(volume_name: &str) {
    let header = NeoFsHeader::new(volume_name, /* total_blocks= */ 1024);
    let bytes: &[u8] = unsafe {
        core::slice::from_raw_parts(
            &header as *const _ as *const u8,
            core::mem::size_of::<NeoFsHeader>(),
        )
    };
    // write `bytes` out to your device...
    let _ = bytes;
}
