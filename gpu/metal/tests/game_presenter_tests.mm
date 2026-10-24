// SPDX-License-Identifier: GPL-2.0-or-later
// Real window/Metal regression: presentation must advance while the UI pauses.
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include "gpu/metal/mtl_device.h"
#include "gpu/metal/mtl_presenter.h"
#include "gpu/metal/frame_handoff.h"
#include "gpu/metal/frame_generation.h"
#include <atomic>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <unistd.h>

// Validate timestamps from real command-buffer and drawable callbacks, rather
// than assuming GPU completion means the compositor presented the drawable.
static void CheckTiming(const char* path) {
    FILE* file=std::fopen(path,"r");assert(file);
    char line[2048];assert(std::fgets(line,sizeof(line),file));
    unsigned link_records=0,legacy_records=0,dropped=0,scheduled_late=0;
    while(std::fgets(line,sizeof(line),file)) {
        unsigned long long sequence=0;int link=0;long status=0,error=0;
        double callback,deadline,target,submitted,commit_begin,commit_end,scheduled;
        double gpu_start,gpu_end,completed,presented_callback,presented;
        const int fields=std::sscanf(line,"%llu,%d,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%ld,%ld",
            &sequence,&link,&callback,&deadline,&target,&submitted,&commit_begin,&commit_end,
            &scheduled,&gpu_start,&gpu_end,&completed,&presented_callback,&presented,&status,&error);
        assert(fields==16 && sequence>0);
        assert(status==MTLCommandBufferStatusCompleted && error==0);
        assert(std::isfinite(gpu_start) && gpu_start>0 && gpu_end>=gpu_start);
        assert(submitted>0 && commit_begin>=submitted && commit_end>=commit_begin);
        assert(scheduled>=commit_begin && completed>=scheduled && presented_callback>0);
        if(link) {
            ++link_records;
            assert(std::isfinite(callback) && callback>0 && deadline>0 && target>0);
            assert(submitted>=callback);
            scheduled_late+=scheduled>deadline;
        } else {
            ++legacy_records;assert(callback==0 && deadline==0 && target==0);
        }
        dropped+=presented==0;
    }
    std::fclose(file);
    assert(link_records>=4 && legacy_records>0);
    std::printf("Presentation timing: %u display-link / %u legacy records, %u scheduled after deadline, %u dropped\n",
        link_records,legacy_records,scheduled_late,dropped);
}

