// SPDX-License-Identifier: GPL-3.0-or-later
// CPU-only checks. Never enters the game's entry point or opens a window.
#include "gpu/metal/bbhost/features.h"
#include "gpu/metal/bbhost/timing_verify.h"
#include "engine/addr.h"
#include "engine/paramdef.h"
#include "engine/patch_manifest.h"
#include "core/elf.h"
#include "core/config.h"
#include "engine/frame_rate.h"
#include "engine/frame_rate_step.h"
#include "engine/sprj_flipper.h"
#include "core/thunk.h"
#include "hle/fs.h"
#include "host/plugins.h"
#include "bbhost/fixed_tick.hpp"
#include <CommonCrypto/CommonDigest.h>
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

extern "C" void* runtime_low_map(std::size_t,int);
extern "C" void runtime_file_configure(const char*,const char*);
extern "C" int runtime_file_translate(const char*,char*,std::size_t);
extern "C" void runtime_restart() { std::abort(); }
template<class T> T value(const std::vector<unsigned char>& bytes,std::size_t offset) {
    assert(offset+sizeof(T)<=bytes.size()); T result; std::memcpy(&result,bytes.data()+offset,sizeof(T)); return result;
}
int main(int argc,char** argv) {
    char temp[]="/tmp/bb-mac-features-XXXXXX"; assert(mkdtemp(temp));
    const auto root=std::filesystem::path(temp),game=root/"game",user=root/"user",mods=root/"mods";
    std::filesystem::create_directories(game/"dvdroot_ps4/menu");
    std::filesystem::create_directories(mods/"dvdroot_ps4/menu");
    std::ofstream(game/"dvdroot_ps4/menu/test") << "original";
    std::ofstream(mods/"dvdroot_ps4/menu/test") << "mod";
    setenv("BB_DATA_DIR",temp,1); setenv("BB_GAME_DIR",argc>3 ? argv[3] : game.c_str(),1);
    setenv("BB_MAC_SETTINGS",(root/"macos-settings.ini").c_str(),1);
    setenv("BB_SOURCE_GAME",argc>3 ? argv[3] : game.c_str(),1); setenv("BB_MAC_MOD_OVERLAY",mods.c_str(),1);
    const bool bundled_plugins=argc>4 && std::string(argv[4])=="--plugins";
    setenv("BBHOST_PLUGINS",bundled_plugins ? "1" : "0",1);
    if (bundled_plugins) std::ofstream(root/"bbhost.toml") <<
        "[plugins]\njoin_modded_worlds = false\nrandomizer = true\nboss_rush = true\nmutators = true\ndebug_menu = true\n";
    MacSettings::Values settings;
    if (argc>2) settings.frame_cap=std::atoi(argv[2]);
    MacFeatures::Initialize(settings);
    const auto discovered=plugins_scan();
    for (const char* name:{"randomizer","boss_rush","mutators","debug_menu"})
        assert(std::any_of(discovered.begin(),discovered.end(),[&](const auto& p) { return p.name==name && !p.options.empty(); }));
    runtime_file_configure(game.c_str(),user.c_str());
    char path[4096]{}; assert(runtime_file_translate("/app0/dvdroot_ps4/menu/test",path,sizeof(path))==0);
    assert(std::filesystem::equivalent(path,mods/"dvdroot_ps4/menu/test"));
    assert(runtime_file_translate("/app0/../outside",path,sizeof(path))!=0);
    assert(!hle_fs_game_file("dvdroot_ps4/menu/test").empty());
    assert(!hle_fs_game_file("/dvdroot_ps4/menu/test").empty());
    assert(std::filesystem::file_size(game/"dvdroot_ps4/menu/test")==8);
    assert(MacFeatures::RequestSettings(settings));
    MacSettings::Values received; assert(MacFeatures::TakeSettings(received) && received==settings);
    unsigned char row[8]{0xa1}; ParamField field;
    field.type="u8"; field.count=1; field.bytes=1; field.bit=1; field.bits=3;
    assert(paramdef_write(field,row,"3") && row[0]==0xa7 && paramdef_read(field,row)=="3");
    assert(!paramdef_write(field,row,"2",1));
    // Multi-site manifests refuse the whole change if any expected byte differs.
    auto patch_memory=static_cast<unsigned char*>(runtime_low_map(4096,PROT_READ|PROT_WRITE)); assert(patch_memory);
    patch_memory[16]=0x11; patch_memory[32]=0x44;
    ElfImage patch_image; patch_image.sha256=kEboot109Sha256;
    patch_image.mem={patch_memory,4096,reinterpret_cast<std::uint64_t>(patch_memory)};
    std::filesystem::create_directories(mods/"patches");
    const auto manifest=[&](const char* second) {
        std::ofstream file(mods/"patches/fixture.toml");
        file << "[patch]\nname = \"fixture\"\neboot = \"" << kEboot109Sha256 << "\"\n"
            "[site.first]\naddress = \"0x400010\"\nexpect = \"11\"\nbytes = \"22\"\nkind = \"data\"\n"
            "[site.second]\naddress = \"0x400020\"\nexpect = \"" << second << "\"\nbytes = \"55\"\nkind = \"data\"\n";
    };
    manifest("33"); patch_manifests_apply(&patch_image);
    assert(patch_memory[16]==0x11 && patch_memory[32]==0x44);
    manifest("44"); patch_manifests_apply(&patch_image);
    assert(patch_memory[16]==0x22 && patch_memory[32]==0x55);
    std::filesystem::remove_all(mods/"patches"); munmap(patch_memory,4096);
    for (float fps:{30,60,90,120,144,165,240}) {
        const auto weight=frame_rate_step::weight(.1f,30/fps);
        assert(std::abs(std::pow(1-weight,fps)-std::pow(.9f,30))<.00001f);
        bb::FixedTicks ticks; int count=0;
        for (int k=0;k<int(fps)*10;++k) count+=ticks.advance(1.0/fps);
        assert(count==600);
    }
    if (argc>1) {
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<unsigned char> elf(std::istreambuf_iterator<char>(input),{});
        assert(elf.size()>64 && std::memcmp(elf.data(),"\177ELF",4)==0);
        unsigned char digest[CC_SHA256_DIGEST_LENGTH]; CC_SHA256(elf.data(),CC_LONG(elf.size()),digest);
        char hash[65]{}; for (int k=0;k<32;++k) std::snprintf(hash+k*2,3,"%02x",digest[k]);
        assert(engine_eboot_supported(hash)); setenv("BB_MAC_EBOOT_SHA256",hash,1);
        const auto headers=value<std::uint64_t>(elf,32);
        const auto stride=value<std::uint16_t>(elf,54),count=value<std::uint16_t>(elf,56);
        std::uint64_t image_size=0;
        for (unsigned k=0;k<count;++k) {
            const auto ph=headers+k*stride;
            if (value<std::uint32_t>(elf,ph)==1) image_size=std::max(image_size,value<std::uint64_t>(elf,ph+16)+value<std::uint64_t>(elf,ph+40));
        }
        image_size=(image_size+4095)&~4095ull;
        auto image=static_cast<unsigned char*>(runtime_low_map(image_size+MacFeatures::ImageTailSize,PROT_READ|PROT_WRITE)); assert(image);
        MacFeatures::BindImage(image,image_size);
        for (unsigned k=0;k<count;++k) {
            const auto ph=headers+k*stride; if (value<std::uint32_t>(elf,ph)!=1) continue;
            const auto offset=value<std::uint64_t>(elf,ph+8),va=value<std::uint64_t>(elf,ph+16);
            const auto files=value<std::uint64_t>(elf,ph+32),memory=value<std::uint64_t>(elf,ph+40);
            assert(offset+files<=elf.size() && va+memory<=image_size);
            std::memcpy(image+va,elf.data()+offset,files);
            MacFeatures::AddSegment(va,memory,value<std::uint32_t>(elf,ph+4));
        }
        MacFeatures::Install();
        // A matching expected byte inside a bypassed function must still be
        // refused, and must not partially apply an earlier safe site.
        assert(MacTimingVerify::Conflicts(0x024347b0, 1));
        assert(!MacTimingVerify::Conflicts(0x400010, 1));
        std::filesystem::create_directories(mods/"patches");
        const auto guard_file=mods/"patches/timing-guard.toml";
        const auto safe_before=image[0x10],timing_before=image[0x024347b0-0x400000];
        {
            std::ofstream file(guard_file);
            file << "[patch]\nname = \"timing-guard\"\neboot = \"" << hash << "\"\n";
            char bytes[3]{};
            const auto site=[&](const char* name,std::uint64_t address,unsigned char was) {
                file << "[site." << name << "]\naddress = \"0x" << std::hex << address << "\"\n";
                std::snprintf(bytes,sizeof(bytes),"%02x",was); file << "expect = \"" << bytes << "\"\n";
                std::snprintf(bytes,sizeof(bytes),"%02x",was^0xff); file << "bytes = \"" << bytes << "\"\n";
            };
            site("a_safe",0x400010,safe_before); site("b_timing",0x024347b0,timing_before);
        }
        ElfImage guarded_image; guarded_image.sha256=hash;
        guarded_image.mem={image,std::size_t(image_size+MacFeatures::ImageTailSize),reinterpret_cast<std::uint64_t>(image)};
        patch_manifests_apply(&guarded_image);
        assert(image[0x10]==safe_before && image[0x024347b0-0x400000]==timing_before);
        std::filesystem::remove(guard_file);
        if (bundled_plugins) {
            const auto catalog=plugins_catalog();
            for (const char* name:{"randomizer","boss_rush","mutators","debug_menu"}) {
                assert(plugins_active(name));
                assert(std::any_of(catalog.begin(),catalog.end(),[&](const auto& p) { return p.name==name && p.loaded && p.enabled; }));
            }
            std::printf("Bundled plugins loaded and their image callbacks completed; ruleset: %s\n",plugins_ruleset().c_str());
            plugins_set_option("mutators","speed","1.0");
            std::map<std::string,std::string> saved; std::string error;
            assert(config_parse_toml((root/"bbhost.toml").string(),&saved,&error));
            assert(saved["mutators.speed"]=="1.0"); // persisted before the next frame or a restart
        }
        if (argc>3) {
            assert(std::filesystem::is_regular_file(root/"bbhost/menu-assets/dvdroot_ps4/menu/optionsetting.gfx"));
            assert(!hle_fs_overlay_file("/dvdroot_ps4/menu/optionsetting.gfx").empty());
            const auto section_count=*(image+0x1f20e21-0x400000);
            assert(section_count==7); // original six plus the Mac section-name slot
        }
        const auto expected=settings.frame_cap>120 ? 0 : settings.frame_cap;
        assert(frame_rate_game_fps()==expected);
        // Execute only the patched frame manager against a fixture, never game startup.
        SprjFlipper flipper{}; flipper.primary_flip_mode_raw=2;
        for (int k=0;k<3;++k) hle_call_guest<std::int64_t>(image+0x2434770-0x400000,&flipper);
        if (expected!=30) assert(frame_rate_step::g_frame_seconds.load()>0 && frame_rate_step::g_frame_seconds.load()<=1.0f/30);
        std::printf("ELF %s: timing mode %d installed and its frame manager executed\n",hash,settings.frame_cap);
    }
    std::filesystem::remove_all(root);
    std::puts("Mac feature services: overlays, settings bridge, named bit fields and timing invariance passed");
}
