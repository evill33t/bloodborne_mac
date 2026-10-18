// SPDX-License-Identifier: GPL-2.0-or-later
#include "device.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace Bb::Metal;
namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void Reject(F action, const char* message) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}
Shader Load(const char* name, Stage stage = Stage::Compute) {
    return TranslateSpirv(ReadSpirv(std::string(BB_METAL_SHADER_DIR) + '/' + name + ".spv"), stage);
}

void TranslationContracts() {
    Reject([] { TranslateSpirv({}, Stage::Compute); }, "Empty SPIR-V accepted");
    const uint32_t truncated[]{0x07230203, 0x00010000, 0, 10, 0, 0x00040000};
    Reject([&] { TranslateSpirv(truncated, Stage::Compute); }, "Truncated instruction accepted");
    const auto copy = Load("buffer_multi_copy_comp");
    Require(copy.workgroup_size == std::array<uint32_t, 3>{64, 1, 1}, "Workgroup reflection mismatch");
    Require(copy.push_constant_size == 4, "Push-constant reflection mismatch");
    Require(copy.FindBinding(0, 0).buffer == 0 && copy.FindBinding(0, 1).buffer == 1 &&
        copy.FindBinding(0, 2).buffer == 2, "Buffer remapping mismatch");
    Require(copy.source.find("[[buffer(30)]]") != std::string::npos, "Push constants collided with descriptors");
    const auto camera = Load("camera_motion_comp");
    Require(camera.FindBinding(0, 0).texture == 0 && camera.FindBinding(0, 1).texture == 1 &&
        camera.FindBinding(0, 2).texture == 2, "Texture remapping mismatch");
    const auto fragment = Load("post_process_frag", Stage::Fragment);
    Require(fragment.FindBinding(0, 0).texture == 0 && fragment.FindBinding(0, 0).sampler == 0,
        "Combined image/sampler reflection mismatch");
    Reject([] { Load("post_process_frag", Stage::Vertex); }, "Shader stage mismatch accepted");
    const auto sets = TranslateSpirv(ReadSpirv(std::string(BB_METAL_FIXTURE_DIR) + "/bindings.spv"), Stage::Compute);
    Require(sets.FindBinding(0, 9).count == 3 && sets.FindBinding(0, 9).texture == 0 &&
        sets.FindBinding(0, 9).sampler == 0 && sets.FindBinding(1, 9).buffer == 0 &&
        sets.FindBinding(2, 9).buffer == 1, "Descriptor sets or array slots collided");
    Reject([] {
        TranslateSpirv(ReadSpirv(std::string(BB_METAL_FIXTURE_DIR) + "/too_many_buffers.spv"), Stage::Compute);
    }, "Metal buffer limit was not enforced");
    std::cout << "PASS: shader validation, reflection and descriptor remapping\n";
}

std::vector<Shader> HostShaders() {
    std::vector<Shader> shaders;
    std::ifstream manifest(std::string(BB_METAL_SHADER_DIR) + "/manifest.txt");
    Require(bool(manifest), "Host shader manifest is missing");
    for (std::string path; std::getline(manifest, path);) {
        if (path.empty()) continue;
        const auto name = std::filesystem::path(path).stem().string();
        const auto stage = name.ends_with("_vert") ? Stage::Vertex :
            name.ends_with("_frag") ? Stage::Fragment : Stage::Compute;
        shaders.push_back(TranslateSpirv(ReadSpirv(path), stage));
    }
    Require(shaders.size() == BB_METAL_HOST_SHADER_COUNT, "Host shader build outputs are missing");
    return shaders;
}

void CompileHostShaders(const Device& device, const std::vector<Shader>& shaders) {
    for (const auto& shader : shaders) {
        if (shader.stage == Stage::Compute) device.ComputePipeline(shader);
        else device.Compile(shader);
    }
    device.ComputePipeline(TranslateSpirv(ReadSpirv(std::string(BB_METAL_FIXTURE_DIR) + "/bindings.spv"), Stage::Compute));
    std::cout << "PASS: " << shaders.size() << " existing host shaders compiled by Metal\n";
}

