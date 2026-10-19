// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in observation of native velocity registration. Guest behavior is kept.
#include "gpu/metal/native_motion_capture.h"
#include "core/elf.h"
#include "core/host_clock.h"
#include "core/portable.h"
#include "core/thunk.h"
#include "engine/addr.h"
#include "engine/graphics_patch.h"
#include "log.h"
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>

namespace Gpu::Metal::NativeMotionCapture {
namespace {
std::atomic<bool> enabled{false};
std::atomic<std::uint64_t> registrations{0};
std::mutex mutex;
Observation latest;
std::FILE* log_file=nullptr;
unsigned log_records=0;

template<class T> bool Read(std::uint64_t address,T& value) {
    return address && sizeof(T)<=std::numeric_limits<std::uint64_t>::max()-address &&
        host_read_safe(reinterpret_cast<const void*>(address),&value,sizeof(value));
}
template<class T> bool Field(std::uint64_t object,std::uint64_t offset,T& value) {
    return object && offset<=std::numeric_limits<std::uint64_t>::max()-object && Read(object+offset,value);
}
Texture ReadTexture(std::uint64_t object) {
    Texture texture;texture.object=object;
    // YEBIS GPUTexture::Initialize (RVA c0b370) stores the retained native
    // resource at +98. Consumers c35b30/c40a20 read dimensions at +50/+54.
    const bool resource=Field(object,0x98,texture.native_resource);
    const bool width=Field(object,0x50,texture.width);
    const bool height=Field(object,0x54,texture.height);
    texture.readable=resource && width && height;
    return texture;
}
struct Registration {
    std::uint64_t sequence=0,monotonic_ns=0,descriptor=0,scale_address=0;
    std::int32_t mode=0,velocity_format=0;
    std::uint32_t width=0,height=0,format=0;
    std::uint8_t flag=0;
    std::array<std::uint32_t,8> words{};
    bool readable=false,pending=false;
};
thread_local Registration registration;

std::int64_t ObserveRegistration(std::uint64_t,const std::uint64_t* saved) noexcept {
    try {
        if (!enabled.load(std::memory_order_acquire) || !saved) return 0;
        Registration item;
        item.sequence=registrations.fetch_add(1,std::memory_order_relaxed)+1;
        item.monotonic_ns=host_clock_monotonic_ns();
        // thunk_emit_prologue_stub supplies r9,r8,rcx,rdx,rsi,rdi.
        item.mode=std::int32_t(saved[4]);item.descriptor=saved[3];
        item.width=std::uint32_t(saved[2]);item.height=std::uint32_t(saved[1]);
        item.format=std::uint32_t(saved[0]);
        // saved[6] is the original return address. Stack args 7..9 are
        // velocity enum, scale pointer and flag; read without dereferencing
        // an unchecked guest pointer. This entry is bb5430's first byte.
        std::array<std::uint64_t,3> stack{};
        if (Read(reinterpret_cast<std::uint64_t>(saved+7),stack)) {
            item.velocity_format=std::int32_t(stack[0]);
            item.scale_address=stack[1];item.flag=std::uint8_t(stack[2]);
            item.pending=true;
        }
        item.readable=Read(item.descriptor,item.words);
        registration=item;
    } catch (...) { registration={}; }
    return 0; // Always run the displaced original prologue and function.
}
void LogTexture(const char* name,const Texture& texture) {
    std::fprintf(log_file,"\"%s\":{\"object\":\"0x%llx\",\"native_resource\":\"0x%llx\","
        "\"width\":%u,\"height\":%u,\"readable\":%s}",name,
        static_cast<unsigned long long>(texture.object),static_cast<unsigned long long>(texture.native_resource),
        texture.width,texture.height,texture.readable?"true":"false");
}
void Log(const Observation& item) {
    if (!log_file || log_records>=256) return;
    ++log_records;
    std::fprintf(log_file,"{\"kind\":\"native-velocity-registration\",\"sequence\":%llu,\"monotonic_ns\":%llu,"
        "\"setter_rva\":\"0xbd3c50\",\"setter_object\":\"0x%llx\",\"mode\":%d,\"velocity_format\":%d,\"flag\":%u,"
        "\"scale_readable\":%s,\"scale_bits\":[\"0x%08x\",\"0x%08x\"],\"scale\":[",
        static_cast<unsigned long long>(item.sequence),static_cast<unsigned long long>(item.monotonic_ns),
        static_cast<unsigned long long>(item.setter_object),item.mode,item.velocity_format,unsigned(item.flag),
        item.scale_readable?"true":"false",std::bit_cast<std::uint32_t>(item.scale[0]),std::bit_cast<std::uint32_t>(item.scale[1]));
    for (unsigned i=0;i<2;++i) {
        if (i) std::fputc(',',log_file);
        if (item.scale_readable && std::isfinite(item.scale[i])) std::fprintf(log_file,"%.9g",double(item.scale[i]));
        else std::fputs("null",log_file);
    }
    std::fputs("],\"arguments\":[",log_file);
    for (unsigned i=0;i<6;++i) std::fprintf(log_file,"%s\"0x%llx\"",i?",":"",static_cast<unsigned long long>(item.arguments[i]));
    std::fputs("],",log_file);LogTexture("external",item.external);std::fputc(',',log_file);LogTexture("internal",item.internal);
    std::fprintf(log_file,",\"registration_matched\":%s,\"registration_sequence\":%llu,\"descriptor_readable\":%s,"
        "\"descriptor_address\":\"0x%llx\",\"source_width\":%u,\"source_height\":%u,\"source_format\":%u,\"source_descriptor\":[",
        item.registration_matched?"true":"false",static_cast<unsigned long long>(item.registration_sequence),
        item.descriptor_readable?"true":"false",static_cast<unsigned long long>(item.descriptor_address),
        item.source_width,item.source_height,item.source_format);
    for (unsigned i=0;i<8;++i) std::fprintf(log_file,"%s\"0x%08x\"",i?",":"",item.source_descriptor[i]);
    std::fputs("],\"scope\":\"CPU registration; GPU frame association and vector units unvalidated\"}\n",log_file);
    std::fflush(log_file);
}
std::int64_t ObserveSetter(std::uint64_t,const std::uint64_t* saved) noexcept {
    try {
        if (!enabled.load(std::memory_order_acquire) || !saved) return 0;
        Observation item;
        item.monotonic_ns=host_clock_monotonic_ns();
        for (unsigned i=0;i<6;++i) item.arguments[i]=saved[5-i];
        item.setter_object=item.arguments[0];item.mode=std::int32_t(item.arguments[1]);
        item.velocity_format=std::int32_t(item.arguments[3]);item.flag=std::uint8_t(item.arguments[5]);
        item.scale_readable=Read(item.arguments[4],item.scale);
        item.external=ReadTexture(item.arguments[2]);
        std::uint64_t internal=0;Field(item.setter_object,0x158,internal);item.internal=ReadTexture(internal);
        // Consume one same-thread wrapper observation. The original setter can
        // also be called through other paths; never attach stale descriptors
        // to those merely because their resolved object looks texture-like.
        const auto entry=registration;registration={};
        if (entry.pending && entry.mode==item.mode && entry.velocity_format==item.velocity_format &&
            entry.scale_address==item.arguments[4] && entry.flag==item.flag &&
            item.monotonic_ns>=entry.monotonic_ns && item.monotonic_ns-entry.monotonic_ns<=1000000000ull) {
            item.registration_matched=true;item.registration_sequence=entry.sequence;
            item.descriptor_address=entry.descriptor;item.source_descriptor=entry.words;
            item.source_width=entry.width;item.source_height=entry.height;item.source_format=entry.format;
            item.descriptor_readable=entry.readable;
        }
        std::scoped_lock lock(mutex);
        item.sequence=latest.sequence+1;
        const bool changed=item.mode!=latest.mode || item.velocity_format!=latest.velocity_format ||
            item.external.object!=latest.external.object || item.internal.object!=latest.internal.object ||
            item.source_descriptor!=latest.source_descriptor || item.registration_matched!=latest.registration_matched;
        latest=item;
        if (item.sequence<=16 || changed || item.sequence%120==0) Log(item);
    } catch (...) { /* Diagnostics must not interrupt the original setter. */ }
    return 0;
}
}
bool Enabled() { return enabled.load(std::memory_order_acquire); }
bool Latest(Observation& observation) {
    if (!Enabled()) return false;
    std::scoped_lock lock(mutex);
    if (!latest.sequence) return false;
    observation=latest;return true;
}
}

