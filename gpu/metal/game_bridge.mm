// SPDX-License-Identifier: GPL-2.0-or-later
// Direct Metal / PM4 integration. NID hashing follows the vendored HLE registry.
#include "gpu/bbgpu.h"
#include "src/runtime_gpu_memory.h"
#include "src/checkpoint/runtime.h"
#include "src/checkpoint/graphics.h"
#include "gpu/metal/frontend_snapshot.h"
#include "gpu/metal/mtl_device.h"
#include "gpu/metal/mtl_renderer.h"
#include "gpu/metal/mtl_presenter.h"
#include "gpu/metal/game_overlay.h"
#include "gpu/metal/game_achievements.h"
#include "gpu/metal/game_input.h"
#include "gpu/metal/bbhost/features.h"
#include "gpu/metal/bbhost/network.h"
#include "gpu/metal/frame_handoff.h"
#include "gpu/metal/frame_generation.h"
#import <QuartzCore/QuartzCore.h>
#include "gpu/metal/stutter_trace.h"
#include "gpu/metal/wind_capture_contract.h"
#include "hle/video_out/video_out.h"
#include "gpu/liverpool/command_processor.h"
#include "core/memory.h"
#include "common/sha1.h"
#include "hle/hle.h"
#include "hle/kernel/kernel.h"
#include "hle/kernel/equeue.h"
#include "hle/gnmdriver/gnmdriver.h"
#include "hle/video_out/video_out.h"
#include "gpu/shadps4/core/libraries/gnmdriver/gnmdriver_init.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <algorithm>
#include <mach/mach.h>
#include <mach/mach_vm.h>

