// SprjDebugCam runtime metadata (no layout: evidence is the class registration).
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

// Marker for the observed SprjDebugCam class. Not a layout type.
struct SprjDebugCam {
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_DEBUG_CAM_RUNTIME_CLASS;
};

}  // namespace bb
