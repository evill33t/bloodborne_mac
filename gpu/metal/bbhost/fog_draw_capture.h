// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

struct ElfImage;

namespace MacFeatures {
// Default off. BBHOST_FOG_DRAW_CAPTURE_DIR and a changed DC_TRACE_FILE token
// enable one bounded observation of original native draw counts2..16.
// The checked hook changes loaded process memory only, and always continues
// into the original function. It neither replaces command data nor waits on GPU.
// BBHOST_FOG_ENTRY_CAPTURE=1 additionally snapshots the original28ec760 early
// gate fields. BBHOST_FOG_ALLOCATION_CAPTURE=1 additionally records checked
// allocation inputs and actual EAX returns, requiring the producer probe.
// Each stream has4096 slots; shared capture ends at any limit or30seconds.
void InstallFogDrawCapture(ElfImage* image);
// Cheap consumption identity for the same opt-in run. No command copies,
// guest reads, allocations, logging, or GPU synchronization on the CP path.
void ObserveFogDrawPacket(std::uint64_t packet,std::uint32_t count,std::uint64_t ps,
                         std::uint64_t vs,std::uint64_t cb0) noexcept;
}
