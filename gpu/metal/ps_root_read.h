// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Exact final prepared PS root extents; no guest table/source-word reads.
#include "gpu/metal/direct_root_read.h"
#include "gpu/metal/static_smrd.h"
#include <algorithm>
#include <cstdint>
#include <span>
namespace Gpu::Metal::PsRootRead {
using DirectRootRead::U64;
using DirectRootRead::Request;
using DirectRootRead::Reject;
using DirectRootRead::Protect;
using DirectRootRead::Names;
inline Request MakeRequest(U64 hash,unsigned slot,std::span<const std::uint32_t,16> ud,U64 encoded) {
    Request request;request.encoded=encoded;
    if(slot>=2)return request;
    const unsigned root=slot ? 12 : 2;
    for(const auto& load:StaticSmrd::Layout(hash))if(load.root==root)
        request.bytes=std::max(request.bytes,U64(load.offset)+U64(load.words)*4);
    request.address=DirectRootSpan::SmrdRoot(ud[root],ud[root+1]);return request;
}
} // namespace Gpu::Metal::PsRootRead
