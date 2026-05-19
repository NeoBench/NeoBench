/*
 * NeoBench System Utility - ls.neofs (Path Parsing Edition)
 * ls.neofs.cpp
 */

#include "include/neobench.h"
#include "fs/neofs.cpp" 
#include <iostream>
#include <string>
#include <fstream>
#include <cstring>
#include <vector>
#include <iomanip>
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

// Structure to cache matching values extracted from an O(n) inline pass
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

void PrintDirectory(std::fstream& disk, uint32_t table_start, uint32_t inode_num, const std::string& display_name) {
    alignas(4096) uint8_t block_buffer[4096];
    
    uint32_t global_idx = inode_num - 1;
    uint32_t target_block = table_start + (global_idx / 16);
    uint32_t byte_offset = (global_idx % 16) * neo::neofs::INODE_SIZE;

    if (!ReadBlock(disk, target_block, block_buffer)) return;

    neo::neofs::Inode* inode = reinterpret_cast<neo::neofs::Inode*>(&block_buffer[byte_offset]);
    uint64_t data_size = ParseBE64(inode->size);

    std::cout << "\nDirectory Listing: [ " << display_name << " ] (" << data_size << " bytes)\n";
    std::cout << "-------------------------------------------------------------\n";
    std::cout << std::left << std::setw(12) << "Inode" << std::setw(10) << "Type" << "Name\n";
    std::cout << "-------------------------------------------------------------\n";

    uint8_t* current_ptr = inode->data.inline_data;
    uint8_t* end_ptr = current_ptr + data_size;

    while (current_ptr < end_ptr) {
        uint32_t entry_ino;
        std::memcpy(&entry_ino, current_ptr, 4);
        entry_ino = ParseBE32(entry_ino);

        uint8_t type_code = *(current_ptr + 4);
        uint8_t name_len = *(current_ptr + 5);
        std::string entry_name(reinterpret_cast<char*>(current_ptr + 6), name_len);

        std::string type_str = (type_code == 4) ? "DIR" : "FILE";
        std::cout << std::left << std::setw(12) << entry_ino << std::setw(10) << type_str << entry_name << "\n";

        current_ptr += 6 + name_len;
    }
    std::cout << "-------------------------------------------------------------\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: ls.neofs [target_hdf_image] (sub_path)\n";
        return 1;
    }

    std::string hdfPath = argv[1];
    std::string subPath = (argc >= 3) ? argv[2] : "";

    std::fstream disk(hdfPath, std::ios::in | std::ios::binary);
    if (!disk.is_open()) {
        std::cerr << "[ERROR] Cannot open image file.\n";
        return -1;
    }

    alignas(4096) uint8_t sb_buffer[4096];
    if (!ReadBlock(disk, 0, sb_buffer)) return -1;

    neo::neofs::Superblock* sb = reinterpret_cast<neo::neofs::Superblock*>(sb_buffer);
    if (ParseBE32(sb->magic) != 0x4E454F46) {
        std::cerr << "[CRITICAL] Invalid NeoFS layout.\n";
        return -1;
    }

    uint32_t table_start = ParseBE32(sb->inode_table_start);
    std::cout << "[NeoFS VFS] Mounted Volume: " << sb->volume_name << "\n";

    uint32_t current_inode = 1; // Start at root directory

    if (!subPath.empty()) {
        std::stringstream ss(subPath);
        std::string token;
        
        while (std::getline(ss, token, '/')) {
            if (token.empty()) continue;
            
            FoundEntry lookup = FindNodeInDirectory(disk, table_start, current_inode, token);
            if (!lookup.success) {
                std::cerr << "[ERROR] Path component not found: " << token << "\n";
                return -1;
            }
            
            if (lookup.type_code != 4 && ss.rdbuf()->in_avail() > 0) {
                std::cerr << "[ERROR] Path element is a regular file, cannot traverse: " << token << "\n";
                return -1;
            }
            
            current_inode = lookup.inode_num;
        }
    }

    PrintDirectory(disk, table_start, current_inode, subPath.empty() ? "/" : subPath);
    return 0;
}
