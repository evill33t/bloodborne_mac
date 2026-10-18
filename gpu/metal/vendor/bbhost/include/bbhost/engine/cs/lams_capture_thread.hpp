// Native executor owner used for the LAMS stdin reader.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"
#include "bbhost/engine/cs/ez_work.hpp"

namespace bb {

// Reflected 0x18-byte owner, allocated by CSLamsCaptureStep::InitializeStdin.
// It owns a separate 0xe8-byte CSEzWork and eight-byte completion holder.
//
// Submission borrows the step through a member fragment, retains completion,
// and queues under the executor mutex; the caller wakes the workers
// separately. Destruction waits indefinitely for the holder's event,
// releases/frees the holder, then destroys the executor. The blocking stdin
// read has no captured cancellation path; native object lifetime must outlast
// its worker callback.
struct CSLamsCaptureThread {
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_LAMS_CAPTURE_THREAD_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x18;
    static constexpr Rva VTABLE{0x5357e70};
    static constexpr Rva CONSTRUCTOR_FN{0x1faa8b0};
    static constexpr Rva DESTRUCTOR_FN{0x1faa9a0};
    static constexpr Rva SIZE_GETTER_FN{0x1fab360};
    // Queues work only; InitializeStdin calls CSEzWork wake-workers afterward.
    static constexpr Rva QUEUE_STDIN_FN{0x1faaa80};
    static constexpr std::uint32_t WORKER_PRIORITY = 3;
    static constexpr std::uint32_t AFFINITY_POLICY = 0;

    const void* vftable;
    CSEzWork* work;
    CSEzWorkCompletionHolder* completion_holder;
};

// Descriptive alias for the 0x30-byte member fragment used by this owner. Its
// target borrows CSLamsCaptureStep, callback is READ_STDIN_FN, adjustment is
// zero, and cleanup mode 0 destroys it after execution. Not a newly identified
// reflected class.
using CSLamsCaptureFragment = CSEzWorkMemberFragment;

namespace detail::lams_capture_thread_layout {
BB_SIZE(CSLamsCaptureThread, CSLamsCaptureThread::SIZE);
static_assert(alignof(CSLamsCaptureThread) == 8, "alignof(CSLamsCaptureThread)");
BB_OFFSET(CSLamsCaptureThread, work, 0x08);
BB_OFFSET(CSLamsCaptureThread, completion_holder, 0x10);
BB_SIZE(CSLamsCaptureFragment, 0x30);
BB_OFFSET(CSLamsCaptureFragment, completion, 0x08);
BB_OFFSET(CSLamsCaptureFragment, cleanup_mode, 0x10);
BB_OFFSET(CSLamsCaptureFragment, target, 0x18);
BB_OFFSET(CSLamsCaptureFragment, member_function_bits, 0x20);
BB_OFFSET(CSLamsCaptureFragment, this_adjustment, 0x28);
}  // namespace detail::lams_capture_thread_layout

}  // namespace bb
