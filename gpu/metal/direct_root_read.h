// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// GPU-time address-only read admission. Never reads guest source words.
#include "gpu/metal/direct_root_span.h"
#include <array>
#include <cstdint>

namespace Gpu::Metal::DirectRootRead {
using U64=std::uint64_t;
struct Request {U64 address=0,bytes=0,encoded=DirectRootSpan::Fallback;};
enum class Reject:unsigned {
    None,Untranslated,Layout,Context,StageWrites,MissingGuard,MapGeneration,
    Mapping,PhysicalMismatch,CpuPending,Callback,GpuUnknown,GpuCurrentCommand,Lease,Count
};
inline constexpr std::array Names{"none","untranslated","layout","context","stage-writes",
    "missing-cp-guard","map-generation","mapping","physical-mismatch","cpu-pending",
    "callback","gpu-unknown","gpu-current-command","lease"};
// The actual renderer and public CPU fixtures share this fail-closed order.
// Guards own no source snapshot: Mapping checks map metadata/backing identity;
// PublishRead is called exactly once, last, on the actual consuming command.
template<class Guards>
Reject Protect(const Request& request,Guards& guard) {
    if(request.encoded==DirectRootSpan::Fallback)return Reject::Untranslated;
    if(!request.bytes || (request.bytes&3) || request.bytes>DirectRootSpan::MaximumBytes ||
       (request.address&3) || request.address>=DirectRootSpan::AddressLimit ||
       request.bytes>DirectRootSpan::AddressLimit-request.address)return Reject::Layout;
    if(!guard.Context())return Reject::Context;
    if(!guard.StagesReadOnly())return Reject::StageWrites;
    if(!guard.HasCpuPredicate())return Reject::MissingGuard;
    if(!guard.GenerationStable())return Reject::MapGeneration;
    if(!guard.Mapping(request))return Reject::Mapping;
    if(!guard.GenerationStable())return Reject::MapGeneration;
    if(!guard.PhysicalMatch(request))return Reject::PhysicalMismatch;
    if(!guard.CpuReady(request))return Reject::CpuPending;
    if(!guard.CallbacksReady())return Reject::Callback;
    const auto writer=guard.WritersReady();
    if(writer!=Reject::None)return writer;
    if(!guard.GenerationStable())return Reject::MapGeneration;
    if(!guard.PublishRead(request))return Reject::Lease;
    return Reject::None;
}
} // namespace Gpu::Metal::DirectRootRead
