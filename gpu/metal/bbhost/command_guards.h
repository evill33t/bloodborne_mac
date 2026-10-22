// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
struct ElfImage;
namespace MacCommandGuards {
// Register before guest execution; the function's first argument owns its lock.
void RegisterMethod(std::uint64_t address,bool context=false);
void Install(ElfImage* image);
}
