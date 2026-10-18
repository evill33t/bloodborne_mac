// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "material_snapshot.h"

namespace Gpu::Metal::TextureSnapshot {
// Capture/restore authoritative GPU bytes, without sampling, colour conversion
// or premultiplication. The checkpoint coordinator must first stop submission
// and commit pending work to this queue. These calls wait for their own blit;
// they must not run on the UI thread while a frame handoff is unacknowledged.
// The initial provider supports non-multisampled RGBA8 2D stain textures only.
bool Capture(void* device,void* queue,void* texture,MaterialSnapshot::Texture&,
             std::string& error);
bool Restore(void* device,void* queue,void* texture,const MaterialSnapshot::Texture&,
             std::string& error);
}
