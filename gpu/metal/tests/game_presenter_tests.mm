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
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdio>
#include <cstring>

int main(int argc,char** argv) {
    using namespace std::chrono_literals;
    const bool legacy=argc>1 && std::strcmp(argv[1],"--legacy")==0;
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
        if(!legacy)presenter.StartFrameLoop([&] {return handoff.Pending();},[&] {handoff.Acknowledge();});
        std::thread producer([&] {while(!stop.load()) {handoff.PublishAndWait((__bridge void*)texture,[]{});++produced;}done=true;});
        const auto pump=[&] {
            @autoreleasepool {
                SDL_Event event;
                if(SDL_WaitEventTimeout(&event,4))while(SDL_PollEvent(&event)){}
                if(legacy)if(void* pending=handoff.Pending()) {presenter.PresentTexture(pending);handoff.Acknowledge();}
            }
        };
        const auto deadline=std::chrono::steady_clock::now()+4s;
        while(presenter.PresentedFrameCount()<8 && std::chrono::steady_clock::now()<deadline)pump();
        const auto before=presenter.PresentedFrameCount();
        std::printf("Presentation warmup: %llu display updates, %llu submissions, %llu skips, %llu displayed\n",
            (unsigned long long)presenter.DisplayLinkUpdateCount(),(unsigned long long)presenter.SubmittedFrameCount(),
            (unsigned long long)presenter.SkippedFrameCount(),(unsigned long long)before);
        if(before>=8) {
            const auto produced_before=produced.load();
            const auto submitted_before=presenter.SubmittedFrameCount();
            std::this_thread::sleep_for(450ms); // deliberate main-thread stall
            const auto displayed=presenter.PresentedFrameCount()-before;
            std::printf("UI blocked 450 ms: %llu new displayed frames, %u producer frames\n",
                (unsigned long long)displayed,produced.load()-produced_before);
            // GPU completion and presentation callbacks are separate. Already
            // queued frames may finish during the pause; no new command can be
            // submitted by the blocked legacy UI consumer.
            if(legacy)assert(presenter.SubmittedFrameCount()==submitted_before &&
                displayed<=submitted_before-before && produced.load()<=produced_before+1);
            else assert(displayed>=4 && produced.load()>produced_before+3);
            // Settings cross the thread boundary; toggles must not deadlock or
            // strand an old display-link delegate. Keep storage producer-owned.
            for(bool vsync:{false,true}) {
                presenter.SetVsync(vsync);presenter.Resize(640,360);presenter.SetRefreshRate(120);
                const auto until=std::chrono::steady_clock::now()+200ms;
                while(std::chrono::steady_clock::now()<until)pump();
            }
        }
        stop=true;
        const auto finish=std::chrono::steady_clock::now()+2s;
        while(!done && std::chrono::steady_clock::now()<finish)pump();
        assert(done);
        producer.join();
        assert(!handoff.Pending());
        if(before<8) {std::puts("No onscreen presentation timestamps; interactive fixture skipped");result=77;}
        else std::puts(legacy?"Legacy control reproduced UI-stall backpressure":"Metal presenter: UI-stall isolation, queue handoff, settings transitions and shutdown passed");
        }
        SDL_Metal_DestroyView(view);SDL_DestroyWindow(window);SDL_Quit();return result;
    }
}
