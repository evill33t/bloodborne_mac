// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace Bb::Checkpoint {
void SyncInterrupt(bool requested);
bool SyncPause(std::vector<std::uint8_t>& bytes,std::string& error);
void SyncThaw();
std::size_t SyncWaiters(bool condition);
bool SyncRestore(std::span<const std::uint8_t> bytes,std::string& error);
}
extern "C" std::uintptr_t bb_checkpoint_mutex_resolve(const char* name);
extern "C" int bb_checkpoint_mutex_replayable(const char* name);
