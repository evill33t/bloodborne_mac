// SPDX-License-Identifier: GPL-2.0-or-later
#import <AppKit/AppKit.h>
#include <SDL3/SDL.h>
#include "gpu/metal/game_overlay.h"
#include "gpu/bbgpu.h"
#include <cassert>
#include <cstdio>
#include <unistd.h>

static bool restarted=false;
extern "C" void runtime_restart() { restarted=true; }
static NSControl* Find(NSView* view,NSString* label) {
    if ([view isKindOfClass:NSControl.class]) {
        auto c=(NSControl*)view;
        if ([c.accessibilityLabel isEqualToString:label] ||
            ([c isKindOfClass:NSButton.class] && [((NSButton*)c).title isEqualToString:label])) return c;
    }
    for (NSView* child in view.subviews) if (auto found=Find(child,label)) return found;
    return nil;
}
static void Invoke(NSControl* c) { assert(c && c.enabled); [c sendAction:c.action to:c.target]; }
int main() {
    @autoreleasepool {
        setenv("BB_MAC_TIMESTEP","1",1);
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
        assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
        auto sdl=SDL_CreateWindow("Bloodborne settings test",1000,700,SDL_WINDOW_METAL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIDDEN);
        assert(sdl);
        auto window=(__bridge NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(sdl),
            SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,nullptr); assert(window);
        char dir[]="/tmp/bb-overlay-XXXXXX"; assert(mkdtemp(dir));
        auto path=std::filesystem::path(dir)/"settings.ini";
        MacSettings::Values values; int applied=0,achievements_opened=0;
        {
            MacSettings::Overlay overlay(sdl,values,path,[&] { ++applied; },[&] { ++achievements_opened; });
            overlay.UpdateStatus(30,2000,1400,true,"Bilinear scaling");
            assert(!bbgpu_overlay_captures_input());
            overlay.Toggle(); assert(bbgpu_overlay_captures_input());
            [window.contentView layoutSubtreeIfNeeded];
            auto cap=(NSPopUpButton*)Find(window.contentView,@"Frame cap"); assert(cap);
            auto res=(NSPopUpButton*)Find(window.contentView,@"Render resolution (restart required)"); assert(res);
            auto scaler=(NSPopUpButton*)Find(window.contentView,@"Upscaling"); assert(scaler);
            auto filter=(NSPopUpButton*)Find(window.contentView,@"Texture filtering"); assert(filter);
            auto temporal=(NSButton*)Find(window.contentView,@"Temporal anti-aliasing (experimental)"); assert(temporal);
            auto save=Find(window.contentView,@"Apply & save");
            auto restart=Find(window.contentView,@"Apply & restart");
            Invoke(Find(window.contentView,@"Achievements…")); assert(achievements_opened==1);
            [cap selectItemAtIndex:1]; Invoke(cap);
            [scaler selectItemAtIndex:1]; Invoke(scaler);
            [filter selectItemAtIndex:3]; Invoke(filter);
            temporal.state=NSControlStateValueOn; Invoke(temporal);
            assert(values.frame_cap==30 && !values.metalfx && restart.enabled);
            Invoke(save);
            assert(values.frame_cap==60 && values.metalfx && applied==1);
            assert(values.anisotropy==8 && values.temporal_aa);
            assert(MacSettings::Load(path)==values);
            [cap selectItemAtIndex:3]; Invoke(cap); Invoke(save);
            assert(values.frame_cap==120 && applied==2 && MacSettings::Load(path)==values);
            [res selectItemAtIndex:1]; Invoke(res); assert(restart.enabled);
            Invoke(restart); assert(restarted && applied==3 && values.render_width==1280);
            assert(MacSettings::Load(path)==values);
            // Closing releases game input; reopening restores the saved values.
            Invoke(Find(window.contentView,@"Resume")); assert(!bbgpu_overlay_captures_input());
            SDL_Event event{}; event.type=SDL_EVENT_KEY_DOWN; event.key.key=SDLK_F1;
            assert(overlay.HandleEvent(event) && bbgpu_overlay_captures_input());
            assert(overlay.HandleEvent(event) && bbgpu_overlay_captures_input()); // same physical F1 observed twice
            assert(cap.indexOfSelectedItem==3 && res.indexOfSelectedItem==1);
            event.key.key=SDLK_ESCAPE; assert(overlay.HandleEvent(event));
            assert(!bbgpu_overlay_captures_input());
            // Resizing to a smaller window retains access to controls via scrolling.
            SDL_SetWindowSize(sdl,800,500); SDL_PumpEvents(); overlay.Toggle();
            [window.contentView layoutSubtreeIfNeeded];
            assert(std::abs(window.contentView.bounds.size.width-800)<1);
            assert(std::abs(window.contentView.bounds.size.height-500)<1);
            assert(save.window==window && restart.window==window);
            overlay.Toggle();
            SDL_VirtualJoystickDesc desc{}; desc.version=sizeof(desc);
            desc.type=SDL_JOYSTICK_TYPE_GAMEPAD; desc.naxes=SDL_GAMEPAD_AXIS_COUNT;
            desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT; desc.name="Bloodborne settings test pad";
            desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
            desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
            const auto id=SDL_AttachVirtualJoystick(&desc); assert(id);
            auto joystick=SDL_OpenJoystick(id); assert(joystick);
            const auto button=[&](SDL_GamepadButton b,bool down) {
                assert(SDL_SetJoystickVirtualButton(joystick,int(b),down));
            };
            button(SDL_GAMEPAD_BUTTON_LEFT_STICK,true); button(SDL_GAMEPAD_BUTTON_RIGHT_STICK,true);
            SDL_PumpEvents(); overlay.Tick(); assert(bbgpu_overlay_captures_input());
            overlay.Tick(); assert(bbgpu_overlay_captures_input()); // held chord toggles once
            button(SDL_GAMEPAD_BUTTON_LEFT_STICK,false); button(SDL_GAMEPAD_BUTTON_RIGHT_STICK,false);
            SDL_PumpEvents(); overlay.Tick();
            button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT,true);
            SDL_PumpEvents(); overlay.Tick(); assert(cap.indexOfSelectedItem==4);
            button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT,false);
            SDL_PumpEvents(); overlay.Tick();
            button(SDL_GAMEPAD_BUTTON_EAST,true);
            SDL_PumpEvents(); overlay.Tick(); assert(!bbgpu_overlay_captures_input());
            button(SDL_GAMEPAD_BUTTON_EAST,false);
            SDL_CloseJoystick(joystick); SDL_DetachVirtualJoystick(id);
        }
        {
            // A new overlay uses the file restored at process startup, rather
            // than retaining the previous overlay's in-memory draft.
            auto restored=MacSettings::Load(path);
            MacSettings::Overlay overlay(sdl,restored,path,[] {});
            overlay.Toggle();
            auto cap=(NSPopUpButton*)Find(window.contentView,@"Frame cap");
            auto scaler=(NSPopUpButton*)Find(window.contentView,@"Upscaling");
            auto res=(NSPopUpButton*)Find(window.contentView,@"Render resolution (restart required)");
            assert(cap.indexOfSelectedItem==3 && scaler.indexOfSelectedItem==1 && res.indexOfSelectedItem==1);
        }
        std::filesystem::remove_all(dir); SDL_DestroyWindow(sdl); SDL_Quit();
        puts("Settings overlay: real Cocoa controls, apply/save/restart, hotkeys and input capture passed");
    }
}
