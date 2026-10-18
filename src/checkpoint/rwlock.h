// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace Bb::Checkpoint {
void RwInterrupt(bool requested);
bool RwPause(std::vector<std::uint8_t>&,std::string& error);
void RwThaw();
std::size_t RwWaiters();
bool RwRestore(std::span<const std::uint8_t>,std::string& error);
}
extern "C" std::uintptr_t bb_checkpoint_rwlock_resolve(const char* name);
extern "C" int bb_checkpoint_rwlock_replayable(const char* name);
