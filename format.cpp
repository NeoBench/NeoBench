/*
 * NeoBench Master Formatter
 * format.cpp - Integrated RDB/Boot/Kernel/FS Tool
 */

#include "include/neobench.h" 
#include "include/nbfs_types.h"

namespace neo {
namespace neofs {
    struct __attribute__((packed)) Superblock {
        uint32_t magic;
        uint32_t version;
        uint32_t block_size;
        uint32_t total_blocks;
        uint32_t free_blocks;
        uint32_t total_inodes;
        uint32_t free_inodes;
        uint32_t inode_bitmap_start;
        uint32_t inode_bitmap_blocks;
        uint32_t block_bitmap_start;
        uint32_t block_bitmap_blocks;
        uint32_t inode_table_start;
        uint32_t inode_table_blocks;
        uint32_t data_start;
        uint32_t journal_blocks;
        uint32_t journal_head;
        uint32_t journal_tail;
        uint32_t journal_txn_id;
        uint32_t mount_count;
        uint32_t max_mount_count;
        uint32_t state;
        uint32_t last_check_time;
        uint32_t last_mount_time;
        uint32_t last_write_time;
        uint32_t creator_os;
        uint32_t uuid_hi;
        uint32_t uuid_lo;
        uint32_t crc32;
        char     volume_name[32];
    };

    struct __attribute__((packed)) Inode {
        uint16_t  mode;
        uint16_t  uid;
        uint16_t  gid;
        uint16_t  link_count;
        uint32_t  flags;
        uint64_t  size;
        uint32_t  atime_sec;
        uint32_t  atime_nsec;
        uint32_t  mtime_sec;
        uint32_t  mtime_nsec;
        uint32_t  ctime_sec;
        uint32_t  ctime_nsec;
        uint32_t  crtime_sec;
        uint32_t  crtime_nsec;
        uint32_t  block_count;
        uint32_t  overflow_block;
        uint32_t  xattr_block;
        uint32_t  generation;
        uint32_t  crc32;
        union {
            uint8_t inline_data[256];
        } data;
    };
}
}

#include <iostream>
#include <string>
#include <fstream>
#include <cstring>
#include <vector>
#include <cstdint>

inline uint32_t ForceBE32(uint32_t val) {
    return ((val >> 24) & 0x000000FF) |
           ((val >>  8) & 0x0000FF00) |
           ((val <<  8) & 0x00FF0000) |
           ((val << 24) & 0xFF000000);
}

inline uint16_t ForceBE16(uint16_t val) {
    return ((val >> 8) & 0x00FF) | 
           ((val << 8) & 0xFF00);
}

inline uint64_t ForceBE64(uint64_t val) {
    return (((uint64_t)ForceBE32(val & 0xFFFFFFFF)) << 32) | 
           ForceBE32(val >> 32);
}

uint32_t ComputeCRC32C(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 1) crc = (crc >> 1) ^ 0x82F63B78UL;
            else crc >>= 1;
        }
    }
    return ~crc; 
}

uint32_t AmigaChecksum(const uint8_t* data, int bytes) {
    uint32_t chk = 0;
    for (int i = 0; i < bytes; i += 4) {
        uint32_t val = (data[i] << 24) | (data[i+1] << 16) | (data[i+2] << 8) | data[i+3];
        chk += val;
    }
    return (0 - chk);
}

