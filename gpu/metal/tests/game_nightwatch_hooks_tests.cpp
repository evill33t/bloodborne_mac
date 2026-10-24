// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only ELF fixture: install hooks in a disposable mapping. Never enters game code.
#include "gpu/metal/nightwatch/integration.h"
#include "gpu/metal/bbhost/features.h"
#include "gpu/metal/vendor/nightwatch/gpu/shim/party/coop_hooks.h"
#include "engine/addr.h"
#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
extern "C" void* runtime_low_map(std::size_t,int);
extern "C" void runtime_restart() { std::abort(); }
template<class T> T read(const std::vector<unsigned char>& bytes,std::size_t at) {
    assert(at+sizeof(T)<=bytes.size()); T result; std::memcpy(&result,bytes.data()+at,sizeof(T)); return result;
}
int main(int argc,char** argv) {
    if(argc!=2) return 77;
    std::ifstream f(argv[1],std::ios::binary);
    std::vector<unsigned char> elf(std::istreambuf_iterator<char>(f),{});
    assert(elf.size()>64 && !std::memcmp(elf.data(),"\177ELF",4));
    unsigned char digest[CC_SHA256_DIGEST_LENGTH]; CC_SHA256(elf.data(),CC_LONG(elf.size()),digest);
    char hash[65]{};for(int k=0;k<32;++k)std::snprintf(hash+k*2,3,"%02x",digest[k]);
    assert(engine_eboot_supported(hash));
    char temp[]="/tmp/bb-nightwatch-hooks-XXXXXX";assert(mkdtemp(temp));
    const auto root=std::filesystem::path(temp),user=root/"nightwatch";
    std::filesystem::create_directories(user);
    setenv("BB_DATA_DIR",temp,1);setenv("BB_MAC_BASE_USER_DIR",temp,1);
    setenv("BB_USER_DIR",user.c_str(),1);setenv("BB_GAME_DIR",(root/"game").c_str(),1);
    setenv("BB_MAC_EBOOT_SHA256",hash,1);setenv("BBHOST_PLUGINS","0",1);
    MacSettings::Values settings;settings.nightwatch_mode=1;
    if(const char* cap=std::getenv("BB_NIGHTWATCH_FIXTURE_FPS"))settings.frame_cap=std::atoi(cap);
    MacFeatures::Initialize(settings);MacNightwatch::Configure(settings);
    const auto headers=read<std::uint64_t>(elf,32);
    const auto stride=read<std::uint16_t>(elf,54),count=read<std::uint16_t>(elf,56);
    std::size_t size=0;
    for(unsigned k=0;k<count;++k) { const auto ph=headers+k*stride;
        if(read<std::uint32_t>(elf,ph)==1)size=std::max(size,std::size_t(read<std::uint64_t>(elf,ph+16)+read<std::uint64_t>(elf,ph+40)));
    }
    size=(size+4095)&~std::size_t(4095);
    auto image=static_cast<unsigned char*>(runtime_low_map(size+MacFeatures::ImageTailSize,PROT_READ|PROT_WRITE));assert(image);
    MacFeatures::BindImage(image,size);MacNightwatch::SetImage(image,size);
    for(unsigned k=0;k<count;++k) { const auto ph=headers+k*stride;
        if(read<std::uint32_t>(elf,ph)!=1)continue;
        const auto off=read<std::uint64_t>(elf,ph+8),va=read<std::uint64_t>(elf,ph+16);
        const auto files=read<std::uint64_t>(elf,ph+32),memory=read<std::uint64_t>(elf,ph+40);
        assert(off+files<=elf.size() && va+memory<=size);
        std::memcpy(image+va,elf.data()+off,files);
        MacFeatures::AddSegment(va,memory,read<std::uint32_t>(elf,ph+4));
    }
    MacFeatures::Install();
    unsigned char timing[32];std::memcpy(timing,image+0x2034770,sizeof(timing));
    // Exercise writes into sealed executable pages as in the real loader.
    assert(!mprotect(image,0x50d9000,PROT_READ|PROT_EXEC));
    MacNightwatch::InstallHooks();
    assert(!std::memcmp(timing,image+0x2034770,sizeof(timing)));
    const auto hooks=coop::InstalledHooks();
    assert(hooks.size()>50);
    assert(std::any_of(hooks.begin(),hooks.end(),[](const auto& s){return s.find("progress: SetEventFlag")!=std::string::npos;}));
    assert(std::none_of(hooks.begin(),hooks.end(),[](const auto& s){return s.find("main-thread tick")!=std::string::npos;}));
    MacFeatures::SetGameTick(nullptr);
    std::printf("Nightwatch: %zu checked hooks; shared %d FPS timing hook preserved; no game code executed\n",hooks.size(),settings.frame_cap);
    std::filesystem::remove_all(root);
}
