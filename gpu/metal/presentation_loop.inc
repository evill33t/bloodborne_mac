// SPDX-License-Identifier: GPL-2.0-or-later
// Presenter::Impl helpers. All layer/encoder work belongs to one render thread;
// the UI only publishes settings and reads atomic presentation statistics.
void Presenter::Impl::ApplySettings(bool threaded) {
    Settings next;std::uint64_t generation;
    {std::lock_guard lock(settings_mutex);generation=settings_generation;if(generation==applied_generation)return;next=requested;}
    if(next.width>0 && next.height>0 && (next.width!=applied.width || next.height!=applied.height))
        layer.drawableSize=CGSizeMake(next.width,next.height);
    layer.displaySyncEnabled=next.vsync;
    metalfx=next.metalfx;sharpness=next.sharpness;
    if(threaded) {
        const char* opt=std::getenv("BB_METAL_DISPLAY_LINK");
        const bool use_link=next.vsync && (!opt || std::strcmp(opt,"0"));
        if(display_link && (!use_link || next.refresh_rate!=applied.refresh_rate)) {
            [display_link invalidate];display_link=nil;
        }
        if(use_link && !display_link) {
            display_link=[[CAMetalDisplayLink alloc] initWithMetalLayer:layer];
            display_link.delegate=display_target;
            const float rate=std::clamp(next.refresh_rate,30.f,1000.f);
            display_link.preferredFrameRateRange=CAFrameRateRangeMake(rate,rate,rate);
            display_link.preferredFrameLatency=display_link_latency;
            display_link.paused=NO;
            last_display_update=StutterTrace::Now();
            [display_link addToRunLoop:NSRunLoop.currentRunLoop forMode:NSRunLoopCommonModes];
        }
    }
    applied=next;applied_generation=generation;
}

void Presenter::Impl::Run(std::stop_token stop) {
    pthread_setname_np("Metal presentation");
    @autoreleasepool {
        display_target=[BBMetalDisplayTarget new];
        Impl* owner=this;
        display_target.render=^(CAMetalDisplayLinkUpdate* update) {
            const double callback=owner->timing_log ? CACurrentMediaTime() : 0;
            ++owner->display_updates;
            owner->last_display_update=StutterTrace::Now();
            void* texture=owner->pending();
            if(!texture || !update.drawable || !owner->pipeline) return;
            // A display callback must never wait for a GPU slot. The producer
            // retains its frame until a later callback can enqueue the read.
            if(dispatch_semaphore_wait(owner->frames_in_flight,DISPATCH_TIME_NOW)!=0)return;
            StutterTrace::Scope timing("display_link_encode",owner->next_sequence+1);
            @autoreleasepool {
                owner->EncodeTexture((__bridge id<MTLTexture>)texture,update.drawable,true,
                    callback,update.targetTimestamp,update.targetPresentationTimestamp);
                owner->queued();
            }
        };
        ApplySettings(true);
        NSTimer* timer=[NSTimer timerWithTimeInterval:.001 repeats:YES block:^(NSTimer*) {
            owner->ApplySettings(true);
            if(owner->display_link) {
                const double now=StutterTrace::Now();
                if(now-owner->last_display_update>.1 && owner->pending()) {
                    // Hidden/occluded windows may receive no updates. Preserve
                    // the old nil-drawable skip behavior with a bounded delay;
                    // never strand a guest fence behind a missing compositor.
                    StutterTrace::Record("display_unavailable",owner->last_display_update,now,owner->next_sequence);
                    owner->last_display_update=now;++owner->skipped;
                    owner->queued();
                }
            } else if(void* texture=owner->pending()) {
                owner->PresentLegacy(texture);owner->queued();
            }
        }];
        NSRunLoop* loop=NSRunLoop.currentRunLoop;
        [loop addTimer:timer forMode:NSRunLoopCommonModes];
        while(!stop.stop_requested()) {
            @autoreleasepool {[loop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.05]];}
        }
        [timer invalidate];
        display_target.render=nil;
        [display_link invalidate];display_link=nil;display_target=nil;
    }
}

void Presenter::StartFrameLoop(std::function<void*()> pending,std::function<void()> queued) {
    if(impl->frame_loop.exchange(true))return;
    impl->pending=std::move(pending);impl->queued=std::move(queued);
    impl->applied_generation=0;
    impl->worker=std::jthread([state=impl.get()](std::stop_token stop) {state->Run(stop);});
}
bool Presenter::HasFrameLoop() const {return impl->frame_loop.load();}
std::uint64_t Presenter::SkippedFrameCount() const {return impl->skipped.load();}
std::uint64_t Presenter::DisplayLinkUpdateCount() const {return impl->display_updates.load();}
std::uint64_t Presenter::SubmittedFrameCount() const {return impl->next_sequence.load();}