bool WriteSectors(std::fstream& f, uint32_t sector, const void* data, int count) {
    f.seekp(static_cast<uint64_t>(sector) * 512);
    f.write(reinterpret_cast<const char*>(data), count * 512);
    return f.good();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: format [target_hdf] (kernel_bin) (bootblock_bin)\n";
        return 1;
    }

    std::string hdfPath = argv[1];
    std::fstream disk(hdfPath, std::ios::in | std::ios::out | std::ios::binary);
    if (!disk.is_open()) {
        std::cerr << "Cannot open " << hdfPath << "\n";
        return -1;
    }

    disk.seekg(0, std::ios::end);
    uint64_t disk_size = disk.tellg();
    uint32_t total_sectors = disk_size / 512;
    uint32_t cyls = total_sectors / (16 * 63);

    std::cout << "[*] Disk: " << hdfPath << " (" << total_sectors << " sectors, " << cyls << " cyls)\n";

    // 1. Build RDB (Sector 0)
    uint8_t rdb[512] = {0};
    std::memcpy(rdb, "RDSK", 4);
    *reinterpret_cast<uint32_t*>(&rdb[4]) = ForceBE32(64);
    *reinterpret_cast<uint32_t*>(&rdb[12]) = ForceBE32(7);
    *reinterpret_cast<uint32_t*>(&rdb[16]) = ForceBE32(512);
    *reinterpret_cast<uint32_t*>(&rdb[20]) = ForceBE32(2); // LastRDB
    *reinterpret_cast<uint32_t*>(&rdb[28]) = ForceBE32(1); // PART at 1
    *reinterpret_cast<uint32_t*>(&rdb[32]) = ForceBE32(cyls);
    *reinterpret_cast<uint32_t*>(&rdb[36]) = ForceBE32(63);
    *reinterpret_cast<uint32_t*>(&rdb[40]) = ForceBE32(16);
    *reinterpret_cast<uint32_t*>(&rdb[8]) = ForceBE32(AmigaChecksum(rdb, 512));
    WriteSectors(disk, 0, rdb, 1);

    // 2. Build PART (Sector 1)
    uint8_t part[512] = {0};
    std::memcpy(part, "PART", 4);
    *reinterpret_cast<uint32_t*>(&part[4]) = ForceBE32(64);
    *reinterpret_cast<uint32_t*>(&part[16]) = ForceBE32(0xFFFFFFFF);
    *reinterpret_cast<uint32_t*>(&part[20]) = ForceBE32(1); // Bootable
    part[36] = 3;
    std::memcpy(&part[37], "DH0", 3);
    *reinterpret_cast<uint32_t*>(&part[128]) = ForceBE32(11); // TableSize
    *reinterpret_cast<uint32_t*>(&part[132]) = ForceBE32(1);
    *reinterpret_cast<uint32_t*>(&part[140]) = ForceBE32(16);
    *reinterpret_cast<uint32_t*>(&part[144]) = ForceBE32(1);
    *reinterpret_cast<uint32_t*>(&part[148]) = ForceBE32(63);
    *reinterpret_cast<uint32_t*>(&part[164]) = ForceBE32(2); // LowCyl (2016)
    *reinterpret_cast<uint32_t*>(&part[168]) = ForceBE32(cyls - 1);
    *reinterpret_cast<uint32_t*>(&part[188]) = ForceBE32(127); // BootPri
    *reinterpret_cast<uint32_t*>(&part[192]) = ForceBE32(0x4E454F00); // NEO\0
    *reinterpret_cast<uint32_t*>(&part[8]) = ForceBE32(AmigaChecksum(part, 512));
    WriteSectors(disk, 1, part, 1);

    // 3. Inject Bootblock & Kernel if provided
    if (argc >= 4) {
        std::string kPath = argv[2];
        std::string bPath = argv[3];
        
        std::ifstream bf(bPath, std::ios::binary);
        uint8_t boot[1024] = {0};
        bf.read(reinterpret_cast<char*>(boot), 1024);
        *reinterpret_cast<uint32_t*>(&boot[4]) = 0; // Clear chk
        *reinterpret_cast<uint32_t*>(&boot[4]) = ForceBE32(AmigaChecksum(boot, 1024));
        WriteSectors(disk, 2016, boot, 2);

        std::ifstream kf(kPath, std::ios::binary);
        uint8_t kbuf[262144] = {0};
        kf.read(reinterpret_cast<char*>(kbuf), 262144);
        WriteSectors(disk, 2018, kbuf, 512); // Write 256KB
        std::cout << "[*] Bootloader and Kernel injected.\n";
    }

    // 4. Format NeoFS starting at Sector 8192 (4MB)
    uint32_t fs_sector_base = 8192;
    uint8_t sb_block[4096] = {0};
    neo::neofs::Superblock* sb = reinterpret_cast<neo::neofs::Superblock*>(sb_block);
    sb->magic = ForceBE32(0x4E454F46);
    sb->version = ForceBE32(0x00010000);
    sb->block_size = ForceBE32(4096);
    sb->total_blocks = ForceBE32((total_sectors - fs_sector_base) / 8);
    sb->data_start = ForceBE32(64); // Relative to fs_sector_base
    std::strncpy(sb->volume_name, "NEOBENCH", 31);
    sb->crc32 = ComputeCRC32C(sb_block, 4096 - 4);
    
    disk.seekp(static_cast<uint64_t>(fs_sector_base) * 512);
    disk.write(reinterpret_cast<char*>(sb_block), 4096);

    std::cout << "[SUCCESS] Full NeoFS HDF created.\n";
    return 0;
}