extern "C" {
int runtime_memory_region(uintptr_t address,uintptr_t *start,uintptr_t *end,int *mapped);
int32_t *runtime_errno(void);
}
namespace {
std::unordered_map<std::string,void*> symbols;
std::unordered_map<std::string,void*> feature_symbols;
std::unique_ptr<Gpu::Metal::Device> device;
std::unique_ptr<Gpu::Metal::MetalRenderer> renderer;
struct GuestMappingHooks {
    bool active=false;
    void Remove() {
        if(active) runtime_gpu_memory_set_mutation_hooks(nullptr,nullptr,nullptr);
        active=false;
    }
    ~GuestMappingHooks() {Remove();}
} guest_mapping_hooks; // removed before the renderer's static destruction
std::atomic<bool> ready{false};
#ifndef BB_CPU_TRANSLATOR
#include "gpu/metal/game_checkpoint.inc"
#endif
std::atomic<float> display_refresh{60};
Gpu::Metal::FrameHandoff frame_handoff;
Uint32 frame_event=0;
using Queue=Hle::Kernel::EventQueue;
std::mutex queue_lock;
std::map<uint32_t,std::shared_ptr<Queue>> queues;
// Retain retired queues until the CP is drained; listeners hold raw pointers.
std::vector<std::shared_ptr<Queue>> retired_queues;
uint32_t next_queue=1;
std::shared_ptr<Queue> GetQueue(uint32_t id) {
    std::scoped_lock lock(queue_lock);
    auto it=queues.find(id);
    return it==queues.end() ? nullptr : it->second;
}
int32_t CreateQueue(uint32_t* out,const char* name) {
    if (!out || !name) return int32_t(0x80020016);
    std::scoped_lock lock(queue_lock);
    const auto id=next_queue++;
    queues[id]=std::make_shared<Queue>(name);
    *out=id;
    return 0;
}
int32_t DeleteQueue(uint32_t id) {
    std::scoped_lock lock(queue_lock);
    const auto it=queues.find(id);
    if (it==queues.end()) return int32_t(0x80020009);
    retired_queues.push_back(it->second);
    queues.erase(it);
    return 0;
}
int32_t WaitQueue(uint32_t id,Hle::Kernel::GuestKevent* events,int count,int* returned,const uint32_t* timeout) {
    const auto queue=GetQueue(id);
    if (!queue) return int32_t(0x80020009);
    if (!events || count<=0 || !returned) return int32_t(0x80020016);
    *returned=queue->Wait(events,count,timeout);
    return !*returned && timeout ? int32_t(0x8002003c) : 0;
}
int32_t AddUserEvent(uint32_t id,uint64_t event) {
    const auto queue=GetQueue(id);
    if (!queue) return int32_t(0x80020009);
    queue->Add(event,Hle::Kernel::kEvfiltUser,nullptr);
    return 0;
}
int32_t TriggerUserEvent(uint32_t id,uint64_t event,void* data) {
    const auto queue=GetQueue(id);
    return queue && queue->Trigger(event,Hle::Kernel::kEvfiltUser,int64_t(uintptr_t(data))) ? 0 : int32_t(0x80020002);
}
int32_t DeleteUserEvent(uint32_t id,uint64_t event) {
    const auto queue=GetQueue(id);
    return queue && queue->Remove(event,Hle::Kernel::kEvfiltUser) ? 0 : int32_t(0x80020002);
}
using GnmAdd=int32_t (*)(Queue*,uint64_t,void*);
using GnmDelete=int32_t (*)(Queue*,uint64_t);
using VideoAdd=int32_t (*)(Queue*,int32_t,void*);
GnmAdd gnm_add;
GnmDelete gnm_delete;
VideoAdd video_flip_add,video_vblank_add;
int32_t GnmAddEvent(uint32_t id,uint64_t event,void* data) {
    const auto queue=GetQueue(id);
    return queue ? gnm_add(queue.get(),event,data) : int32_t(0x80020009);
}
int32_t GnmDeleteEvent(uint32_t id,uint64_t event) {
    const auto queue=GetQueue(id);
    return queue ? gnm_delete(queue.get(),event) : int32_t(0x80020009);
}
int32_t VideoFlipEvent(uint32_t id,int32_t handle,void* data) {
    const auto queue=GetQueue(id);
    return queue ? video_flip_add(queue.get(),handle,data) : int32_t(0x80020009);
}
int32_t VideoVblankEvent(uint32_t id,int32_t handle,void* data) {
    const auto queue=GetQueue(id);
    return queue ? video_vblank_add(queue.get(),handle,data) : int32_t(0x80020009);
}
template<class Fn> void Bind(const char* name,Fn fn) {
    symbols[Hle::Nid(name)]=reinterpret_cast<void*>(fn);
}
uint32_t HardwareState200(uint32_t* commands,uint32_t size) {
    if (!commands || size<256) return 0;
    static constexpr uint32_t clear[]{0xc0012800u,0x80000000u,0x80000000u,0xc0001200u,0,
        0xc0055800u,0x2ec47fc0u,0xffffffffu,0,0,0,10};
    uint32_t* next=commands;
    memcpy(next,clear,sizeof(clear)); next+=std::size(clear);
    const auto& sequence=Libraries::GnmDriver::InitSequence200;
    memcpy(next,sequence.data()+2,(sequence.size()-2)*4); next+=sequence.size()-2;
    Gpu::Liverpool::WriteNop(next,uint32_t(commands+256-next-1));
    return 256;
}
int32_t VideoGetFlipStatus(int32_t handle,Hle::VideoOut::FlipStatus* status) {
    return Hle::VideoOut::Driver::Instance().GetFlipStatus(handle,status);
}
int32_t VideoClose(int32_t handle) { return Hle::VideoOut::Driver::Instance().Close(handle); }
void* VideoPlayerInit(const void*) {
    // AvPlayer's documented failure result. Bloodborne handles this branch;
    // cinematic decoding has not been ported to AVFoundation yet.
    fprintf(stderr,"Metal port: video decoder unavailable (sceAvPlayerInit failed)\n");
    return nullptr;
}
int32_t VideoSubmitFlip(int32_t handle,int32_t index,int32_t mode,int64_t argument) {
    Hle::VideoOut::FlipStatus status;
    auto& driver=Hle::VideoOut::Driver::Instance();
    const int32_t error=driver.GetFlipStatus(handle,&status);
    if (error) return error;
    if (index < -1 || index >= 16 || (index>=0 && !driver.GetFlipInfo(handle,index).address))
        return int32_t(0x80c9000a);
    if (mode<1 || mode>3) return int32_t(0x80c9000d);
    using Request=int32_t (*)(uint32_t,uint32_t,int64_t);
    driver.MarkFlipPending(handle,index);
    return reinterpret_cast<Request>(Hle::Lookup({},Hle::Nid("sceGnmRequestFlipAndSubmitDone")))(handle,index,argument);
}
} // namespace

