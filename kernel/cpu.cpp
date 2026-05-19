/*
 * NeoBench Bare-Metal Amiga Kernel
 * CPU/FPU Detection
 */

#include "../include/neobench.h"
#include "../include/types.h"

namespace neo {
namespace cpu {

extern "C" volatile uint32 _cpu_type;
extern "C" volatile uint32 _fpu_type;

static constexpr uint32 HW_CPU_68030 = 30;
static constexpr uint32 HW_CPU_68040 = 40;
static constexpr uint32 HW_CPU_68060 = 60;
static constexpr uint32 HW_FPU_NONE  = 0;
static constexpr uint32 HW_FPU_68881 = 1;
static constexpr uint32 HW_FPU_68882 = 2;
static constexpr uint32 HW_FPU_INT   = 3;

void detect(CpuInfo* info)
{
    if (!info) return;
    switch (_cpu_type) {
    case HW_CPU_68060: info->type = CPU_68060; break;
    case HW_CPU_68040: info->type = CPU_68040; break;
    default:           info->type = CPU_68030; break;
    }
    switch (_fpu_type) {
    case HW_FPU_68881: info->fpu_type = FPU_68881;    break;
    case HW_FPU_68882: info->fpu_type = FPU_68882;    break;
    case HW_FPU_INT:   info->fpu_type = FPU_INTERNAL; break;
    default:           info->fpu_type = FPU_NONE;     break;
    }
    info->has_fpu    = (info->fpu_type != FPU_NONE);
    info->cache_size = 4096;
    info->mmu_present = true;
}

bool is_present() { return true; }

}  /* namespace cpu */
}  /* namespace neo */
