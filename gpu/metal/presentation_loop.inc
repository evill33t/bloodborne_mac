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
    frame_generation=next.frame_generation && next.vsync;
    if(frame_generation && !interpolation_queue) {
        presentation_order=[device newSharedEvent];
        if(presentation_order)interpolation_queue=[device newCommandQueue];
    }
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

// One generated midpoint followed by its current real frame. The producer is
// released when its committed private snapshots are retained by the consumer.
// Never advance guest frame clocks or count a repeat as a generated frame.
void Presenter::Impl::Display(CAMetalDisplayLinkUpdate* update,double diagnostic_callback) {
    const double now=CACurrentMediaTime();
    const double tick=1./std::max(30.f,applied.refresh_rate);
    const double display_time=update.targetPresentationTimestamp>0 ? update.targetPresentationTimestamp : now;
    // The renderer has already committed private snapshots before publishing.
    // Release guest submission now, without reserving a drawable that would
    // expire while its GPU inputs are still being produced.
    if(frame_generation && !ready_pair && frame_provider && pending()) {
        auto packet=frame_provider();
        if(packet && packet->real && packet->ready_event) {ready_pair=std::move(packet);queued();}
    }
    if(!update.drawable || !pipeline)return;
    if(held_real) {
        if(frame_generation && display_time+tick*.25<real_due)return;
        if(dispatch_semaphore_wait(frames_in_flight,DISPATCH_TIME_NOW)!=0)return;
        auto packet=std::move(held_real);
        EncodeTexture((__bridge id<MTLTexture>)packet->real,update.drawable,true,diagnostic_callback,
            update.targetTimestamp,update.targetPresentationTimestamp,false,packet->source_frame,packet);
        return;
    }
    if(ready_pair && ready_pair->ready_event &&
        [(__bridge id<MTLSharedEvent>)ready_pair->ready_event signaledValue]<ready_pair->ready_value)return;
    void* texture=ready_pair ? ready_pair->real : pending();if(!texture)return;
    if(dispatch_semaphore_wait(frames_in_flight,DISPATCH_TIME_NOW)!=0)return;
    const bool acknowledge=!ready_pair;
    auto packet=std::move(ready_pair);
    const bool generated=frame_generation && packet && packet->generated && packet->interval>=1.75/std::max(30.f,applied.refresh_rate);
    if(packet)texture=generated ? packet->generated : packet->real;
    EncodeTexture((__bridge id<MTLTexture>)texture,update.drawable,true,diagnostic_callback,
        update.targetTimestamp,update.targetPresentationTimestamp,generated,packet ? packet->source_frame : 0,packet);
    if(generated) {
        held_real=packet;
        real_due=display_time+std::clamp(std::round(packet->interval*.5/tick)*tick,tick,.05);
    }
    if(acknowledge)queued();
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
            owner->Display(update,callback);

        };
        ApplySettings(true);
        NSTimer* timer=[NSTimer timerWithTimeInterval:.001 repeats:YES block:^(NSTimer*) {
            owner->ApplySettings(true);
            if(owner->display_link) {
                const double now=StutterTrace::Now();
                if(now-owner->last_display_update>.1 && (owner->pending() || owner->held_real || owner->ready_pair)) {
                    // Hidden/occluded windows may receive no updates. Preserve
                    // the old nil-drawable skip behavior with a bounded delay;
                    // never strand a guest fence behind a missing compositor.
                    StutterTrace::Record("display_unavailable",owner->last_display_update,now,owner->next_sequence);
                    owner->last_display_update=now;++owner->skipped;
                    owner->held_real.reset();owner->ready_pair.reset();
                    if(owner->pending())owner->queued();
                }
            } else if(owner->held_real) {
                owner->PresentLegacy(owner->held_real->real,owner->held_real);owner->held_real.reset();
            } else if(owner->ready_pair) {
                owner->PresentLegacy(owner->ready_pair->real,owner->ready_pair);owner->ready_pair.reset();
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
