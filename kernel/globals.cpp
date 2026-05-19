#include "include/types.h"
#include "include/neobench.h"

extern "C" {
    /* MMU globals used by mmu.cpp */
    volatile neo::uint32 _g_chip_top = 0x200000;
    volatile neo::uint32 _g_fast_base = 0x08000000;
    volatile neo::uint32 _g_fast_size = 0;
    
    neo::uint32 _kernel_end = 0;

    /* Drivers / Subsystems Stubs */
    void _kernel_exception_handler(neo::uint32 vector, neo::uint32 pc, neo::uint32 sr) {}
    void _kernel_fpu_exception_handler(neo::uint32 vector, neo::uint32 pc, neo::uint32 sr) {}
    void _kernel_mmu_exception_handler(neo::uint32 vector, neo::uint32 pc, neo::uint32 sr) {}
    
    uint32_t _syscall_dispatch(uint32_t num, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4) { return 0; }
    uint32_t _syscall_console(uint32_t num, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4) { return 0; }
    uint32_t _syscall_memory(uint32_t num, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4) { return 0; }
    uint32_t _syscall_process(uint32_t num, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4) { return 0; }
    uint32_t _syscall_filesystem(uint32_t num, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4) { return 0; }
    uint32_t _syscall_device(uint32_t num, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4) { return 0; }
    uint32_t _syscall_network(uint32_t num, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4) { return 0; }
    uint32_t _syscall_debug(uint32_t num, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4) { return 0; }

    void _int_level1() {}
    void _int_level2() {}
    void _int_level3() {}
    void _int_level4() {}
    void _int_level5() {}
    void _int_level6() {}
    void _int_nmi() {}

    /* Placeholder for display symbols if needed */
    void _ZN3neo7display6set_fgEh(uint8_t) {}
    int kprintf(const char *fmt, ...) { return 0; }
    int ksprintf(char *buf, uint32 buf_size, const char *fmt, ...) { return 0; }
}
