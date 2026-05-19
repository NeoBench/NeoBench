/*
 * NeoBench System Utility - cat.neofs
 * cat.neofs.cpp
 */

#include "include/neobench.h"
#include "fs/neofs.cpp" 
#include <iostream>
#include <string>
#include <fstream>
#include <cstring>
#include <vector>
#include <sstream>

inline uint32_t ParseBE32(uint32_t val) {
    return ((val >> 24) & 0x000000FF) |
           ((val >>  8) & 0x0000FF00) |
           ((val <<  8) & 0x00FF0000) |
           ((val << 24) & 0xFF000000);
}

inline uint16_t ParseBE16(uint16_t val) {
    return ((val >> 8) & 0x00FF) | 
           ((val << 8) & 0xFF00);
}

inline uint64_t ParseBE64(uint64_t val) {
    return (((uint64_t)ParseBE32(val & 0xFFFFFFFF)) << 32) | 
           ParseBE32(val >> 32);
}

bool ReadBlock(std::fstream& disk, uint32_t block_index, void* buffer) {
    disk.seekg(static_cast<uint64_t>(block_index) * 4096);
    disk.read(reinterpret_cast<char*>(buffer), 4096);
    return disk.good();
}

struct FoundEntry {
    uint32_t inode_num;
    uint8_t type_code;
    bool success = false;
};

FoundEntry FindNodeInDirectory(std::fstream& disk, uint32_t table_start, uint32_t dir_inode, const std::string& target_name) {
    alignas(4096) uint8_t block_buffer[4096];
    FoundEntry result;

    uint32_t global_idx = dir_inode - 1;
    uint32_t target_block = table_start + (global_idx / 16);
    uint32_t byte_offset = (global_idx % 16) * neo::neofs::INODE_SIZE;

    if (!ReadBlock(disk, target_block, block_buffer)) return result;

    neo::neofs::Inode* inode = reinterpret_cast<neo::neofs::Inode*>(&block_buffer[byte_offset]);
    uint64_t data_size = ParseBE64(inode->size);

    uint8_t* current_ptr = inode->data.inline_data;
    uint8_t* end_ptr = current_ptr + data_size;

    while (current_ptr < end_ptr) {
        uint32_t entry_ino;
        std::memcpy(&entry_ino, current_ptr, 4);
        entry_ino = ParseBE32(entry_ino);

        uint8_t type_code = *(current_ptr + 4);
        uint8_t name_len = *(current_ptr + 5);
        std::string entry_name(reinterpret_cast<char*>(current_ptr + 6), name_len);

        if (entry_name == target_name) {
            result.inode_num = entry_ino;
            result.type_code = type_code;
            result.success = true;
            return result;
        }
        current_ptr += 6 + name_len;
    }
    return result;
}

void DumpFileContents(std::fstream& disk, uint32_t table_start, uint32_t inode_num) {
    alignas(4096) uint8_t block_buffer[4096];
    
    uint32_t global_idx = inode_num - 1;
    uint32_t target_block = table_start + (global_idx / 16);
    uint32_t byte_offset = (global_idx % 16) * neo::neofs::INODE_SIZE;

    if (!ReadBlock(disk, target_block, block_buffer)) return;

    neo::neofs::Inode* inode = reinterpret_cast<neo::neofs::Inode*>(&block_buffer[byte_offset]);
    uint64_t file_size = ParseBE64(inode->size);
    uint32_t flags = ParseBE32(inode->flags);

    if (flags & 0x00000001) { // NFS_FL_INLINE check verification
        uint32_t safe_size = static_cast<uint32_t>(file_size);
        if (safe_size > 128) safe_size = 128; // Cap at structural inline boundary limits

        std::vector<char> text_buffer(safe_size + 1, 0);
        std::memcpy(text_buffer.data(), inode->data.inline_data, safe_size);
        std::cout << text_buffer.data();
    } else {
        std::cout << "[INFO] Target targets non-inline extents. Block mappings required.\n";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: cat.neofs [target_hdf_image] [file_path]\n";
        std::cout << "Example: cat.neofs test_drive.hdf s/Startup-sequence\n";
        return 1;
    }

    std::string hdfPath = argv[1];
    std::string filePath = argv[2];

    std::fstream disk(hdfPath, std::ios::in | std::ios::binary);
    if (!disk.is_open()) {
        std::cerr << "[ERROR] Cannot open image file.\n";
        return -1;
    }

    alignas(4096) uint8_t sb_buffer[4096];
    if (!ReadBlock(disk, 0, sb_buffer)) return -1;

    neo::neofs::Superblock* sb = reinterpret_cast<neo::neofs::Superblock*>(sb_buffer);
    if (ParseBE32(sb->magic) != 0x4E454F46) {
        std::cerr << "[CRITICAL] Invalid NeoFS geometry header.\n";
        return -1;
    }

    uint32_t table_start = ParseBE32(sb->inode_table_start);
    uint32_t current_inode = 1; 

    std::stringstream ss(filePath);
    std::string token;
    
    FoundEntry lookup;
    lookup.inode_num = 1;
    lookup.type_code = 4; 
    lookup.success = false;

    while (std::getline(ss, token, '/')) {
        if (token.empty()) continue;
        
        lookup = FindNodeInDirectory(disk, table_start, current_inode, token);
        if (!lookup.success) {
            std::cerr << "[ERROR] Missing target item: " << token << "\n";
            return -1;
        }
        current_inode = lookup.inode_num;
    }

    if (lookup.type_code == 4) {
        std::cerr << "[ERROR] Path points to a directory container object. Use ls.neofs instead.\n";
        return -1;
    }

    DumpFileContents(disk, table_start, current_inode);
    return 0;
}
