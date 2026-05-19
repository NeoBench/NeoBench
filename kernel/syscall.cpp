/*
 * NeoBench Bare-Metal Amiga Kernel
 * System Call Dispatcher
 *
 * TRAP #0 based syscall interface.
 * Syscall number in D0, arguments in D1-D4, return value in D0.
 */

#include "../include/neobench.h"
#include "../include/types.h"

namespace neo {
namespace syscall {

/* System call numbers */
static constexpr uint32 SYS_EXIT    = 0;
static constexpr uint32 SYS_WRITE   = 1;
static constexpr uint32 SYS_READ    = 2;
static constexpr uint32 SYS_OPEN    = 3;
static constexpr uint32 SYS_CLOSE   = 4;
static constexpr uint32 SYS_ALLOC   = 5;
static constexpr uint32 SYS_FREE    = 6;
static constexpr uint32 SYS_FORK    = 7;
static constexpr uint32 SYS_EXEC    = 8;
static constexpr uint32 SYS_GETPID  = 9;
static constexpr uint32 SYS_SLEEP   = 10;
static constexpr uint32 SYS_YIELD   = 11;
static constexpr uint32 SYS_MAX     = 12;

/* External kernel subsystems */
namespace proc {
    extern void  exit(int32 code);
    extern void  yield();
    extern void  sleep(uint32 ticks);
    extern int32 getpid();
    extern int32 create(uint32 entry, const char* name, uint32 prio);
}

namespace mem {
    extern void* alloc(uint32 size);
    extern void  free(void* ptr);
}

/* Forward declaration for console I/O */
namespace console {
    extern int32 write(int32 fd, const void* buf, uint32 count);
    extern int32 read(int32 fd, void* buf, uint32 count);
}

/* Forward declaration for file I/O */
namespace fs {
    extern int32 open(const char* path, uint32 flags);
    extern int32 close(int32 fd);
}

/* ------------------------------------------------------------------ */
/*  Syscall handlers                                                   */
/* ------------------------------------------------------------------ */

static uint32 handle_exit(uint32 d1, uint32 /*d2*/, uint32 /*d3*/, uint32 /*d4*/)
{
    proc::exit(static_cast<int32>(d1));
    return 0;  /* Never reached */
}

static uint32 handle_write(uint32 d1, uint32 d2, uint32 d3, uint32 /*d4*/)
{
    int32 fd       = static_cast<int32>(d1);
    const void* buf = reinterpret_cast<const void*>(d2);
    uint32 count   = d3;
    return static_cast<uint32>(console::write(fd, buf, count));
}

static uint32 handle_read(uint32 d1, uint32 d2, uint32 d3, uint32 /*d4*/)
{
    int32 fd   = static_cast<int32>(d1);
    void* buf  = reinterpret_cast<void*>(d2);
    uint32 count = d3;
    return static_cast<uint32>(console::read(fd, buf, count));
}

static uint32 handle_open(uint32 d1, uint32 d2, uint32 /*d3*/, uint32 /*d4*/)
{
    const char* path = reinterpret_cast<const char*>(d1);
    uint32 flags     = d2;
    return static_cast<uint32>(fs::open(path, flags));
}

static uint32 handle_close(uint32 d1, uint32 /*d2*/, uint32 /*d3*/, uint32 /*d4*/)
{
    return static_cast<uint32>(fs::close(static_cast<int32>(d1)));
}

static uint32 handle_alloc(uint32 d1, uint32 /*d2*/, uint32 /*d3*/, uint32 /*d4*/)
{
    void* ptr = mem::alloc(d1);
    return reinterpret_cast<uint32>(ptr);
}

static uint32 handle_free(uint32 d1, uint32 /*d2*/, uint32 /*d3*/, uint32 /*d4*/)
{
    mem::free(reinterpret_cast<void*>(d1));
    return 0;
}

static uint32 handle_fork(uint32 /*d1*/, uint32 /*d2*/, uint32 /*d3*/, uint32 /*d4*/)
{
    /* Simplified fork: create a copy of current process */
    /* In this bare-metal kernel, fork creates a new process at same entry */
    return static_cast<uint32>(-1);  /* Not fully implemented */
}

static uint32 handle_exec(uint32 d1, uint32 d2, uint32 /*d3*/, uint32 /*d4*/)
{
    uint32 entry = d1;
    const char* name = reinterpret_cast<const char*>(d2);
    return static_cast<uint32>(proc::create(entry, name, 128));
}

static uint32 handle_getpid(uint32 /*d1*/, uint32 /*d2*/, uint32 /*d3*/, uint32 /*d4*/)
{
    return static_cast<uint32>(proc::getpid());
}

static uint32 handle_sleep(uint32 d1, uint32 /*d2*/, uint32 /*d3*/, uint32 /*d4*/)
{
    proc::sleep(d1);
    return 0;
}

static uint32 handle_yield(uint32 /*d1*/, uint32 /*d2*/, uint32 /*d3*/, uint32 /*d4*/)
{
    proc::yield();
    return 0;
}

/* Syscall dispatch table */
typedef uint32 (*SyscallFunc)(uint32, uint32, uint32, uint32);

static const SyscallFunc syscall_table[SYS_MAX] = {
    handle_exit,     /* 0: SYS_EXIT */
    handle_write,    /* 1: SYS_WRITE */
    handle_read,     /* 2: SYS_READ */
    handle_open,     /* 3: SYS_OPEN */
    handle_close,    /* 4: SYS_CLOSE */
    handle_alloc,    /* 5: SYS_ALLOC */
    handle_free,     /* 6: SYS_FREE */
    handle_fork,     /* 7: SYS_FORK */
    handle_exec,     /* 8: SYS_EXEC */
    handle_getpid,   /* 9: SYS_GETPID */
    handle_sleep,    /* 10: SYS_SLEEP */
    handle_yield,    /* 11: SYS_YIELD */
};

}  /* namespace syscall */
}  /* namespace neo */

