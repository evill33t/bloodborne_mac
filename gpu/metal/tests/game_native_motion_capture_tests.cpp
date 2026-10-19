// SPDX-License-Identifier: GPL-3.0-or-later
// Executes checked diagnostic trampolines on a private image with synthetic
// continuations. Never runs a game initializer, renderer, or original body.
#include "gpu/metal/native_motion_capture.h"
#include "core/elf.h"
#include "engine/addr.h"
#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

extern "C" void* runtime_low_map(std::size_t,int);
extern "C" void runtime_restart() { std::abort(); }
namespace {
template<class T> T Read(const std::vector<unsigned char>& bytes,std::size_t at) {
    assert(at<=bytes.size() && sizeof(T)<=bytes.size()-at);
    T value;std::memcpy(&value,bytes.data()+at,sizeof(value));return value;
}
template<class T,std::size_t N> void Put(std::array<unsigned char,N>& bytes,std::size_t at,const T& value) {
    assert(at<=bytes.size() && sizeof(value)<=bytes.size()-at);std::memcpy(bytes.data()+at,&value,sizeof(value));
}
constexpr std::uint64_t Result=0x5a392104beef7312ull;
void Continuation(unsigned char* output,bool wrapper) {
    // Store all six restored argument registers through the synthetic first
    // argument, return a nonzero sentinel, and unwind the exact native prologue.
    const unsigned char stores[]{0x48,0x89,0x3f,0x48,0x89,0x77,0x08,0x48,0x89,0x57,0x10,
        0x48,0x89,0x4f,0x18,0x4c,0x89,0x47,0x20,0x4c,0x89,0x4f,0x28,0x48,0xb8};
    std::memcpy(output,stores,sizeof(stores));output+=sizeof(stores);std::memcpy(output,&Result,8);output+=8;
    if (wrapper) {
        const unsigned char release[]{0x48,0x81,0xc4,0x88,0,0,0};
        std::memcpy(output,release,sizeof(release));output+=sizeof(release);
    } else {
        const unsigned char release[]{0x48,0x83,0xc4,0x08};
        std::memcpy(output,release,sizeof(release));output+=sizeof(release);
    }
    const unsigned char restore[]{0x5b,0x41,0x5c,0x41,0x5d,0x41,0x5e,0x41,0x5f,0x5d,0xc3};
    std::memcpy(output,restore,sizeof(restore));
}
}
int main(int argc,char** argv) {
    if (argc!=2) { std::puts("Pass a supported original eboot.elf for private-image diagnostic hook verification");return 77; }
    std::ifstream input(argv[1],std::ios::binary);assert(input);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),{});
    assert(bytes.size()>64 && !std::memcmp(bytes.data(),"\177ELF",4));
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256(bytes.data(),CC_LONG(bytes.size()),digest);
    char hash[65]{};for(unsigned i=0;i<32;++i) std::snprintf(hash+i*2,3,"%02x",digest[i]);
    assert(engine_eboot_supported(hash));
    const auto ph=Read<std::uint64_t>(bytes,32);
    const auto stride=Read<std::uint16_t>(bytes,54),count=Read<std::uint16_t>(bytes,56);
    assert(stride==56);
    std::size_t size=0;
    for(unsigned i=0;i<count;++i) if(Read<std::uint32_t>(bytes,ph+i*stride)==1)
        size=std::max(size,std::size_t(Read<std::uint64_t>(bytes,ph+i*stride+16)+Read<std::uint64_t>(bytes,ph+i*stride+40)));
    auto* memory=static_cast<unsigned char*>(runtime_low_map(size,PROT_READ|PROT_WRITE|PROT_EXEC));assert(memory);
    for(unsigned i=0;i<count;++i) if(Read<std::uint32_t>(bytes,ph+i*stride)==1) {
        const auto off=Read<std::uint64_t>(bytes,ph+i*stride+8),rva=Read<std::uint64_t>(bytes,ph+i*stride+16),n=Read<std::uint64_t>(bytes,ph+i*stride+32);
        assert(off<=bytes.size() && n<=bytes.size()-off && rva<=size && n<=size-rva);
        std::memcpy(memory+rva,bytes.data()+off,n);
    }
    constexpr std::size_t Wrapper=0xbb5430,Setter=0xbd3c50;
    std::array<unsigned char,20> wrapper{};std::memcpy(wrapper.data(),memory+Wrapper,wrapper.size());
    std::array<unsigned char,14> setter{};std::memcpy(setter.data(),memory+Setter,setter.size());
    Continuation(memory+Wrapper+wrapper.size(),true);Continuation(memory+Setter+setter.size(),false);
    ElfImage image;image.mem={memory,size,reinterpret_cast<std::uint64_t>(memory)};image.sha256=hash;
    const auto unchanged=[&] {
        return !std::memcmp(memory+Wrapper,wrapper.data(),wrapper.size()) && !std::memcmp(memory+Setter,setter.data(),setter.size());
    };
    using namespace Gpu::Metal::NativeMotionCapture;
    Observation observation;
    unsetenv("BB_METAL_CAPTURE_DIR");MacFeatures::InstallMotionCapture(&image);
    assert(!Enabled() && !Latest(observation) && unchanged());
    char temporary[]="/tmp/bb-native-motion-XXXXXX";assert(mkdtemp(temporary));
    const auto root=std::filesystem::path(temporary),game=root/"game",capture=root/"capture";
    std::filesystem::create_directories(game);
    setenv("BB_SOURCE_GAME",game.c_str(),1);setenv("BB_GAME_DIR",game.c_str(),1);
    setenv("BB_METAL_CAPTURE_DIR",capture.c_str(),1);
    image.sha256="unsupported";MacFeatures::InstallMotionCapture(&image);image.sha256=hash;
    assert(!Enabled() && unchanged() && !std::filesystem::exists(capture));
    // Both sites must pass preflight before either can be patched.
    memory[Setter]^=1;MacFeatures::InstallMotionCapture(&image);memory[Setter]^=1;
    assert(!Enabled() && unchanged());
    setenv("BB_METAL_CAPTURE_DIR",(game/"forbidden").c_str(),1);MacFeatures::InstallMotionCapture(&image);
    assert(!Enabled() && unchanged() && !std::filesystem::exists(game/"forbidden"));
    std::filesystem::create_directory_symlink(game,root/"game-alias");
    setenv("BB_METAL_CAPTURE_DIR",(root/"game-alias"/"forbidden").c_str(),1);MacFeatures::InstallMotionCapture(&image);
    assert(!Enabled() && unchanged() && !std::filesystem::exists(game/"forbidden"));
    setenv("BB_METAL_CAPTURE_DIR",capture.c_str(),1);MacFeatures::InstallMotionCapture(&image);
    assert(Enabled() && !unchanged() && !Latest(observation));
    std::array<unsigned char,0x180> object{},external{},internal{};
    const auto address=[](const auto& value) {return reinterpret_cast<std::uint64_t>(value.data());};
    Put(object,0x158,address(internal));
    Put(external,0x98,std::uint64_t(0x1234567890));Put(external,0x50,std::uint32_t(640));Put(external,0x54,std::uint32_t(360));
    Put(internal,0x98,std::uint64_t(0x2234567890));Put(internal,0x50,std::uint32_t(640));Put(internal,0x54,std::uint32_t(360));
    std::array<std::uint32_t,8> descriptor{0x12345678,0x09200080,0x0059c27f,0x90000688,0x4fe000,0,0,0};
    std::array<float,2> scale{.25f,.5f};
    using WrapperFn=std::uint64_t(*)(void*,std::uint64_t,void*,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,void*,std::uint64_t);
    using SetterFn=std::uint64_t(*)(void*,std::uint64_t,void*,std::uint64_t,void*,std::uint64_t);
    auto call_wrapper=reinterpret_cast<WrapperFn>(memory+Wrapper);
    auto call_setter=reinterpret_cast<SetterFn>(memory+Setter);
    const auto wrapper_args=std::array<std::uint64_t,6>{address(object),1,address(descriptor),640,360,0x17};
    assert(call_wrapper(object.data(),1,descriptor.data(),640,360,0x17,1,scale.data(),0x35)==Result);
    assert(!std::memcmp(object.data(),wrapper_args.data(),sizeof(wrapper_args)));
    const auto setter_args=std::array<std::uint64_t,6>{address(object),1,address(external),1,address(scale),0x35};
    assert(call_setter(object.data(),1,external.data(),1,scale.data(),0x35)==Result);
    assert(!std::memcmp(object.data(),setter_args.data(),sizeof(setter_args)));
    assert(Latest(observation) && observation.sequence==1 && observation.arguments==setter_args);
    assert(observation.registration_matched && observation.descriptor_readable && observation.source_descriptor==descriptor);
    assert(observation.source_width==640 && observation.source_height==360 && observation.source_format==0x17);
    assert(observation.scale_readable && observation.scale==scale && observation.flag==0x35);
    assert(observation.external.object==address(external) && observation.external.native_resource==0x1234567890);
    assert(observation.internal.object==address(internal) && observation.internal.native_resource==0x2234567890);
    // No new wrapper: consume neither the old descriptor nor a changed enum.
    assert(call_setter(object.data(),2,external.data(),0,scale.data(),0x35)==Result);
    assert(Latest(observation) && observation.sequence==2 && !observation.registration_matched && !observation.descriptor_readable);
    assert(call_wrapper(object.data(),1,descriptor.data(),640,360,0x17,1,scale.data(),0x35)==Result);
    assert(call_setter(object.data(),1,external.data(),2,scale.data(),0x35)==Result);
    assert(Latest(observation) && !observation.registration_matched);
    // Null guest inputs fail safe and the synthetic original still returns.
    assert(call_setter(object.data(),1,nullptr,1,nullptr,0)==Result);
    assert(Latest(observation) && !observation.external.readable && !observation.scale_readable);
    std::ifstream log(capture/"native-motion-observations.jsonl");assert(log);
    std::string line;unsigned lines=0;while(std::getline(log,line)) {assert(line.starts_with("{") && line.ends_with("}"));++lines;}
    assert(lines==4);
    std::filesystem::remove_all(root);munmap(memory,size);
    std::puts("Native motion observation: original prologues, trampoline register/stack preservation, descriptor correlation, safe reads and game-directory guards passed");
}