int main(int argc,char** argv) {
    using namespace std::chrono_literals;
    const bool legacy=argc>1 && std::strcmp(argv[1],"--legacy")==0;
    const bool latency2=argc>1 && std::strcmp(argv[1],"--latency2")==0;
    const bool framegen=argc>1 && std::strcmp(argv[1],"--framegen")==0;
    const bool force_link=argc>1 && std::strcmp(argv[1],"--display-link")==0;
    const bool event_wake=argc>1 && std::strcmp(argv[1],"--event-wake")==0;
    const bool poll_only=argc>1 && std::strcmp(argv[1],"--poll-only")==0;
    const bool force_pump=event_wake || poll_only || (argc>1 && std::strcmp(argv[1],"--display-link-off")==0);
    // Test the default independently of the caller's diagnostic environment.
    // Latency two explicitly selects the link whose latency is being tested.
    assert(unsetenv("BB_METAL_DISPLAY_LINK")==0);
    assert(unsetenv("BB_METAL_DISPLAY_LINK_LATENCY")==0);
    assert(unsetenv("BB_METAL_PRESENT_EVENT_WAKE")==0);
    assert(unsetenv("BB_METAL_PRESENT_POLL_MS")==0);
    if(event_wake) {
        assert(setenv("BB_METAL_PRESENT_EVENT_WAKE","1",1)==0);
        // A 250-ms fallback timer cannot satisfy the 450-ms UI-pause check.
        assert(setenv("BB_METAL_PRESENT_POLL_MS","250",1)==0);
    }
    if(poll_only)assert(setenv("BB_METAL_PRESENT_EVENT_WAKE","0",1)==0);
    if(force_link || latency2)assert(setenv("BB_METAL_DISPLAY_LINK","1",1)==0);
    if(force_pump)assert(setenv("BB_METAL_DISPLAY_LINK","0",1)==0);
    char timing_path[]="/private/tmp/bb-presenter-timing-XXXXXX";
    if(latency2) {
        const int fd=mkstemp(timing_path);assert(fd>=0);close(fd);
        assert(setenv("BB_METAL_DISPLAY_LINK_LATENCY","2",1)==0);
        assert(setenv("BB_METAL_PRESENT_TIMING_CSV",timing_path,1)==0);
    }
    std::function<void()> retained_notifier;
    @autoreleasepool {
        assert(SDL_Init(SDL_INIT_VIDEO));
        auto* window=SDL_CreateWindow("Metal presentation regression",640,360,SDL_WINDOW_METAL);
        assert(window);auto view=SDL_Metal_CreateView(window);assert(view);SDL_RaiseWindow(window);
        auto layer=(__bridge CAMetalLayer*)SDL_Metal_GetLayer(view);
        int result=0;
        {
        Gpu::Metal::FrameHandoff handoff; // callbacks outlive the presenter
        Gpu::Metal::Device device;
        Gpu::Metal::Presenter presenter(device,(__bridge void*)layer,{true,120});
        presenter.Resize(640,360);
        auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:64 height:64 mipmapped:NO];
        d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageShaderRead;
        auto texture=[(__bridge id<MTLDevice>)device.Handle() newTextureWithDescriptor:d];assert(texture);
        std::vector<std::uint32_t> pixels(64*64,0xff4080c0);
        [texture replaceRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0 withBytes:pixels.data() bytesPerRow:64*4];
        std::atomic<bool> stop=false;
        std::atomic<bool> done=false;
        std::atomic<unsigned> produced=0;
        std::shared_ptr<const Gpu::Metal::GeneratedFrame> packet;
        id<MTLSharedEvent> ready_event=[(__bridge id<MTLDevice>)device.Handle() newSharedEvent];
        if(framegen) {
            if(!presenter.SupportsFrameGeneration())return 77;
            presenter.SetFrameGeneration(true);
            presenter.SetFrameProvider([&] {return std::atomic_load(&packet);});
        }
        if(!legacy)presenter.StartFrameLoop([&] {return handoff.Pending();},[&] {handoff.Acknowledge();});
        retained_notifier=presenter.FrameReadyNotifier();
        handoff.SetNotifier(retained_notifier);
        std::thread producer([&] {while(!stop.load()) {
            if(framegen) {
                auto next=std::make_shared<Gpu::Metal::GeneratedFrame>();next->real=next->generated=(__bridge void*)texture;
                next->source_frame=produced.load()+1;next->interval=1./30;
                next->ready_event=(__bridge void*)ready_event;next->ready_value=next->source_frame;
                auto command=[(__bridge id<MTLCommandQueue>)device.Queue() commandBuffer];
                [command encodeSignalEvent:ready_event value:next->ready_value];[command commit];
                std::atomic_store(&packet,std::shared_ptr<const Gpu::Metal::GeneratedFrame>(next));
            }
            handoff.PublishAndWait((__bridge void*)texture,[]{});++produced;
            if(framegen)std::this_thread::sleep_for(33ms);
        }done=true;});
        const auto pump=[&] {
            @autoreleasepool {
                SDL_Event event;
                if(SDL_WaitEventTimeout(&event,4))while(SDL_PollEvent(&event)){}
                if(legacy)if(void* pending=handoff.Pending()) {presenter.PresentTexture(pending);handoff.Acknowledge();}
            }
        };
        const auto pump_for=[&](std::chrono::milliseconds duration) {
            const auto until=std::chrono::steady_clock::now()+duration;
            while(std::chrono::steady_clock::now()<until)pump();
        };
        const auto check_pacing=[&](bool vsync,bool generation) {
#if defined(__aarch64__)
            constexpr bool default_real_link=false;
#else
            constexpr bool default_real_link=true;
#endif
            const bool expected_link=!legacy && vsync && (force_link || latency2 || (!force_pump && (generation || default_real_link)));
            const auto updates=presenter.DisplayLinkUpdateCount();
            pump_for(100ms);
            if(expected_link)assert(presenter.DisplayLinkUpdateCount()>updates);
            else assert(presenter.DisplayLinkUpdateCount()==updates);
            assert(bool(layer.displaySyncEnabled)==vsync);
        };
        const auto deadline=std::chrono::steady_clock::now()+4s;
        while(presenter.PresentedFrameCount()<8 && std::chrono::steady_clock::now()<deadline)pump();
        const auto before=presenter.PresentedFrameCount();
        std::printf("Presentation warmup: %llu display updates, %llu submissions, %llu skips, %llu displayed\n",
            (unsigned long long)presenter.DisplayLinkUpdateCount(),(unsigned long long)presenter.SubmittedFrameCount(),
            (unsigned long long)presenter.SkippedFrameCount(),(unsigned long long)before);
        if(before>=8) {
            check_pacing(true,framegen);
            const auto produced_before=produced.load();
            const auto submitted_before=presenter.SubmittedFrameCount();
            const auto displayed_before=presenter.PresentedFrameCount();
            std::this_thread::sleep_for(450ms); // deliberate main-thread stall
            const auto displayed=presenter.PresentedFrameCount()-displayed_before;
            std::printf("UI blocked 450 ms: %llu new displayed frames, %u producer frames\n",
                (unsigned long long)displayed,produced.load()-produced_before);
            // GPU completion and presentation callbacks are separate. Already
            // queued frames may finish during the pause; no new command can be
            // submitted by the blocked legacy UI consumer.
            if(legacy)assert(presenter.SubmittedFrameCount()==submitted_before &&
                displayed<=submitted_before-displayed_before && produced.load()<=produced_before+1);
            else assert(displayed>=4 && produced.load()>produced_before+3);
            // Settings cross the thread boundary; toggles must not deadlock or
            // strand an old display-link delegate. Keep storage producer-owned.
            for(bool vsync:{false,true}) {
                presenter.SetVsync(vsync);presenter.Resize(640,360);presenter.SetRefreshRate(120);
                pump_for(200ms);check_pacing(vsync,framegen);
            }
            if(framegen) {
                // Default policy changes with FG, not with a recreated
                // presenter. Producer storage and queued packet order stay
                // under the existing owner/event contract across transitions.
                for(bool enabled:{false,true}) {
                    presenter.SetFrameGeneration(enabled);
                    pump_for(200ms);check_pacing(true,enabled);
                }
            }
        }
        stop=true;
        const auto finish=std::chrono::steady_clock::now()+2s;
        while(!done && std::chrono::steady_clock::now()<finish)pump();
        assert(done);
        producer.join();
        assert(!handoff.Pending());
        if(framegen && before>=8) {
            const auto until=std::chrono::steady_clock::now()+100ms;
            while(std::chrono::steady_clock::now()<until)pump();
            auto samples=presenter.TakePresentations();
            std::sort(samples.begin(),samples.end(),[](const auto& a,const auto& b){return a.sequence<b.sequence;});
            unsigned generated=0,real=0;
            std::uint64_t previous_source=0;
            for(const auto& sample:samples)if(sample.presented_time>0 && sample.source_frame) {
                assert(sample.source_frame>=previous_source);previous_source=sample.source_frame;
                if(sample.generated)++generated;else ++real;
            }
            std::printf("Frame-generation presentation: %u real, %u generated drawable timestamps, %u producer frames\n",real,generated,produced.load());
            assert(generated>=8 && real>=8 && presenter.GeneratedPresentedFrameCount()>0);
            assert(presenter.PresentedFrameCount()<=2*produced.load());
        }
        if(before<8) {std::puts("No onscreen presentation timestamps; interactive fixture skipped");result=77;}
        else std::puts(legacy?"Legacy UI control reproduced UI-stall backpressure":
            event_wake?"Metal presenter: event wake without timer assistance, VSync transitions and shutdown passed":
            poll_only?"Metal presenter: polling-only comparison, VSync transitions and shutdown passed":
            framegen?"Metal presenter: default FG display link, FG/VSync transitions, ordered pairs and shutdown passed":
            force_link?"Metal presenter: explicit display link, UI-stall isolation, VSync transitions and shutdown passed":
            force_pump?"Metal presenter: explicit worker pump, UI-stall isolation, VSync transitions and shutdown passed":
            latency2?"Metal presenter: explicit display link latency2, UI-stall isolation and shutdown passed":
            "Metal presenter: CPU-specific default pacing, UI-stall isolation, VSync transitions and shutdown passed");
        }
        // Producer-held notifications do not retain or call a destroyed Impl.
        if(retained_notifier)for(unsigned i=0;i<100;++i)retained_notifier();
        if(latency2 && result==0)CheckTiming(timing_path);
        if(latency2)unlink(timing_path);
        SDL_Metal_DestroyView(view);SDL_DestroyWindow(window);SDL_Quit();return result;
    }
}
