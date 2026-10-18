// Shared Type A/B/C/D work executors used by resource-loading paths.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct CSEzWork;

// Native singleton allocated as 0x20 bytes aligned to eight by RVA 0x201b0e0.
// Its four slots are zeroed before initialization. The singleton assertion
// identifies CSEzWorkPool; no vtable or runtime-class registration is claimed.
//
// Each slot owns a separate 0xe8-byte CSEzWork. Initialization names them
// EzWorkPool_TypeA/B/C/D and supplies distinct priority/affinity options.
// Cleanup destroys, frees, and clears the slots in C/B/A/D order; the engine
// owner then frees the pool and clears its singleton pointer. This pool is
// separate from the executors owned by CSChrThread and CSClothThread.
struct CSEzWorkPool {
    static constexpr std::size_t SIZE = 0x20;
    static constexpr Rva SINGLETON_PTR = CS_EZ_WORK_POOL_SINGLETON_PTR;
    static constexpr Rva NAME_STRING{0x4938b7a};
    // Populates the four previously zeroed executor slots.
    static constexpr Rva INITIALIZE_EXECUTORS_FN{0x2033230};
    // Releases the executors without freeing this pool object.
    static constexpr Rva CLEAR_EXECUTORS_FN{0x20333f0};

    // Batched resource-loading path at RVA 0x1d874a0 appends work here.
    // Priority 6, policy 7: four workers, affinity masks 1/2/4/8.
    CSEzWork* type_a;
    // Static work context returned by RVA 0x1f36d40 borrows this executor.
    // Priority 6, policy 6: three workers, affinity masks 2/4/8.
    CSEzWork* type_b;
    // Resource-fragment submission at RVA 0x1d57c70 uses this executor.
    // Priority 6, policy 14: one worker, affinity mask 0x30.
    CSEzWork* type_c;
    // Static work context returned by RVA 0x1f36e30 borrows this executor.
    // Priority 2, policy 13: one worker, affinity mask 0x0e.
    CSEzWork* type_d;
};

namespace detail::ez_work_pool_layout {
BB_SIZE(CSEzWorkPool, CSEzWorkPool::SIZE);
static_assert(alignof(CSEzWorkPool) == 8, "alignof(CSEzWorkPool)");
BB_OFFSET(CSEzWorkPool, type_a, 0x00);
BB_OFFSET(CSEzWorkPool, type_b, 0x08);
BB_OFFSET(CSEzWorkPool, type_c, 0x10);
BB_OFFSET(CSEzWorkPool, type_d, 0x18);
static_assert(CSEzWorkPool::SINGLETON_PTR.bn() == 0x059404f0, "CSEzWorkPool::SINGLETON_PTR");
static_assert(CSEzWorkPool::NAME_STRING.bn() == 0x04d38b7a, "CSEzWorkPool::NAME_STRING");
}  // namespace detail::ez_work_pool_layout

}  // namespace bb
