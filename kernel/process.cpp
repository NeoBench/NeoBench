/*
 * NeoBench Bare-Metal Amiga Kernel
 * Cooperative Multitasking Process Manager
 *
 * Round-robin scheduler with FPU context support.
 * Context switch saves D0-D7, A0-A6, SR, PC, USP.
 */

#include "../include/neobench.h"
#include "../include/types.h"

namespace neo {
namespace proc {

enum ProcessState : uint32 {
    PROC_FREE     = 0,
    PROC_READY    = 1,
    PROC_RUNNING  = 2,
    PROC_BLOCKED  = 3,
    PROC_SLEEPING = 4,
    PROC_ZOMBIE   = 5
};

static constexpr uint32 MAX_PROCESSES = 32;
static constexpr uint32 STACK_SIZE    = 8192;
static constexpr uint32 FPU_CTX_SIZE  = 324;

struct RegisterContext {
    uint32 d[8];    /* D0-D7 */
    uint32 a[7];    /* A0-A6 */
    uint32 usp;     /* User stack pointer */
    uint32 sr;      /* Status register */
    uint32 pc;      /* Program counter */
} __attribute__((packed));

struct PCB {
    uint32          pid;
    ProcessState    state;
    uint32          priority;
    RegisterContext regs;
    uint32          ssp;
    uint8*          stack_base;
    uint32          stack_size;
    bool            fpu_used;
    uint8           fpu_ctx[FPU_CTX_SIZE] __attribute__((aligned(4)));
    uint32          wake_tick;
    int32           exit_code;
    char            name[32];
};

static PCB   process_table[MAX_PROCESSES];
static int32 current_pid      = -1;
static int32 next_pid_counter = 1;
static bool  scheduler_active = false;

/* External subsystem references */
namespace fpu {
    extern void save_context(void* ctx);
    extern void restore_context(const void* ctx);
    extern bool is_present();
    extern void init_context(void* ctx);
}

namespace mem {
    extern void* alloc(uint32 size);
    extern void  free(void* ptr);
}

namespace timer {
    extern uint32 get_ticks();
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static void clear_pcb(PCB& p)
{
    p.pid = 0;
    p.state = PROC_FREE;
    p.priority = 128;
    p.ssp = 0;
    p.stack_base = nullptr;
    p.stack_size = 0;
    p.fpu_used = false;
    p.wake_tick = 0;
    p.exit_code = 0;
    for (int i = 0; i < 8; i++) p.regs.d[i] = 0;
    for (int i = 0; i < 7; i++) p.regs.a[i] = 0;
    p.regs.usp = 0;
    p.regs.sr  = 0;
    p.regs.pc  = 0;
    for (int i = 0; i < 32; i++) p.name[i] = 0;
}

static int32 find_free_slot()
{
    for (uint32 i = 0; i < MAX_PROCESSES; i++)
        if (process_table[i].state == PROC_FREE) return static_cast<int32>(i);
    return -1;
}

static void str_copy(char* d, const char* s, uint32 max)
{
    uint32 i = 0;
    while (i < max - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

/* ------------------------------------------------------------------ */
/*  Context switch                                                     */
/* ------------------------------------------------------------------ */

static void save_cpu_context(RegisterContext* ctx)
{
    __asm__ volatile (
        "move.l %%d0, %0\n\t"
        "move.l %%d1, %1\n\t"
        "move.l %%d2, %2\n\t"
        "move.l %%d3, %3\n\t"
        "move.l %%d4, %4\n\t"
        "move.l %%d5, %5\n\t"
        "move.l %%d6, %6\n\t"
        "move.l %%d7, %7\n\t"
        : "=m"(ctx->d[0]), "=m"(ctx->d[1]), "=m"(ctx->d[2]), "=m"(ctx->d[3]),
          "=m"(ctx->d[4]), "=m"(ctx->d[5]), "=m"(ctx->d[6]), "=m"(ctx->d[7])
        : : "memory"
    );
    __asm__ volatile (
        "move.l %%a0, %0\n\t"
        "move.l %%a1, %1\n\t"
        "move.l %%a2, %2\n\t"
        "move.l %%a3, %3\n\t"
        "move.l %%a4, %4\n\t"
        "move.l %%a5, %5\n\t"
        "move.l %%a6, %6\n\t"
        : "=m"(ctx->a[0]), "=m"(ctx->a[1]), "=m"(ctx->a[2]), "=m"(ctx->a[3]),
          "=m"(ctx->a[4]), "=m"(ctx->a[5]), "=m"(ctx->a[6])
        : : "memory"
    );

    uint32 usp_val;
    __asm__ volatile ("movec %%usp, %0" : "=d"(usp_val));
    ctx->usp = usp_val;

    uint16 sr_val;
    __asm__ volatile ("move.w %%sr, %0" : "=d"(sr_val));
    ctx->sr = sr_val;
}

} /* close namespace proc */
} /* close namespace neo */

extern "C" void neo_proc_restore_cpu_context(const neo::proc::RegisterContext* ctx);
__asm__(
    ".text\n"
    ".align 2\n"
    ".global neo_proc_restore_cpu_context\n"
    ".type neo_proc_restore_cpu_context, @function\n"
    "neo_proc_restore_cpu_context:\n"
    "    move.l 4(%sp), %a0\n"
    "    movem.l (%a0), %d0-%d7\n"
    "    movem.l 32(%a0), %a0-%a6\n"
    "    rts\n"
    ".size neo_proc_restore_cpu_context, .-neo_proc_restore_cpu_context\n"
);

namespace neo {
namespace proc {

/* ------------------------------------------------------------------ */
/*  Idle process                                                       */
/* ------------------------------------------------------------------ */

static void idle_loop()
{
    for (;;) {
        __asm__ volatile ("stop #0x2000" : : : "memory");
    }
}

extern "C" void shell_main();

/* Forward declarations */
int32 create(uint32 entry_point, const char* name, uint32 priority);

/* ------------------------------------------------------------------ */
/*  Public interface                                                    */
/* ------------------------------------------------------------------ */

void init()
{
    for (uint32 i = 0; i < MAX_PROCESSES; i++)
        clear_pcb(process_table[i]);

    current_pid = -1;
    next_pid_counter = 1;
    scheduler_active = false;

    /* Idle process: PID 0, slot 0, lowest priority */
    PCB& idle = process_table[0];
    idle.pid = 0;
    idle.state = PROC_READY;
    idle.priority = 255;
    str_copy(idle.name, "idle", 32);

    idle.stack_base = static_cast<uint8*>(mem::alloc(STACK_SIZE));
    idle.stack_size = STACK_SIZE;
    if (idle.stack_base) {
        uint32 stop = reinterpret_cast<uint32>(idle.stack_base) + STACK_SIZE;
        idle.regs.usp = stop;
        idle.regs.pc  = reinterpret_cast<uint32>(&idle_loop);
        idle.regs.sr  = 0x2000;  /* Supervisor, IPL=0 */
        idle.ssp = stop;
    }

    /* Shell process: PID 1 */
    // create(reinterpret_cast<uint32>(&shell_main), "shell", 0);

    scheduler_active = true;
}

int32 create(uint32 entry_point, const char* name, uint32 priority)
{
    int32 slot = find_free_slot();
    if (slot < 0) return -1;

    PCB& pcb = process_table[slot];
    clear_pcb(pcb);

    pcb.pid      = static_cast<uint32>(next_pid_counter++);
    pcb.state    = PROC_READY;
    pcb.priority = priority;
    if (name) str_copy(pcb.name, name, 32);

    pcb.stack_base = static_cast<uint8*>(mem::alloc(STACK_SIZE));
    if (!pcb.stack_base) { pcb.state = PROC_FREE; return -1; }
    pcb.stack_size = STACK_SIZE;

    uint32 stop = reinterpret_cast<uint32>(pcb.stack_base) + STACK_SIZE;
    pcb.regs.pc  = entry_point;
    pcb.regs.sr  = 0x0000;   /* User mode, IPL=0 */
    pcb.regs.usp = stop;
    pcb.ssp      = stop - 8;

    /* Initial exception frame for RTE */
    uint32* sp = reinterpret_cast<uint32*>(pcb.ssp);
    sp[0] = pcb.regs.sr;
    sp[1] = entry_point;

    if (fpu::is_present())
        fpu::init_context(pcb.fpu_ctx);

    return static_cast<int32>(pcb.pid);
}

void schedule()
{
    if (!scheduler_active) return;

    /* Wake sleeping processes */
    uint32 now = timer::get_ticks();
    for (uint32 i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_SLEEPING && now >= process_table[i].wake_tick)
            process_table[i].state = PROC_READY;
    }

