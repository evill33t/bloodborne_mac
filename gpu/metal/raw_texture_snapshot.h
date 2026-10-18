// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <span>
#include <vector>
namespace Gpu::Metal::RawTextureSnapshot {
struct Descriptor {
    std::uint32_t format=0,type=0,width=0,height=0,depth=0,layers=0,levels=0;
    bool operator==(const Descriptor&) const=default;
};
enum class Aspect:std::uint32_t {Color=0,Depth=1,Stencil=2};
struct Plane {
    std::uint32_t level=0,slice=0,width=0,height=0,depth=0,texel_bytes=0;
    Aspect aspect=Aspect::Color;
    std::uint64_t row_bytes=0,image_bytes=0,size=0;
    bool operator==(const Plane&) const=default;
};
// Metadata only; rejects unsupported formats/types/MSAA before allocating a
// staging buffer. Planes include every mip and slice and separate depth/stencil
// aspects. Row padding is 256-byte aligned and canonicalized to zero on read.
bool Describe(void* texture,Descriptor&,std::vector<Plane>&,std::string& error);
bool Layout(void* device,const Descriptor&,std::vector<Plane>&,std::string& error);
// The coordinator has stopped submission and drained GPU work. Each call uses
// one bounded plane staging buffer and a deadline, with no sampling/colour
// conversion. Call from the worker, never from the presentation/UI thread.
bool Capture(void* device,void* queue,void* texture,std::size_t plane,std::vector<std::uint8_t>&,
             std::chrono::steady_clock::time_point deadline,std::string& error);
bool Restore(void* device,void* queue,void* texture,std::size_t plane,std::span<const std::uint8_t>,
             std::chrono::steady_clock::time_point deadline,std::string& error);
}