/* ------------------------------------------------------------------ */
/*  C-linkage entry points (called from vectors.S TRAP handler)        */
/* ------------------------------------------------------------------ */

extern "C" {

/*
 * Main syscall dispatcher - called from TRAP #0 handler in vectors.S.
 * D0 = syscall number, D1-D4 = arguments.
 * Returns result in D0.
 */
uint32 _syscall_dispatch(uint32 num, uint32 d1, uint32 d2, uint32 d3, uint32 d4)
{
    if (num >= neo::syscall::SYS_MAX) {
        return static_cast<uint32>(-1);  /* Invalid syscall */
    }

    return neo::syscall::syscall_table[num](d1, d2, d3, d4);
}

/*
 * Category-specific dispatchers for organized handling.
 * These can be called directly or routed through _syscall_dispatch.
 */

uint32 _syscall_console(uint32 num, uint32 d1, uint32 d2, uint32 d3, uint32 d4)
{
    switch (num) {
        case neo::syscall::SYS_WRITE:
            return neo::syscall::syscall_table[neo::syscall::SYS_WRITE](d1, d2, d3, d4);
        case neo::syscall::SYS_READ:
            return neo::syscall::syscall_table[neo::syscall::SYS_READ](d1, d2, d3, d4);
        default:
            return static_cast<uint32>(-1);
    }
}

uint32 _syscall_memory(uint32 num, uint32 d1, uint32 d2, uint32 d3, uint32 d4)
{
    switch (num) {
        case neo::syscall::SYS_ALLOC:
            return neo::syscall::syscall_table[neo::syscall::SYS_ALLOC](d1, d2, d3, d4);
        case neo::syscall::SYS_FREE:
            return neo::syscall::syscall_table[neo::syscall::SYS_FREE](d1, d2, d3, d4);
        default:
            return static_cast<uint32>(-1);
    }
}

uint32 _syscall_process(uint32 num, uint32 d1, uint32 d2, uint32 d3, uint32 d4)
{
    switch (num) {
        case neo::syscall::SYS_EXIT:
            return neo::syscall::syscall_table[neo::syscall::SYS_EXIT](d1, d2, d3, d4);
        case neo::syscall::SYS_FORK:
            return neo::syscall::syscall_table[neo::syscall::SYS_FORK](d1, d2, d3, d4);
        case neo::syscall::SYS_EXEC:
            return neo::syscall::syscall_table[neo::syscall::SYS_EXEC](d1, d2, d3, d4);
        case neo::syscall::SYS_GETPID:
            return neo::syscall::syscall_table[neo::syscall::SYS_GETPID](d1, d2, d3, d4);
        case neo::syscall::SYS_SLEEP:
            return neo::syscall::syscall_table[neo::syscall::SYS_SLEEP](d1, d2, d3, d4);
        case neo::syscall::SYS_YIELD:
            return neo::syscall::syscall_table[neo::syscall::SYS_YIELD](d1, d2, d3, d4);
        default:
            return static_cast<uint32>(-1);
    }
}

uint32 _syscall_filesystem(uint32 num, uint32 d1, uint32 d2, uint32 d3, uint32 d4)
{
    switch (num) {
        case neo::syscall::SYS_OPEN:
            return neo::syscall::syscall_table[neo::syscall::SYS_OPEN](d1, d2, d3, d4);
        case neo::syscall::SYS_CLOSE:
            return neo::syscall::syscall_table[neo::syscall::SYS_CLOSE](d1, d2, d3, d4);
        default:
            return static_cast<uint32>(-1);
    }
}

uint32 _syscall_device(uint32 /*num*/, uint32 /*d1*/, uint32 /*d2*/, uint32 /*d3*/, uint32 /*d4*/)
{
    /* Device syscalls not yet implemented */
    return static_cast<uint32>(-1);
}

uint32 _syscall_network(uint32 /*num*/, uint32 /*d1*/, uint32 /*d2*/, uint32 /*d3*/, uint32 /*d4*/)
{
    /* Network syscalls not yet implemented */
    return static_cast<uint32>(-1);
}

uint32 _syscall_debug(uint32 /*num*/, uint32 d1, uint32 /*d2*/, uint32 /*d3*/, uint32 /*d4*/)
{
    /* Debug syscall: write D1 to debug port (serial) */
    volatile uint16* serdat = reinterpret_cast<volatile uint16*>(0xDFF030);
    *serdat = static_cast<uint16>(d1 & 0xFF) | 0x100;  /* 8N1: data + stop bit */
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Exception handlers                                                 */
/* ------------------------------------------------------------------ */

void _kernel_exception_handler(uint32 vector, uint32 pc, uint32 sr)
{
    (void)vector;
    (void)pc;
    (void)sr;

    /*
     * Generic exception handler.
     * For now, halt the system on unrecoverable exceptions.
     * Future: dump registers, attempt recovery.
     */

    /* Flash screen red as visual indicator */
    volatile uint16* color0 = reinterpret_cast<volatile uint16*>(0xDFF180);
    *color0 = 0x0F00;  /* Red */

    /* Halt */
    __asm__ volatile (
        "or.w  #0x0700, %%sr\n\t"  /* Disable all interrupts */
        "1: bra.s 1b\n\t"           /* Infinite loop */
        : : : "cc"
    );
}

void _kernel_fpu_exception_handler(uint32 vector, uint32 pc, uint32 sr)
{
    (void)vector;
    (void)pc;
    (void)sr;

    /* FPU exception: flash yellow */
    volatile uint16* color0 = reinterpret_cast<volatile uint16*>(0xDFF180);
    *color0 = 0x0FF0;  /* Yellow */

    /* Mark current process as using FPU, then try to continue */
    neo::proc::set_fpu_used();

    /* For BSUN/OPERR/etc., we'd need to examine the FPU state.
     * For now, halt on FPU exceptions. */
    __asm__ volatile (
        "or.w  #0x0700, %%sr\n\t"
        "1: bra.s 1b\n\t"
        : : : "cc"
    );
}

void _kernel_mmu_exception_handler(uint32 vector, uint32 pc, uint32 sr)
{
    (void)vector;
    (void)pc;
    (void)sr;

    /* MMU/Bus error: flash blue */
    volatile uint16* color0 = reinterpret_cast<volatile uint16*>(0xDFF180);
    *color0 = 0x000F;  /* Blue */

    /* Bus error / Address error recovery would require examining
     * the exception stack frame which differs between 030/040/060.
     * For now, halt. */
    __asm__ volatile (
        "or.w  #0x0700, %%sr\n\t"
        "1: bra.s 1b\n\t"
        : : : "cc"
    );
}

}  /* extern "C" */