    /* Find next: priority-aware round-robin */
    int32  next = -1;
    uint32 best = 0xFFFFFFFF;
    uint32 start = (current_pid >= 0) ? (static_cast<uint32>(current_pid) + 1) : 0;
    for (uint32 i = 0; i < MAX_PROCESSES; i++) {
        uint32 idx = (start + i) % MAX_PROCESSES;
        if (process_table[idx].state == PROC_READY && process_table[idx].priority < best) {
            best = process_table[idx].priority;
            next = static_cast<int32>(idx);
        }
    }

    if (next < 0) {
        if (process_table[0].state == PROC_READY || process_table[0].state == PROC_RUNNING)
            next = 0;
        else
            return;
    }

    if (next == current_pid) return;

    /* Save outgoing */
    if (current_pid >= 0 && process_table[current_pid].state == PROC_RUNNING) {
        PCB& old = process_table[current_pid];
        save_cpu_context(&old.regs);
        if (old.fpu_used && fpu::is_present())
            fpu::save_context(old.fpu_ctx);
        old.state = PROC_READY;
    }

    /* Restore incoming */
    PCB& np = process_table[next];
    current_pid = next;
    np.state = PROC_RUNNING;

    if (np.fpu_used && fpu::is_present())
        fpu::restore_context(np.fpu_ctx);

