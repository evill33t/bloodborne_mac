// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "gpu/metal/game_settings.h"

namespace MacFeatures {
inline constexpr std::size_t ImageTailSize = 16 * 1024 * 1024;
void Initialize(const MacSettings::Values& startup);
void BindImage(unsigned char* image, std::uint64_t size);
void AddSegment(std::uint64_t at, std::uint64_t size, std::uint32_t flags);
void AddRelative(std::uint64_t at, std::uint64_t target);
void Install();
void PollMenu();
void SetGameTick(void (*tick)());
void SetSettings(const MacSettings::Values& values);
bool TakeSettings(MacSettings::Values& values);
MacSettings::Values Settings();
bool RequestSettings(const MacSettings::Values& values);
std::string TakeMessage();
void RequestRestart();
bool TakeRestartRequest();
void RequestDebugToggle();
bool TakeDebugToggle();
void UpdateGraphics(const MacSettings::Values& values);
}

extern "C" void bbmac_features_segment(std::uint64_t at,std::uint64_t size,std::uint32_t flags);
extern "C" void bbmac_features_relative(std::uint64_t at,std::uint64_t target);
extern "C" void bbmac_features_install();
