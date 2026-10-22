// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/mtl_compiler.h"
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {
void Usage() {
    std::puts("Usage: bb-macos-precompile-shaders [--data-dir DIRECTORY] [--threads 1..8]\n"
              "Precompile recorded Metal shader/pipeline recipes before gameplay.\n"
              "Defaults: BB_DATA_DIR or out/macos-data; four compiler workers.\n"
              "Only previously observed variants are available; no game is launched.");
}
void CheckData(const std::filesystem::path& data) {
    // A source dump or a child directory inside one is never a cache target.
    for(auto at=data;!at.empty();) {
        if(std::filesystem::exists(at/"eboot.bin"))
            throw std::runtime_error("The data directory must be outside the original game dump.");
        const auto parent=at.parent_path();if(parent==at)break;at=parent;
    }
}
}
int main(int argc,char** argv) { @autoreleasepool {
    try {
        const char* environment=std::getenv("BB_DATA_DIR");
        std::string data=environment && *environment ? environment : "out/macos-data";
        unsigned threads=4;
        for(int i=1;i<argc;++i) {
            const std::string_view option=argv[i];
            if(option=="--help" || option=="-h"){Usage();return 0;}
            if(option=="--data-dir" && i+1<argc){data=argv[++i];continue;}
            if(option=="--threads" && i+1<argc) {
                const std::string_view value=argv[++i];
                const auto parsed=std::from_chars(value.data(),value.data()+value.size(),threads);
                if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || threads<1 || threads>8)
                    throw std::runtime_error("--threads must be an integer from 1 to 8.");
                continue;
            }
            throw std::runtime_error("Unknown or incomplete option: "+std::string(option));
        }
        const auto directory=std::filesystem::canonical(data);
        if(!std::filesystem::is_directory(directory))throw std::runtime_error("The data directory does not exist.");
        CheckData(directory);
        if(::setenv("BB_DATA_DIR",directory.c_str(),1) || ::setenv("BB_METAL_SHADER_PREWARM","1",1))
            throw std::runtime_error("Could not configure shader precompilation.");
        auto device=MTLCreateSystemDefaultDevice();
        if(!device)throw std::runtime_error("No Metal device is available.");
        std::printf("Precompiling recorded shaders on %s (%u workers).\n",device.name.UTF8String,threads);
        const auto start=std::chrono::steady_clock::now();
        // DriverManaged never creates or serializes an explicit binary archive.
        Gpu::Metal::ShaderCompiler compiler(device,threads,Gpu::Metal::ShaderCompiler::CacheMode::DriverManaged);
        compiler.EnablePrewarming(true);
        auto state=compiler.GetWarmupStats();
        if(!state.loaded) {
            std::puts("No recorded pipeline recipes were found. Play an area once to collect its variants.");
            return 3;
        }
        auto next=start;
        while(state.queued) {
            const auto now=std::chrono::steady_clock::now();
            if(now>=next) {
                std::printf("Shaders: %llu/%llu ready, %llu queued.\n",
                    static_cast<unsigned long long>(state.ready),static_cast<unsigned long long>(state.loaded),
                    static_cast<unsigned long long>(state.queued));std::fflush(stdout);
                next=now+std::chrono::seconds(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            state=compiler.GetWarmupStats();
        }
        const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::printf("Precompile %s: %llu/%llu ready in %.2f seconds.\n",
            state.ready==state.loaded ? "complete" : "incomplete",
            static_cast<unsigned long long>(state.ready),static_cast<unsigned long long>(state.loaded),seconds);
        return state.ready==state.loaded ? 0 : 1;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"Shader precompile: %s\n",error.what());return 2;
    }
}}
