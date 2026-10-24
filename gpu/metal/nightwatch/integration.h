// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gpu/metal/game_settings.h"
#include <cstdint>
#include <string>
namespace MacNightwatch {
bool Available();
bool Active();
bool ValidJoinCode(const std::string& code);
void Configure(const MacSettings::Values& settings);
void Register();
void SetImage(unsigned char*,std::uint64_t);
void Install();
void Shutdown();
std::string Status();
std::string Code(bool lan=false);
void Leave();
void Rejoin();
}
