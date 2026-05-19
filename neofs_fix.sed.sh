s/alignas(4096) uint8_t sb_page;/alignas(4096) uint8_t sb_page[4096];/g
s/alignas(4096) uint8_t bitmap_page;/alignas(4096) uint8_t bitmap_page[4096];/g
s/alignas(4096) uint8_t block_bitmap_page;/alignas(4096) uint8_t block_bitmap_page[4096];/g
s/alignas(4096) uint8_t table_page;/alignas(4096) uint8_t table_page[4096];/g
s/alignas(4096) uint8_t verify_page;/alignas(4096) uint8_t verify_page[4096];/gs/std::strcpy(reinterpret_cast<char \*\>\(([^)]+)\), ([^;]+));/std::memcpy(\1, \2, std::strlen(\2));/gs/\(inode_offset.*\)256/\1INODE_SIZE/g
s/256/INODE_SIZE/g
s/ForceBE32(~crc)/~crc/g
0,/WriteBlockToDisk(disk, 1,/d
s/bitmap_page\[0\] = 0xFF;/std::memset(bitmap_page, 0xFF, 1);/g
s/bitmap_page\[1\] = 0xFC;/bitmap_page[1] = 0xFC; \/\/ retained/g
s/ComputeCRC32C(sb_page, 508)/ComputeCRC32C(sb_page, sizeof(sb_page) - 4)/g
s/std::strcpy(reinterpret_cast<char \*\>\(([^)]+)\), ([^;]+));/std::memcpy(\1, \2, std::strlen(\2));/g
s/\(\(.*\)\(offset\|inode\)\(.*\)\)256/\1INODE_SIZE/g