namespace MacFeatures {
void InstallMotionCapture(ElfImage* image) {
    using namespace Gpu::Metal::NativeMotionCapture;
    const char* directory=std::getenv("BB_METAL_CAPTURE_DIR");
    if (!directory || !*directory || !image || !engine_eboot_supported(image->sha256) || Enabled()) return;
    try {
        // Both are complete non-RIP-relative prologues, verified against the
        // supported loaded image. bb5430 supplies the original S#; bd3c50
        // supplies the resolved objects. No native resource method is called.
        static constexpr std::uint8_t wrapper[]{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,
            0x41,0x54,0x53,0x48,0x81,0xec,0x88,0,0,0};
        static constexpr std::uint8_t setter[]{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,
            0x41,0x54,0x53,0x50};
        const auto matches=[&](std::uint64_t rva,const auto& bytes) {
            if (rva>image->mem.size || sizeof(bytes)>image->mem.size-rva ||
                rva>std::numeric_limits<std::uint64_t>::max()-image->mem.slide) return false;
            std::array<std::uint8_t,sizeof(bytes)> current{};
            return Read(image->mem.slide+rva,current) && !std::memcmp(current.data(),bytes,sizeof(bytes));
        };
        if (!matches(0xbb5430,wrapper) || !matches(0xbd3c50,setter)) {
            host_log("Native velocity capture refused: expected prologue differs");return;
        }
        std::error_code error;
        const auto root=std::filesystem::weakly_canonical(directory,error);
        if (error) { host_log("Native velocity capture: cannot resolve capture directory");return; }
        for (const char* key:{"BB_SOURCE_GAME","BB_GAME_DIR"}) {
            const char* game=std::getenv(key);
            if (!game || !*game) continue;
            const auto source=std::filesystem::weakly_canonical(game,error);
            if (error) { host_log("Native velocity capture: cannot resolve game directory");return; }
            const auto relative=root.lexically_relative(source);
            if (root==source || (!relative.empty() && *relative.begin()!="..")) {
                host_log("Native velocity capture refused: output is inside a game directory");return;
            }
        }
        const auto path=root/"native-motion-observations.jsonl";
        std::filesystem::create_directories(path.parent_path(),error);
        if (error) { host_log("Native velocity capture: cannot create capture directory");return; }
        auto* output=std::fopen(path.c_str(),"a");
        if (!output) { host_log("Native velocity capture: cannot open observation log");return; }
        if (!engine_prologue_hook(image,image->mem.slide+0xbb5430,wrapper,sizeof(wrapper),reinterpret_cast<void*>(&ObserveRegistration)) ||
            !engine_prologue_hook(image,image->mem.slide+0xbd3c50,setter,sizeof(setter),reinterpret_cast<void*>(&ObserveSetter))) {
            std::fclose(output);host_log("Native velocity capture refused: checked hook installation failed");return;
        }
        { std::scoped_lock lock(mutex);log_file=output; }
        enabled.store(true,std::memory_order_release);
        host_log("Native velocity capture installed: observation only; up to 256 log records");
    } catch (...) { host_log("Native velocity capture unavailable: diagnostic setup failed"); }
}
}
