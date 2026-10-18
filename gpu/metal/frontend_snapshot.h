// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gpu/liverpool/command_processor.h"
namespace Gpu::Metal::FrontendSnapshot {
bool Encode(const Liverpool::CommandProcessor::CheckpointState&,std::vector<std::uint8_t>&,std::string& error);
bool Decode(std::span<const std::uint8_t>,Liverpool::CommandProcessor::CheckpointState&,std::string& error);
}
