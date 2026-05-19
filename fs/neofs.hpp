namespace neo {
namespace neofs {

struct Inode {
    uint16_t mode;
    uint16_t link_count;
    uint32_t flags;
    uint64_t size;
    uint32_t crc32;

    union {
        uint8_t inline_data[256];
    } data;

} __attribute__((packed));

}
}
