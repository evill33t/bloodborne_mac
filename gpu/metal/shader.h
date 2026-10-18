// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Bb::Metal {

enum class Stage { Vertex, Fragment, Compute };
constexpr uint32_t Unbound = ~uint32_t{0};
// Keep push constants out of the descriptor-buffer range.
constexpr uint32_t PushConstantIndex = 30;

struct Binding {
    uint32_t set{}, binding{}, count{1};
    uint32_t buffer{Unbound}, texture{Unbound}, sampler{Unbound};
};

struct Shader {
    Stage stage;
    std::string source;
    std::string entry_point;
    std::vector<Binding> bindings;
    size_t push_constant_size{};
    std::array<uint32_t, 3> workgroup_size{1, 1, 1};

    const Binding& FindBinding(uint32_t set, uint32_t binding) const;
};

// SPIR-V is a compiler intermediate here. No Vulkan loader/device is involved.
// Throws std::runtime_error on unsupported stages/resources or malformed input.
Shader TranslateSpirv(std::span<const uint32_t> words, Stage stage,
                      const std::string& entry_point = "main");
std::vector<uint32_t> ReadSpirv(const std::string& path);

} // namespace Bb::Metal