namespace Hle {
std::string Nid(std::string_view name) {
    static constexpr uint8_t salt[]{0x51,0x8d,0x64,0xa6,0x35,0xde,0xd8,0xc1,
        0xe6,0xb0,0x39,0xb1,0xc3,0xe5,0x52,0x30};
    std::string input(name);
    input.append(reinterpret_cast<const char*>(salt),sizeof(salt));
    const auto digest=Common::Sha1(reinterpret_cast<const u8*>(input.data()),input.size());
    uint64_t value=0;
    for (int i=7;i>=0;--i) value=(value<<8)|digest[i];
    static constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";
    std::string result(11,'\0');
    for (int i=0;i<11;++i) {
        const int shift=58-6*i;
        result[i]=alphabet[(shift>=0 ? value>>shift : value<<-shift)&63];
    }
    return result;
}
std::string_view NameOf(std::string_view nid) { return nid; }
void Register(std::string_view,std::string_view nid,void* function) { symbols[std::string(nid)]=function; }
void* Lookup(std::string_view,std::string_view nid) {
    const auto it=symbols.find(std::string(nid));
    return it==symbols.end() ? nullptr : it->second;
}
namespace Kernel { int* GuestErrno() { return runtime_errno(); } }
} // namespace Hle

namespace Core::Memory {
u64 RuntimeDirectSize() { return runtime_gpu_memory_size(); }
void* DirectHostView() { return runtime_gpu_memory_backing(); }
u64 DirectMapGeneration() { return runtime_gpu_memory_generation(); }
std::vector<DirectRange> DirectMappings() {
    std::vector<BbDirectMapping> mappings(runtime_gpu_memory_mappings(nullptr,0));
    for (;;) {
        const size_t count=runtime_gpu_memory_mappings(mappings.data(),mappings.size());
        if (count<=mappings.size()) { mappings.resize(count); break; }
        mappings.resize(count);
    }
    std::vector<DirectRange> result;
    result.reserve(mappings.size());
    for (const auto& range:mappings) result.push_back({range.start,range.size,range.physical});
    return result;
}
int AllocateDirect(u64 start,u64 end,u64 size,u64 alignment,int type,u64* physical) {
    return runtime_gpu_memory_allocate(start,end,size,alignment,type,physical);
}
int MapDirect(VAddr* address,u64 size,int prot,int flags,u64 physical,u64 alignment,const char*) {
    uintptr_t pointer=uintptr_t(*address);
    int error=runtime_gpu_memory_map(&pointer,size,prot,flags,physical,alignment);
    if (!error) *address=pointer;
    return error;
}
bool Query(VAddr address,bool find_next,Vma* out) {
    uintptr_t start=0,end=0;
    int mapped=0;
    if (runtime_memory_region(uintptr_t(address),&start,&end,&mapped) && mapped) {
        *out=Vma{.start=start,.size=end-start,.prot=3,.type=VmaType::Direct};
        return true;
    }
    mach_vm_address_t at=address;
    mach_vm_size_t size=0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object=MACH_PORT_NULL;
    const auto error=mach_vm_region(mach_task_self(),&at,&size,VM_REGION_BASIC_INFO_64,
        reinterpret_cast<vm_region_info_t>(&info),&count,&object);
    if (object) mach_port_deallocate(mach_task_self(),object);
    if (error!=KERN_SUCCESS || (!find_next && at>address) || !(info.protection&VM_PROT_READ)) return false;
    *out=Vma{.start=at,.size=size,.prot=info.protection,.type=VmaType::Direct};
    return true;
}
} // namespace Core::Memory

