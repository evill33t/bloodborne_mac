// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <spirv_msl.hpp>

namespace Bb::Metal {
namespace {
spv::ExecutionModel Model(Stage stage) {
    switch (stage) {
    case Stage::Vertex: return spv::ExecutionModelVertex;
    case Stage::Fragment: return spv::ExecutionModelFragment;
    case Stage::Compute: return spv::ExecutionModelGLCompute;
    }
    throw std::runtime_error("Unsupported Metal shader stage");
}

uint32_t ArrayCount(const spirv_cross::SPIRType& type) {
    uint32_t count = 1;
    for (size_t i = 0; i < type.array.size(); ++i) {
        if (!type.array_size_literal[i] || !type.array[i] ||
            type.array[i] > 128 || count > 128 / type.array[i]) {
            throw std::runtime_error("Metal descriptor arrays require a fixed size of at most 128");
        }
        count *= type.array[i];
    }
    return count;
}
} // namespace

const Binding& Shader::FindBinding(uint32_t set, uint32_t binding) const {
    const auto it = std::find_if(bindings.begin(), bindings.end(), [&](const Binding& b) {
        return b.set == set && b.binding == binding;
    });
    if (it == bindings.end()) throw std::runtime_error("Shader descriptor binding was not reflected");
    return *it;
}

Shader TranslateSpirv(std::span<const uint32_t> words, Stage stage,
                      const std::string& entry_point) {
    if (words.size() < 5 || words[0] != 0x07230203 || !words[3] || words[3] > 1'000'000) {
        throw std::runtime_error("Invalid SPIR-V header");
    }
    // Check instruction boundaries before handing external input to the compiler.
    for (size_t at = 5; at < words.size();) {
        const size_t length = words[at] >> 16;
        if (!length || length > words.size() - at) {
            throw std::runtime_error("Truncated SPIR-V instruction");
        }
        at += length;
    }
    try {
        spirv_cross::CompilerMSL compiler(words.data(), words.size());
        const auto model = Model(stage);
        compiler.set_entry_point(entry_point, model);
        auto options = compiler.get_msl_options();
        options.platform = spirv_cross::CompilerMSL::Options::macOS;
        options.msl_version = spirv_cross::CompilerMSL::Options::make_msl_version(2, 4);
        options.argument_buffers = false;
        compiler.set_msl_options(options);
        // Vulkan and Metal both use a 0..1 clip-depth range. Preserve the incoming
        // Y convention; viewport/front-face conversion belongs to the rasterizer.
        auto common = compiler.get_common_options();
        common.vertex.fixup_clipspace = false;
        common.vertex.flip_vert_y = false;
        compiler.set_common_options(common);

        Shader shader{.stage = stage};
        const auto resources = compiler.get_shader_resources();
        if (!resources.subpass_inputs.empty() || !resources.atomic_counters.empty() ||
            !resources.acceleration_structures.empty() || !resources.shader_record_buffers.empty()) {
            throw std::runtime_error("Unsupported Metal shader resource category");
        }
        const auto add = [&](const auto& list, bool buffer, bool texture, bool sampler) {
            for (const auto& resource : list) {
                Binding b{
                    .set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet),
                    .binding = compiler.get_decoration(resource.id, spv::DecorationBinding),
                    .count = ArrayCount(compiler.get_type(resource.type_id)),
                    .buffer = buffer ? 0u : Unbound,
                    .texture = texture ? 0u : Unbound,
                    .sampler = sampler ? 0u : Unbound,
                };
                shader.bindings.push_back(b);
            }
        };
        add(resources.uniform_buffers, true, false, false);
        add(resources.storage_buffers, true, false, false);
        add(resources.separate_images, false, true, false);
        add(resources.storage_images, false, true, false);
        add(resources.separate_samplers, false, false, true);
        add(resources.sampled_images, false, true, true);
        std::sort(shader.bindings.begin(), shader.bindings.end(), [](const auto& a, const auto& b) {
            return std::tie(a.set, a.binding) < std::tie(b.set, b.binding);
        });
        uint32_t buffers = 0, textures = 0, samplers = 0;
        for (size_t i = 0; i < shader.bindings.size(); ++i) {
            auto& b = shader.bindings[i];
            if (i && b.set == shader.bindings[i - 1].set &&
                b.binding == shader.bindings[i - 1].binding) {
                throw std::runtime_error("Duplicate shader descriptor binding");
            }
            if (b.buffer != Unbound) { b.buffer = buffers; buffers += b.count; }
            if (b.texture != Unbound) { b.texture = textures; textures += b.count; }
            if (b.sampler != Unbound) { b.sampler = samplers; samplers += b.count; }
            if (buffers > PushConstantIndex || textures > 128 || samplers > 16) {
                throw std::runtime_error("Shader exceeds direct Metal resource-slot limits");
            }
            spirv_cross::MSLResourceBinding remap;
            remap.stage = model;
            remap.desc_set = b.set;
            remap.binding = b.binding;
            remap.count = b.count;
            remap.msl_buffer = b.buffer;
            remap.msl_texture = b.texture;
            remap.msl_sampler = b.sampler;
            compiler.add_msl_resource_binding(remap);
        }
        if (resources.push_constant_buffers.size() > 1) {
            throw std::runtime_error("Multiple push-constant blocks are unsupported");
        }
        if (!resources.push_constant_buffers.empty()) {
            shader.push_constant_size = compiler.get_declared_struct_size(
                compiler.get_type(resources.push_constant_buffers.front().base_type_id));
            spirv_cross::MSLResourceBinding push;
            push.stage = model;
            push.desc_set = spirv_cross::ResourceBindingPushConstantDescriptorSet;
            push.binding = spirv_cross::ResourceBindingPushConstantBinding;
            push.msl_buffer = PushConstantIndex;
            compiler.add_msl_resource_binding(push);
        }
        if (stage == Stage::Compute) {
            for (uint32_t i = 0; i < 3; ++i) {
                shader.workgroup_size[i] = compiler.get_execution_mode_argument(spv::ExecutionModeLocalSize, i);
                if (!shader.workgroup_size[i]) throw std::runtime_error("Invalid compute workgroup size");
            }
        }
        shader.source = compiler.compile();
        if (compiler.needs_swizzle_buffer() || compiler.needs_buffer_size_buffer() ||
            compiler.needs_output_buffer() || compiler.needs_patch_output_buffer() ||
            compiler.needs_view_mask_buffer() || compiler.needs_dispatch_base_buffer()) {
            throw std::runtime_error("Shader requires an unimplemented Metal auxiliary buffer");
        }
        shader.entry_point = compiler.get_cleansed_entry_point_name(entry_point, model);
        return shader;
    } catch (const spirv_cross::CompilerError& error) {
        throw std::runtime_error(std::string("SPIR-V to Metal: ") + error.what());
    }
}

std::vector<uint32_t> ReadSpirv(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open SPIR-V: " + path);
    const auto size = file.tellg();
    if (size < 20 || size > 64 * 1024 * 1024 || size % 4) {
        throw std::runtime_error("Invalid SPIR-V file size: " + path);
    }
    std::vector<uint32_t> words(static_cast<size_t>(size) / 4);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(words.data()), size)) {
        throw std::runtime_error("Cannot read SPIR-V: " + path);
    }
    return words;
}
} // namespace Bb::Metal
