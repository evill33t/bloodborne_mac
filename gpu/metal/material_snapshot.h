// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Gpu::Metal::MaterialSnapshot {
// A graphics debugging fixture. GPU images are authoritative; memory blocks
// retain the shader inputs for inspection, and are never written into a game.
struct Texture {
    std::uint32_t width=0,height=0,format=0;
    std::vector<std::vector<std::uint8_t>> mips;
    bool operator==(const Texture&) const = default;
};
struct MemoryBlock {
    std::uint64_t address=0;
    std::vector<std::uint8_t> bytes;
    bool operator==(const MemoryBlock&) const = default;
};
struct Material {
    std::uint64_t key=0,shader_hash=0;
    Texture colour,surface;
    std::array<std::uint32_t,16> vertex_data{},pixel_data{};
    std::vector<MemoryBlock> memory;
    bool operator==(const Material&) const = default;
};
struct Snapshot {
    std::string game_hash;
    std::vector<Material> materials;
    bool operator==(const Snapshot&) const = default;
};
inline constexpr std::uint64_t MaxFileBytes=256ull<<20;
inline constexpr unsigned MaxMaterials=64;
std::uint64_t Key(std::uint64_t shader,std::uint64_t normal_asset,std::uint64_t diffuse_asset);
bool HasStains(const Texture& colour);
bool Save(const std::filesystem::path& path,const Snapshot& snapshot,std::string& error);
// Parse into a temporary object, then replace `out` only after all validation.
// The caller must supply the nonempty identity of the prepared game build.
bool Load(const std::filesystem::path& path,const std::string& game_hash,Snapshot& out,std::string& error);
}
