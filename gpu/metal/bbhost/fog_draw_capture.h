// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

struct ElfImage;

namespace MacFeatures {
// Default off. BBHOST_FOG_DRAW_CAPTURE_DIR and a changed DC_TRACE_FILE token
// enable one bounded observation of original native draw counts2..16.
// The checked hook changes loaded process memory only, and always continues
// into the original function. It neither replaces command data nor waits on GPU.
// BBHOST_FOG_ENTRY_CAPTURE=1 additionally snapshots the original28ec760 early
// gate fields. Each stream has4096 slots; shared capture ends at either limit.
void InstallFogDrawCapture(ElfImage* image);
}