void MultiCopy(const Device& device) {
    const auto shader = Load("buffer_multi_copy_comp");
    const auto pipeline = device.ComputePipeline(shader);
    std::array<uint32_t, 256> source{}, initial{};
    for (uint32_t i = 0; i < source.size(); ++i) source[i] = 0xa0000000u + i;
    initial.fill(0xdeadbeef);
    const uint32_t regions[][4]{{3, 7, 129, 0}, {199, 200, 29, 0}};
    const uint32_t count = 2;
    id<MTLBuffer> src = device.Buffer(sizeof(source), source.data());
    id<MTLBuffer> dst = device.Buffer(sizeof(initial), initial.data());
    id<MTLBuffer> ranges = device.Buffer(sizeof(regions), regions);
    id<MTLCommandBuffer> commands = device.Commands("Host multi-copy shader");
    id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
    [encoder setComputePipelineState:pipeline];
    [encoder setBuffer:src offset:0 atIndex:shader.FindBinding(0, 0).buffer];
    [encoder setBuffer:dst offset:0 atIndex:shader.FindBinding(0, 1).buffer];
    [encoder setBuffer:ranges offset:0 atIndex:shader.FindBinding(0, 2).buffer];
    [encoder setBytes:&count length:sizeof(count) atIndex:PushConstantIndex];
    // An extra group exercises the shader's push-constant bounds check.
    [encoder dispatchThreadgroups:MTLSizeMake(3, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
    [encoder endEncoding];
    Device::SubmitAndWait(commands);
    auto expected = initial;
    for (const auto& region : regions) {
        std::copy_n(source.begin() + region[0], region[2], expected.begin() + region[1]);
    }
    Require(!std::memcmp(dst.contents, expected.data(), sizeof(expected)), "Metal multi-copy readback differs");
    std::cout << "PASS: multi-region compute dispatch and shared-buffer readback\n";
}

void PostProcess(const Device& device) {
    const auto vertex = Load("fs_tri_vert", Stage::Vertex);
    const auto fragment = Load("post_process_frag", Stage::Fragment);
    const auto pipeline = device.RenderPipeline(vertex, fragment, MTLPixelFormatRGBA8Unorm);
    // An odd width exercises padded texture-to-buffer row copies.
    constexpr uint32_t width = 37, height = 19;
    const auto source = device.Texture2D(width, height, MTLPixelFormatRGBA8Unorm,
        MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
    const auto target = device.Texture2D(width, height, MTLPixelFormatRGBA8Unorm, MTLTextureUsageRenderTarget);
    id<MTLCommandBuffer> commands = device.Commands("Host post-processing shaders");
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = source;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.25, 0.5, 0.75, 1);
    id<MTLRenderCommandEncoder> clear = [commands renderCommandEncoderWithDescriptor:pass];
    [clear endEncoding];
    pass.colorAttachments[0].texture = target;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(1, 0, 1, 1);
    id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
    [encoder setRenderPipelineState:pipeline];
    const auto binding = fragment.FindBinding(0, 0);
    [encoder setFragmentTexture:source atIndex:binding.texture];
    MTLSamplerDescriptor* sampler_desc = [MTLSamplerDescriptor new];
    sampler_desc.minFilter = MTLSamplerMinMagFilterNearest;
    sampler_desc.magFilter = MTLSamplerMinMagFilterNearest;
    id<MTLSamplerState> sampler = [device.Native() newSamplerStateWithDescriptor:sampler_desc];
    [encoder setFragmentSamplerState:sampler atIndex:binding.sampler];
    struct { float gamma; uint32_t hdr, srgb_input; } push{1.f, 1, 0};
    Require(fragment.push_constant_size == sizeof(push), "Post-process constants layout mismatch");
    [encoder setFragmentBytes:&push length:sizeof(push) atIndex:PushConstantIndex];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [encoder endEncoding];
    Device::SubmitAndWait(commands);
    const auto data = device.ReadTexture(target);
    for (size_t i = 0; i < data.size(); i += 4) {
        Require(std::abs(int(data[i]) - 64) <= 1 && std::abs(int(data[i+1]) - 128) <= 1 &&
            std::abs(int(data[i+2]) - 191) <= 1 && data[i+3] == 255,
            "Metal full-screen draw/pixel readback differs");
    }
    std::cout << "PASS: full-screen draw, texture/sampler binding and private-texture readback\n";
}

float Half(uint16_t value) {
    const int exponent = (value >> 10) & 31;
    return (value & 0x8000 ? -1.f : 1.f) * (exponent == 0 ?
        std::ldexp(float(value & 1023), -24) : std::ldexp(1.f + float(value & 1023) / 1024.f, exponent - 15));
}

void CameraMotion(const Device& device) {
    const auto shader = Load("camera_motion_comp");
    const auto pipeline = device.ComputePipeline(shader);
    constexpr uint32_t width = 9, height = 7;
    const auto depth = device.Texture2D(width, height, MTLPixelFormatR32Float, MTLTextureUsageShaderRead);
    const auto object = device.Texture2D(width, height, MTLPixelFormatRGBA32Float, MTLTextureUsageShaderRead);
    const auto motion = device.Texture2D(width, height, MTLPixelFormatRG16Float, MTLTextureUsageShaderWrite);
    const auto run = [&](const char* name, float d, float translation, float rotation,
                         float object_depth, uint32_t mode, float expected, bool jitter = false) {
        std::array<float, width * height> depths;
        depths.fill(d);
        std::array<float, width * height * 4> objects;
        for (size_t i = 0; i < objects.size(); i += 4) {
            objects[i] = 2; objects[i+1] = 0; objects[i+2] = 1; objects[i+3] = object_depth;
        }
        device.UploadTexture(depth, {reinterpret_cast<const uint8_t*>(depths.data()), sizeof(depths)});
        device.UploadTexture(object, {reinterpret_cast<const uint8_t*>(objects.data()), sizeof(objects)});
        struct Params {
            float reproject[12], proj[4], prev_proj[4], size[2], jitter[2], previous_jitter[2];
            uint32_t mode;
        } params{{1,0,rotation,translation,0,1,0,0,0,0,1,0},
            {1,1,1,-0.05f},{1,1,1,-0.05f},{width,height},
            {jitter ? 0.37f : 0.f,jitter ? -0.29f : 0.f},{0,0},mode};
        Require(shader.push_constant_size == sizeof(params), "Camera constants layout mismatch");
        id<MTLCommandBuffer> commands = device.Commands("Camera motion");
        id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
        [encoder setComputePipelineState:pipeline];
        [encoder setTexture:depth atIndex:shader.FindBinding(0, 0).texture];
        [encoder setTexture:motion atIndex:shader.FindBinding(0, 1).texture];
        [encoder setTexture:object atIndex:shader.FindBinding(0, 2).texture];
        [encoder setBytes:&params length:sizeof(params) atIndex:PushConstantIndex];
        [encoder dispatchThreadgroups:MTLSizeMake(2, 1, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
        [encoder endEncoding];
        Device::SubmitAndWait(commands);
        const auto pixels = device.ReadTexture(motion);
        uint16_t center[2];
        std::memcpy(center, pixels.data() + (3 * width + 4) * sizeof(center), sizeof(center));
        Require(std::abs(Half(center[0]) - expected) < 0.002f && std::abs(Half(center[1])) < 0.001f,
            name);
    };
    constexpr float distant = 0.999995f;
    run("distant geometry moves", distant, 1000, 0, 0, 0, 4.5f * 1000 * (1 - distant) / 0.05f);
    run("sky rotates", 1, 0, 0.1f, 0, 0, 0.45f);
    run("sky ignores translation", 1, 1000, 0, 0, 0, 0);
    run("static camera cancels jitter", distant, 0, 0, 0, 0, 0, true);
    run("matching object motion", distant, 0, 0, distant, 1, 2);
    run("stale object motion is rejected", distant, 0, 0, 0.9999f, 1, 0);
    std::cout << "PASS: camera reprojection, sky, jitter and object-motion rejection (6 GPU cases)\n";
}
} // namespace

int main(int argc, char** argv) {
    @autoreleasepool {
        try {
            TranslationContracts();
            const auto shaders = HostShaders();
            if (argc == 2 && std::string(argv[1]) == "--translation-only") return 0;
            // A headless CI host may have no GPU. CTest records this as skipped.
            if (!MTLCreateSystemDefaultDevice()) {
                std::cerr << "SKIP: no accessible Metal device\n";
                return 77;
            }
            const Device device;
            std::cout << "Metal device: " << device.Native().name.UTF8String << '\n';
            CompileHostShaders(device, shaders);
            MultiCopy(device);
            PostProcess(device);
            CameraMotion(device);
        } catch (const std::exception& error) {
            std::cerr << "FAIL: " << error.what() << '\n';
            return 1;
        }
    }
}