extern "C" void bbgpu_register_kernel(void) {
    Hle::Kernel::RegisterSync();
    Hle::GnmDriver::RegisterGnmDriver();
    Hle::VideoOut::RegisterVideoOut();
    Bind("sceGnmDrawInitDefaultHardwareState200",HardwareState200);
    // The sequential PM4 processor applies context writes directly, including
    // the context updates encoded as magic NOPs by the console's Update APIs.
    for (const auto& pair : {std::pair{"sceGnmUpdatePsShader","sceGnmSetPsShader"},
         {"sceGnmUpdateVsShader","sceGnmSetVsShader"},
         {"sceGnmUpdateHsShader","sceGnmSetHsShader"},
         {"sceGnmUpdateGsShader","sceGnmSetGsShader"},
         {"sceGnmMapComputeQueueWithPriority","sceGnmMapComputeQueue"}})
        symbols[Hle::Nid(pair.first)]=Hle::Lookup({},Hle::Nid(pair.second));
    Bind("sceVideoOutGetFlipStatus",VideoGetFlipStatus);
    Bind("sceVideoOutSubmitFlip",VideoSubmitFlip);
    Bind("sceVideoOutClose",VideoClose);
    Bind("sceAvPlayerInit",VideoPlayerInit);
    Bind("sceKernelCreateEqueue",CreateQueue);
    Bind("sceKernelDeleteEqueue",DeleteQueue);
    Bind("sceKernelWaitEqueue",WaitQueue);
    Bind("sceKernelAddUserEvent",AddUserEvent);
    Bind("sceKernelAddUserEventEdge",AddUserEvent);
    Bind("sceKernelTriggerUserEvent",TriggerUserEvent);
    Bind("sceKernelDeleteUserEvent",DeleteUserEvent);
    gnm_add=reinterpret_cast<GnmAdd>(symbols.at("b0xyllnVY-I"));
    gnm_delete=reinterpret_cast<GnmDelete>(symbols.at("PVT+fuoS9gU"));
    video_flip_add=reinterpret_cast<VideoAdd>(symbols.at("HXzjK9yI30k"));
    video_vblank_add=reinterpret_cast<VideoAdd>(symbols.at("Xru92wHJRmg"));
    Bind("sceGnmAddEqEvent",GnmAddEvent);
    Bind("sceGnmDeleteEqEvent",GnmDeleteEvent);
    Bind("sceVideoOutAddFlipEvent",VideoFlipEvent);
    Bind("sceVideoOutAddVblankEvent",VideoVblankEvent);
}
extern "C" int bbgpu_init(const BbGpuConfig*) {
    try {
        // Bloodborne's verified GPU argument/texture path avoids draining
        // unrelated CPU callbacks. Set the game defaults before creating the
        // renderer so its admission mode and mapping-mutation hooks agree.
        // Explicit environment overrides retain the synchronized comparison.
        for(const char* key:{"BB_METAL_NATIVE_INDIRECT","BB_METAL_GPU_WRITTEN_TEXTURE_SNAPSHOT"})
            if(!std::getenv(key) && ::setenv(key,"1",0))
                throw std::runtime_error("Could not configure the Bloodborne Metal defaults.");
        device=std::make_unique<Gpu::Metal::Device>();
        renderer=std::make_unique<Gpu::Metal::MetalRenderer>(*device);
        renderer->SetFrameReadyCallback([](void* texture) {
            Gpu::Metal::StutterTrace::Scope timing("frame_handoff",renderer->FrameCount(),0,0,50);
            frame_handoff.PublishAndWait(texture,[] {
                if (frame_event) { SDL_Event event{};event.type=frame_event;SDL_PushEvent(&event); }
            });
        });
        Gpu::Liverpool::Instance().SetRenderer(renderer.get());
        // The opt-in wind observer has real GPU source read leases even in
        // the synchronized native=0 comparison. Its leases need the same
        // remap exclusion; installing hooks does not enable native rendering.
        if(Gpu::Metal::WindCapture::NeedsMappingHooks(std::getenv("BB_METAL_NATIVE_INDIRECT"),
                std::getenv("BB_METAL_CAPTURE_WIND_FRAMES"))) {
            if(!runtime_gpu_memory_set_mutation_hooks(renderer.get(),
                [](void* context)->int {return static_cast<Gpu::Metal::MetalRenderer*>(context)->BeginGuestMemoryMutation();},
                [](void* context) {static_cast<Gpu::Metal::MetalRenderer*>(context)->EndGuestMemoryMutation();}))
                throw std::runtime_error("Could not protect Metal guest memory read mappings.");
            guest_mapping_hooks.active=true;
        }
        ready.store(true,std::memory_order_release);
#ifndef BB_CPU_TRANSLATOR
        if(bb_checkpoint_runtime_enabled() && !bb_checkpoint_runtime_graphics_hooks(&graphics_hooks))
            throw std::runtime_error("Could not register the Metal checkpoint provider.");
#endif
        return 0;
    } catch (const std::exception& error) {
        guest_mapping_hooks.Remove();
        fprintf(stderr,"Metal initialization failed: %s\n",error.what());
        return -1;
    }
}
extern "C" uintptr_t bbgpu_resolve(const char* name) {
    const auto end=strchr(name,'#');
    const auto it=feature_symbols.find(std::string(name,end ? size_t(end-name) : strlen(name)));
    if (it!=feature_symbols.end()) return uintptr_t(it->second);
    return uintptr_t(Hle::Lookup({},std::string_view(name,end ? size_t(end-name) : strlen(name))));
}
extern "C" unsigned bbgpu_symbol_count(void) { return unsigned(symbols.size()); }
extern "C" int bbmetal_wait_gpu_label(std::uint64_t address,std::uint64_t reference,unsigned compare,std::uint64_t budget) {
    return Gpu::Liverpool::Instance().WaitLabel(address,reference,8,compare,~0ull,
        std::chrono::steady_clock::now()+std::chrono::microseconds(std::min<std::uint64_t>(budget,250)));
}
extern "C" int bbgpu_handle_fault(void*,void*) { return 0; }
extern "C" void bbgpu_dump_guest_writes(void*) {}
extern "C" void bbnet_configure(const char*,const char*,const char*,const char*) { MacNetwork::Register(); }
extern "C" void bbnet_set_image(unsigned char* image,uint64_t size) { MacFeatures::BindImage(image,size); MacNetwork::SetImage(image,size); }
extern "C" void bbnet_initialize(void) { MacNetwork::Initialize(); }
extern "C" int bbgpu_overlay_captures_input(void) {
    return MacSettings::CapturesInput.load(std::memory_order_acquire) ||
        MacAchievements::CapturesInput.load(std::memory_order_acquire) ||
        MacInput::CapturesInput.load(std::memory_order_acquire);
}
extern "C" int vulkan_smoke(void) { return -1; }

