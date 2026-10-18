// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/game_input.h"
#include "gpu/bbgpu.h"
#import <AppKit/AppKit.h>
#include <SDL3/SDL.h>
#include <cassert>
#include <cstring>
#include <cstdio>

static void Pump() {
    for (int i=0;i<20;++i) {
        SDL_PumpEvents();
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.001]];
    }
}
static NSTextField* Field(NSView* view) {
    if ([view isKindOfClass:NSTextField.class] && ((NSTextField*)view).editable) return (NSTextField*)view;
    for (NSView* child in view.subviews) if (auto field=Field(child)) return field;
    return nil;
}
int main() {
    @autoreleasepool {
        char text[512];
        assert(!bbgpu_text_input_begin("Hunter","Name"));
        assert(bbgpu_text_input_poll(text,sizeof(text))==2);
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
        if (!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD)) return 77;
        auto window=SDL_CreateWindow("Input fixture",640,480,SDL_WINDOW_HIDDEN|SDL_WINDOW_METAL);
        if (!window) return 77;
        auto cocoa=(__bridge NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,nullptr);
        assert(cocoa);
        {
            MacInput::Bridge input(window);
            bbgpu_input_configure(0,SDL_SCANCODE_F7,SDL_SCANCODE_F8);
            SDL_Event event{}; event.type=SDL_EVENT_KEY_DOWN; event.key.scancode=SDL_SCANCODE_F8;
            assert(input.HandleEvent(event)); assert(bbgpu_input_reload_requested()); assert(!bbgpu_input_reload_requested());
            event.key.repeat=true; input.HandleEvent(event); assert(!bbgpu_input_reload_requested());
            assert(bbgpu_text_input_begin("Hunter","Character name"));
            assert(MacInput::CapturesInput.load() && bbgpu_text_input_poll(text,sizeof(text))==0);
            assert(!bbgpu_text_input_begin("Other","Duplicate"));
            input.Tick(false); Pump();
            auto sheet=cocoa.attachedSheet; assert(sheet);
            auto field=Field(sheet.contentView); assert(field && [field.stringValue isEqualToString:@"Hunter"]);
            field.stringValue=@"Éileen 猎人";
            [cocoa endSheet:sheet returnCode:NSAlertFirstButtonReturn]; Pump();
            assert(bbgpu_text_input_poll(text,sizeof(text))==1 && !std::strcmp(text,"Éileen 猎人"));
            assert(!MacInput::CapturesInput.load());
            char tiny[2]{'x','x'}; assert(bbgpu_text_input_poll(tiny,2)==1 && tiny[0]==0);
            char small[3]{}; bbgpu_text_input_poll(small,3); assert(!std::strcmp(small,"É"));
            assert(bbgpu_text_input_begin("Hunter","Cancel")); input.Tick(false); Pump();
            sheet=cocoa.attachedSheet; assert(sheet);
            [cocoa endSheet:sheet returnCode:NSAlertSecondButtonReturn]; Pump();
            assert(bbgpu_text_input_poll(text,sizeof(text))==2 && !MacInput::CapturesInput.load());
            SDL_VirtualJoystickDesc desc{}; desc.version=sizeof(desc);
            desc.type=SDL_JOYSTICK_TYPE_GAMEPAD; desc.naxes=SDL_GAMEPAD_AXIS_COUNT;
            desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT; desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
            desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1; desc.name="Metal input fixture";
            const auto id=SDL_AttachVirtualJoystick(&desc); assert(id);
            auto pad=SDL_OpenGamepad(id); assert(pad);
            auto joystick=SDL_GetGamepadJoystick(pad); assert(joystick);
            assert(bbgpu_text_input_begin("Hunter","Controller confirm")); input.Tick(false); Pump();
            assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true)); SDL_PumpEvents();
            input.Tick(false); Pump();
            assert(bbgpu_text_input_poll(text,sizeof(text))==1 && MacInput::CapturesInput.load());
            input.Tick(false); assert(MacInput::CapturesInput.load()); // held Cross must stay neutral in the game
            assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,false)); SDL_PumpEvents();
            input.Tick(false); assert(!MacInput::CapturesInput.load());
            SDL_CloseGamepad(pad); SDL_DetachVirtualJoystick(id);
            bbgpu_input_configure(1,SDL_SCANCODE_F7,SDL_SCANCODE_F8);
            event={}; event.type=SDL_EVENT_WINDOW_FOCUS_GAINED; input.HandleEvent(event); input.Tick(false);
            BbMouseInput mouse{}; bbgpu_mouse_take(&mouse);
            if (mouse.captured) {
                event={}; event.type=SDL_EVENT_MOUSE_MOTION; event.motion.xrel=3; event.motion.yrel=-2;
                assert(input.HandleEvent(event));
                event.type=SDL_EVENT_MOUSE_BUTTON_DOWN; event.button.button=SDL_BUTTON_LEFT; input.HandleEvent(event);
                bbgpu_mouse_take(&mouse); assert(mouse.dx==3 && mouse.dy==-2 && mouse.buttons==SDL_BUTTON_LMASK);
                bbgpu_mouse_take(&mouse); assert(mouse.dx==0 && mouse.dy==0 && mouse.buttons==SDL_BUTTON_LMASK);
                event={}; event.type=SDL_EVENT_MOUSE_WHEEL; event.wheel.y=1; input.HandleEvent(event);
                bbgpu_mouse_take(&mouse); assert(mouse.wheel==1);
                SDL_Delay(35); bbgpu_mouse_take(&mouse); assert(mouse.wheel==0);
                input.Tick(true); bbgpu_mouse_take(&mouse);
                assert(!mouse.captured && !mouse.buttons && !mouse.dx && !mouse.wheel);
                input.Tick(false); bbgpu_mouse_take(&mouse); assert(mouse.captured && !mouse.buttons);
                event={}; event.type=SDL_EVENT_WINDOW_FOCUS_LOST; input.HandleEvent(event);
                bbgpu_mouse_take(&mouse); assert(!mouse.captured && !mouse.buttons);
                puts("Mouse: accumulated deltas, held buttons, wheel pulses, UI exclusion and focus loss passed");
            } else puts("Mouse capture unavailable for hidden fixture; text and hotkey checks passed");
        }
        assert(!bbgpu_text_input_begin("Hunter","Closed"));
        SDL_DestroyWindow(window); SDL_Quit();
        puts("Native text entry: UTF-8, confirmation, cancellation, duplicate requests and input hotkeys passed");
    }
}