    /* Switch supervisor stack pointer to the incoming process's stack.
     * Without this, the RTS in neo_proc_restore_cpu_context would pop the
     * return address from the OUTGOING process's stack, resuming the wrong
     * execution context. Loading np.ssp into A7 (supervisor SP) first ensures
     * RTS unwinds through the correct process's saved call chain. */
    __asm__ volatile ("move.l %0, %%sp" : : "g"(np.ssp) : "memory");
    neo_proc_restore_cpu_context(&np.regs);
}

void yield()  { schedule(); }

void exit(int32 code)
{
    if (current_pid < 0) return;
    PCB& pcb = process_table[current_pid];
    pcb.exit_code = code;
    if (pcb.stack_base) { mem::free(pcb.stack_base); pcb.stack_base = nullptr; }
    pcb.state = PROC_FREE;
    current_pid = -1;
    schedule();
}

void sleep(uint32 ticks)
{
    if (current_pid < 0) return;
    process_table[current_pid].state = PROC_SLEEPING;
    process_table[current_pid].wake_tick = timer::get_ticks() + ticks;
    schedule();
}

int32 getpid()
{
    if (current_pid < 0) return -1;
    return static_cast<int32>(process_table[current_pid].pid);
}

const char* get_name(int32 pid)
{
    for (uint32 i = 0; i < MAX_PROCESSES; i++)
        if (process_table[i].state != PROC_FREE && static_cast<int32>(process_table[i].pid) == pid)
            return process_table[i].name;
    return nullptr;
}

void set_fpu_used()
{
    if (current_pid >= 0) process_table[current_pid].fpu_used = true;
}

}  /* namespace proc */
}  /* namespace neo */