extern "C" int bbmetal_window_main(int argc,char** argv,int (*guest_main)(int,char**)) {
    if (!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD)) {
        fprintf(stderr,"SDL video: %s\n",SDL_GetError()); return 1;
    }
    SDL_Window* window=SDL_CreateWindow("Bloodborne — 0.0 FPS",1280,720,
        SDL_WINDOW_METAL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) { fprintf(stderr,"SDL window: %s\n",SDL_GetError()); return 1; }
    // Optional test sizing uses physical pixels, independent of Retina scale.
    if(const char* native=std::getenv("BB_TEST_NATIVE_1080P_WINDOW");native && !std::strcmp(native,"1")) {
        SDL_SyncWindow(window);
        // Cocoa can constrain the initial window height before SDL receives its
        // resize event. Pixel density uses width and avoids that stale height.
        const float density=SDL_GetWindowPixelDensity(window);
        if(density>0)SDL_SetWindowSize(window,int(std::lround(1920.0/density)),int(std::lround(1080.0/density)));
        SDL_SyncWindow(window);
    }
    SDL_MetalView view=SDL_Metal_CreateView(window);
    if (!view) { fprintf(stderr,"SDL Metal view: %s\n",SDL_GetError()); return 1; }
    std::filesystem::path user="out/macos-data/user",data="out/macos-data";
    for (int k=1;k+1<argc;++k) {
        if (!std::strcmp(argv[k],"--app0")) setenv("BB_GAME_DIR",argv[k+1],1);
        if (!std::strcmp(argv[k],"--user")) user=argv[k+1];
    }
    if (argc>1) data=std::filesystem::path(argv[1]).parent_path().parent_path();
    if (const char* env=std::getenv("BB_DATA_DIR")) data=env;
    const auto settings_path=std::filesystem::absolute(std::getenv("BB_MAC_SETTINGS")
        ? std::filesystem::path(std::getenv("BB_MAC_SETTINGS")) : data/"macos-settings.ini");
    setenv("BB_MAC_SETTINGS",settings_path.c_str(),1);
    setenv("BB_DATA_DIR",std::filesystem::absolute(data).c_str(),1);
    setenv("BB_USER_DIR",std::filesystem::absolute(user).c_str(),1);
    MacSettings::Values settings=MacSettings::Load(settings_path);
    const bool startup_native_1080=settings.render_width==1920 && settings.render_height==1080;
    MacFeatures::Initialize(settings);
    MacNetwork::Configure(settings);
    if (auto mode=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window)); mode && mode->refresh_rate>=30) display_refresh.store(mode->refresh_rate);
    std::unique_ptr<Gpu::Metal::Presenter> presenter;
    bool applied_fullscreen=false;
    const auto apply_settings=[&] {
        MacFeatures::SetSettings(settings);
        if (presenter) {
            presenter->SetVsync(settings.vsync);
            presenter->SetUpscaling(settings.metalfx,settings.sharpness);
            renderer->SetGraphicsOptions(settings.anisotropy,settings.temporal_aa);
            const char* threaded=std::getenv("BB_METAL_PRESENT_THREAD");
            const char* link=std::getenv("BB_METAL_DISPLAY_LINK");
            const bool fg=settings.frame_generation && startup_native_1080 && settings.vsync &&
                (!threaded || std::strcmp(threaded,"0")) && (!link || std::strcmp(link,"0")) && presenter->SupportsFrameGeneration();
            renderer->SetFrameGeneration(fg);presenter->SetFrameGeneration(fg);
        }
        if (settings.fullscreen!=applied_fullscreen) {
            if (SDL_SetWindowFullscreen(window,settings.fullscreen)) applied_fullscreen=settings.fullscreen;
            else fprintf(stderr,"Fullscreen: %s\n",SDL_GetError());
        }
    };
    apply_settings();
    std::unique_ptr<MacAchievements::Browser> achievements;
    MacInput::Bridge input(window);
    MacSettings::Overlay overlay(window,settings,settings_path,apply_settings,[&] { if (achievements) achievements->Show(); });
    achievements=std::make_unique<MacAchievements::Browser>(window,std::getenv("BB_GAME_DIR") ? std::getenv("BB_GAME_DIR") : "");
    if (std::getenv("BB_MAC_SETTINGS_OPEN")) overlay.Toggle();
    frame_event=SDL_RegisterEvents(1);
    if (frame_event==Uint32(-1)) frame_event=0;
    std::thread worker([=] {
        @autoreleasepool {
            int result=guest_main(argc,argv);
            _Exit(result);
        }
    });
    worker.detach();
    // Optional CPU-side display timing: no GPU counters or capture instrumentation.
    std::unique_ptr<FILE,decltype(&std::fclose)> frame_times(nullptr,&std::fclose);
    if (const char* path=std::getenv("DC_FRAME_TIMES")) {
        frame_times.reset(std::fopen(path,"w"));
        if (frame_times) std::fprintf(frame_times.get(),"sequence,submitted_s,presented_s,interval_ms,queue_ms,generated,source_frame\n");
        else std::perror("DC_FRAME_TIMES");
    }
    std::unique_ptr<FILE,decltype(&std::fclose)> framegen_stats(nullptr,&std::fclose);
    if(const char* path=std::getenv("BB_METAL_FRAMEGEN_STATS")) {
        framegen_stats.reset(std::fopen(path,"w"));
        if(framegen_stats)std::fprintf(framegen_stats.get(),"time_s,rendered_total,displayed_total,generated_displayed_total,skipped_total,output_width,output_height\n");
    }
    double last_display_time=0;
    Uint64 render_frames=0,generated_frames=0;
    Uint64 fps_start=0, fps_frames=0;
    while (true) {
        @autoreleasepool {
            Gpu::Metal::StutterTrace::Scope iteration_timing("ui_iteration",0,0,0,50);
            Gpu::Metal::StutterTrace::Scope event_timing("ui_events");
            SDL_Event event;
            bool have_event=SDL_WaitEventTimeout(&event,4);
            while (have_event) {
                if (event.type==SDL_EVENT_QUIT) _Exit(0);
                if (event.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && presenter)
                    presenter->Resize(event.window.data1,event.window.data2);
                if (event.type==SDL_EVENT_WINDOW_DISPLAY_CHANGED) {
                    if (auto mode=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window)); mode && mode->refresh_rate>=30) display_refresh.store(mode->refresh_rate);
                    if(presenter)presenter->SetRefreshRate(display_refresh.load());
                }
                if (!input.HandleEvent(event) && !MacInput::CapturesInput.load() && !achievements->HandleEvent(event)) overlay.HandleEvent(event);
                input.Tick(MacSettings::CapturesInput.load() || MacAchievements::CapturesInput.load());
                have_event=SDL_PollEvent(&event);
            }
            overlay.Tick();
            achievements->Tick(SDL_GetTicks());
            input.Tick(MacSettings::CapturesInput.load() || MacAchievements::CapturesInput.load());
            if (MacFeatures::TakeSettings(settings)) apply_settings();
            event_timing.End();
            if (ready.load(std::memory_order_acquire)) {
                if(renderer->Execution()->Failed()) {
                    const auto error=renderer->Execution()->Error();
                    std::fprintf(stderr,"[Metal failure] %s\n",error.c_str());
                    frame_handoff.Acknowledge();
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Bloodborne graphics stopped",
                        "Graphics processing could not continue. Close and relaunch Bloodborne. Details were written to the launch log.",window);
                    _Exit(1);
                }
                if (!presenter) {
                    presenter=std::make_unique<Gpu::Metal::Presenter>(*device,SDL_Metal_GetLayer(view),Gpu::Metal::PresenterConfig{settings.vsync,display_refresh.load()});
                    int width=0,height=0; SDL_GetWindowSizeInPixels(window,&width,&height);
                    presenter->Resize(width,height);
                    apply_settings();
                    presenter->SetFrameProvider([] {return renderer->LatestGeneratedFrame();});
                    const char* threaded=std::getenv("BB_METAL_PRESENT_THREAD");
                    if(!threaded || std::strcmp(threaded,"0"))
                        presenter->StartFrameLoop([] {return frame_handoff.Pending();},[] {frame_handoff.Acknowledge();});
                    fps_start=SDL_GetTicksNS();
                    fps_frames=presenter->PresentedFrameCount();
                    render_frames=renderer->FrameCount();generated_frames=presenter->GeneratedPresentedFrameCount();
                }
                if (void* frame=presenter->HasFrameLoop()?nullptr:frame_handoff.Pending()) {
                    presenter->PresentTexture(frame);
                    frame_handoff.Acknowledge();
                }
                const Uint64 now=SDL_GetTicksNS();
                if (now-fps_start>=1000000000ull) {
                    Gpu::Metal::StutterTrace::Scope status_timing("ui_status");
                    const Uint64 frames=presenter->PresentedFrameCount();
                    const double fps=double(frames-fps_frames)*1.0e9/double(now-fps_start);
                    const auto rendered=renderer->FrameCount(),generated=presenter->GeneratedPresentedFrameCount();
                    const double rendered_fps=double(rendered-render_frames)*1.0e9/double(now-fps_start);
                    const double generated_fps=double(generated-generated_frames)*1.0e9/double(now-fps_start);
                    int width=0,height=0; SDL_GetWindowSizeInPixels(window,&width,&height);
                    if(framegen_stats) {
                        std::fprintf(framegen_stats.get(),"%.9f,%llu,%llu,%llu,%llu,%d,%d\n",CACurrentMediaTime(),
                            (unsigned long long)rendered,(unsigned long long)frames,(unsigned long long)generated,
                            (unsigned long long)presenter->SkippedFrameCount(),width,height);std::fflush(framegen_stats.get());
                    }
                    if(settings.frame_generation || framegen_stats)std::fprintf(stderr,"[frame rates] rendered=%.2f displayed=%.2f generated=%.2f FPS · %s\n",
                        rendered_fps,fps,generated_fps,renderer->FrameGenerationStatus());
                    char title[160];
                    if(settings.show_fps && settings.frame_generation)std::snprintf(title,sizeof(title),"Bloodborne — %.1f rendered / %.1f displayed FPS",rendered_fps,fps);
                    else if (settings.show_fps) std::snprintf(title,sizeof(title),"Bloodborne — %.1f FPS",fps);
                    else std::snprintf(title,sizeof(title),"Bloodborne");
                    SDL_SetWindowTitle(window,title);
                    std::string quality=presenter->UpscalingStatus();
                    if (settings.temporal_aa) quality+=renderer->TemporalAAActive()
                        ? " · Temporal AA active" : " · Temporal AA waiting for scene";
                    if(settings.frame_generation) {
                        quality+=" · ";
                        quality+=!presenter->SupportsFrameGeneration() ? "MetalFX frame generation unavailable" :
                            !startup_native_1080 ? "Frame generation requires native 1080p" : !settings.vsync ? "Frame generation requires V-sync" : renderer->FrameGenerationStatus();
                        quality+=" · "+std::to_string(int(std::round(rendered_fps)))+" rendered / "+std::to_string(int(std::round(fps)))+" displayed FPS";
                    }
                    overlay.UpdateStatus(fps,width,height,presenter->SupportsMetalFX(),quality.c_str());
                    auto samples=presenter->TakePresentations();
                    if (frame_times) {
                        std::sort(samples.begin(),samples.end(),[](const auto& a,const auto& b) {
                            return a.sequence<b.sequence;
                        });
                        std::vector<double> intervals;
                        unsigned skipped=0;
                        for (const auto& sample:samples) {
                            double interval=0;
                            if (sample.presented_time>last_display_time) {
                                if (last_display_time>0) interval=(sample.presented_time-last_display_time)*1000;
                                last_display_time=sample.presented_time;
                                if (interval>0) intervals.push_back(interval);
                            }
                            // Keep the last displayed time across dropped drawables
                            // so the next interval includes the visible gap.
                            if (sample.presented_time==0) ++skipped;
                            std::fprintf(frame_times.get(),"%llu,%.9f,%.9f,%.3f,%.3f,%u,%llu\n",
                                static_cast<unsigned long long>(sample.sequence),sample.submitted_time,
                                sample.presented_time,interval,sample.presented_time>0
                                    ? (sample.presented_time-sample.submitted_time)*1000 : 0,unsigned(sample.generated),(unsigned long long)sample.source_frame);
                        }
                        std::fflush(frame_times.get());
                        if (!intervals.empty()) {
                            std::sort(intervals.begin(),intervals.end());
                            std::fprintf(stderr,"[display] %.1f fps, frame interval median %.1f ms p95 %.1f ms max %.1f ms, skipped %u\n",
                                fps,intervals[intervals.size()/2],intervals[(intervals.size()-1)*95/100],intervals.back(),skipped);
                        }
                    }
                    fps_start=now;
                    fps_frames=frames;render_frames=rendered;generated_frames=generated;
                    if(std::getenv("DC_GPU_STATS")) {
                        std::fprintf(stderr,"[presentation] %s, %llu unavailable-drawable skips\n",presenter->HasFrameLoop()?"dedicated thread":"UI thread",(unsigned long long)presenter->SkippedFrameCount());
                        const auto& stats=Gpu::Liverpool::Instance().GetStats();
                        std::fprintf(stderr,"[GPU CPU waits] %llu waits, %.1f ms parked, %llu timeouts (cumulative)\n",
                            (unsigned long long)stats.label_waits.load(),stats.label_parked_us.load()/1000.0,
                            (unsigned long long)stats.label_timeouts.load());
                        auto& cp=Gpu::Liverpool::Instance();
                        if(cp.FenceMapCacheEnabled()) {
                            const auto cache=cp.FenceMapCacheStats();
                            std::fprintf(stderr,"[CP fence map cache] queries=%llu rebuilds=%llu reused=%llu rows=%llu lookups=%llu probes=%llu generation-rejects=%llu invalid-maps=%llu allocation-failures=%llu zero-bytes=%llu empty-pending=%llu unresolved-spans=%llu snapshot-rows=%llu (cumulative; mapping metadata only)\n",
                                static_cast<unsigned long long>(cache.queries),static_cast<unsigned long long>(cache.rebuilds),
                                static_cast<unsigned long long>(cache.reused),static_cast<unsigned long long>(cache.rows),
                                static_cast<unsigned long long>(cache.lookups),static_cast<unsigned long long>(cache.probes),
                                static_cast<unsigned long long>(cache.generation_rejects),static_cast<unsigned long long>(cache.invalid_maps),
                                static_cast<unsigned long long>(cache.allocation_failures),static_cast<unsigned long long>(cache.zero_bytes),
                                static_cast<unsigned long long>(cache.empty_pending),static_cast<unsigned long long>(cache.unresolved_spans),
                                static_cast<unsigned long long>(cache.snapshot_rows));
                            if(cp.FenceDestinationIndexEnabled()) {
                                const auto& index=cache.destinations;
                                std::fprintf(stderr,"[CP fence destination index] queries=%llu rebuilds=%llu reused=%llu rows=%llu probes=%llu unresolved=%llu epoch-rejects=%llu size-rejects=%llu allocation-failures=%llu current-rows=%llu (metadata counts cumulative; current-rows gauge)\n",
                                    static_cast<unsigned long long>(index.queries),static_cast<unsigned long long>(index.rebuilds),
                                    static_cast<unsigned long long>(index.reused),static_cast<unsigned long long>(index.rows),
                                    static_cast<unsigned long long>(index.probes),static_cast<unsigned long long>(index.unresolved),
                                    static_cast<unsigned long long>(index.epoch_rejects),static_cast<unsigned long long>(index.size_rejects),
                                    static_cast<unsigned long long>(index.allocation_failures),static_cast<unsigned long long>(index.current_rows));
                            }
                        }
                    }
                }
            }
        }
    }
}

// Services consumed by the selected bbhost engine modules.
extern "C" void bbmac_register_hle(const char* name,void* fn) { feature_symbols[Hle::Nid(name)]=fn; }
extern "C" void bbmac_register_nid(const char* nid,void* fn) { symbols[nid]=fn; }
std::uint64_t hle_video_flip_count() { return renderer ? renderer->FrameCount() : 0; }
void hle_video_set_game_pace(int) { Hle::VideoOut::Driver::Instance().SetFrameLimit(0); }
float host_window_refresh_hz() { return display_refresh.load(); }
