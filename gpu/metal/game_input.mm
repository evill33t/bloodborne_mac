// SPDX-License-Identifier: GPL-2.0-or-later
// Adapted from bbport's SDL input flow; no graphics API dependency.
#include "game_input.h"
#include "gpu/bbgpu.h"
#import <AppKit/AppKit.h>
#include <SDL3/SDL.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <cstdio>

namespace MacInput {
std::atomic<bool> CapturesInput{false};
namespace {
struct State {
    std::mutex mutex;
    bool available=false, configured=false, mouse_available=false, mouse_on=false;
    bool reload=false, text_requested=false, text_active=false, release_guard=false;
    int32_t toggle=0, reload_key=0;
    int text_result=2;
    std::string initial, prompt, text;
    BbMouseInput mouse{};
    std::array<Uint64,4> wheel_until{};
};
// Completion blocks own the state, even if a window closes with a sheet open.
std::shared_ptr<State> state=std::make_shared<State>();
void ClearMouse(State& s) { s.mouse={}; s.wheel_until={}; }
bool DialogButtonHeld() {
    if (!SDL_WasInit(SDL_INIT_VIDEO)) return false;
    const auto keys=SDL_GetKeyboardState(nullptr);
    bool held=keys && (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER] || keys[SDL_SCANCODE_ESCAPE]);
    int count=0; auto ids=SDL_GetGamepads(&count);
    for (int i=0;ids && i<count;++i) if (auto pad=SDL_GetGamepadFromID(ids[i]))
        held|=SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_SOUTH) || SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_EAST);
    SDL_free(ids); return held;
}
void CopyText(char* out,uint64_t size,const std::string& text) {
    if (!out || !size) return;
    size_t n=std::min<uint64_t>(text.size(),size-1);
    // Do not return half a UTF-8 codepoint when the guest provides a small buffer.
    while (n<text.size() && n && (static_cast<unsigned char>(text[n])&0xc0)==0x80) --n;
    std::memcpy(out,text.data(),n); out[n]=0;
}
}
struct Bridge::Impl {
    SDL_Window* window;
    NSWindow* cocoa;
    NSAlert* alert=nil;
    NSTextField* field=nil;
    bool focused=false, captured=false, blocked=false;
    bool accept_held=false,cancel_held=false;
    explicit Impl(SDL_Window* w):window(w) {
        cocoa=(__bridge NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(w),
            SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,nullptr);
        focused=(SDL_GetWindowFlags(w)&SDL_WINDOW_INPUT_FOCUS)!=0;
    }
    void Release() {
        if (captured) SDL_SetWindowRelativeMouseMode(window,false);
        captured=false;
        std::scoped_lock lock(state->mutex); ClearMouse(*state);
    }
};
Bridge::Bridge(SDL_Window* window):impl(std::make_unique<Impl>(window)) {
    std::scoped_lock lock(state->mutex);
    state->available=true; state->configured=false;
    state->mouse_available=state->mouse_on=false;
    state->reload=state->text_requested=state->text_active=false;
    state->release_guard=false;
    state->text_result=2; ClearMouse(*state); CapturesInput=false;
}
Bridge::~Bridge() {
    impl->Release();
    if (impl->alert.window.sheetParent) [impl->cocoa endSheet:impl->alert.window returnCode:NSAlertSecondButtonReturn];
    std::scoped_lock lock(state->mutex);
    state->available=false; state->text_active=state->text_requested=false;
    state->release_guard=false;
    state->text_result=2; CapturesInput=false;
}
bool Bridge::HandleEvent(const SDL_Event& e) {
    if (e.type==SDL_EVENT_WINDOW_FOCUS_LOST) { impl->focused=false; impl->Release(); }
    if (e.type==SDL_EVENT_WINDOW_FOCUS_GAINED) impl->focused=true;
    std::scoped_lock lock(state->mutex);
    if (e.type==SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        if (state->reload_key && e.key.scancode==state->reload_key) { state->reload=true; return true; }
        if (state->toggle && e.key.scancode==state->toggle && state->mouse_available && !state->text_active) {
            state->mouse_on=!state->mouse_on;
            // Tick applies capture immediately after this event, before the next mouse event.
            return true;
        }
    }
    if (!state->mouse.captured || impl->blocked || CapturesInput.load()) return false;
    if (e.type==SDL_EVENT_MOUSE_MOTION) {
        state->mouse.dx+=e.motion.xrel; state->mouse.dy+=e.motion.yrel;
    } else if (e.type==SDL_EVENT_MOUSE_BUTTON_DOWN || e.type==SDL_EVENT_MOUSE_BUTTON_UP) {
        if (e.button.button<1 || e.button.button>32) return false;
        const auto bit=1u<<(e.button.button-1);
        if (e.type==SDL_EVENT_MOUSE_BUTTON_DOWN) state->mouse.buttons|=bit;
        else state->mouse.buttons&=~bit;
    } else if (e.type==SDL_EVENT_MOUSE_WHEEL) {
        const Uint64 until=SDL_GetTicks()+33;
        const float sign=e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
        if (e.wheel.y*sign>0) state->wheel_until[0]=until;
        if (e.wheel.y*sign<0) state->wheel_until[1]=until;
        if (e.wheel.x*sign<0) state->wheel_until[2]=until;
        if (e.wheel.x*sign>0) state->wheel_until[3]=until;
    } else return false;
    return true;
}
void Bridge::Tick(bool blocked) {
    impl->blocked=blocked;
    bool requested=false,wants=false;
    std::string initial,prompt;
    {
        std::scoped_lock lock(state->mutex);
        if (state->release_guard && !DialogButtonHeld()) {
            state->release_guard=false; CapturesInput=state->text_active;
        }
        requested=std::exchange(state->text_requested,false);
        if (requested) { initial=state->initial; prompt=state->prompt; }
        wants=state->mouse_available && state->mouse_on && impl->focused && !blocked &&
            !state->text_active && !state->release_guard;
    }
    if (wants!=impl->captured) {
        impl->Release();
        if (wants && SDL_SetWindowRelativeMouseMode(impl->window,true)) {
            impl->captured=true;
            std::scoped_lock lock(state->mutex); state->mouse.captured=1;
        }
    }
    if (requested) {
        impl->alert=[NSAlert new];
        impl->alert.messageText=[NSString stringWithUTF8String:prompt.c_str()] ?: @"Character name";
        impl->alert.informativeText=@"Enter text, then choose OK. Return confirms; Escape cancels.";
        [impl->alert addButtonWithTitle:@"OK"]; [impl->alert addButtonWithTitle:@"Cancel"];
        impl->alert.buttons[1].keyEquivalent=@"\033";
        impl->field=[[NSTextField alloc] initWithFrame:NSMakeRect(0,0,320,24)];
        impl->field.stringValue=[NSString stringWithUTF8String:initial.c_str()] ?: @"";
        impl->field.accessibilityLabel=impl->alert.messageText;
        impl->alert.accessoryView=impl->field;
        auto shared=state;
        auto field=impl->field;
        [impl->alert beginSheetModalForWindow:impl->cocoa completionHandler:^(NSModalResponse response) {
            std::scoped_lock lock(shared->mutex);
            shared->text=field.stringValue.UTF8String ?: "";
            shared->text_result=response==NSAlertFirstButtonReturn ? 1 : 2;
            shared->text_active=false;
            // Confirmation must not also become an attack/Options press on
            // the first game sample after the dialog closes.
            shared->release_guard=DialogButtonHeld();
            CapturesInput=shared->release_guard;
        }];
        [impl->alert.window makeFirstResponder:impl->field];
        [impl->field selectText:nil];
        // A held button that opened the game dialog must not immediately accept it.
        impl->accept_held=impl->cancel_held=true;
    }
    if (impl->alert.window.sheetParent) {
        bool accept=false,cancel=false;
        int count=0; auto ids=SDL_GetGamepads(&count);
        for (int i=0;ids && i<count;++i) if (auto pad=SDL_GetGamepadFromID(ids[i])) {
            accept|=SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_SOUTH);
            cancel|=SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_EAST);
        }
        SDL_free(ids);
        if ((accept && !impl->accept_held) || (cancel && !impl->cancel_held))
            [impl->cocoa endSheet:impl->alert.window returnCode:cancel ? NSAlertSecondButtonReturn : NSAlertFirstButtonReturn];
        impl->accept_held=accept; impl->cancel_held=cancel;
    }
}
}

extern "C" int bbgpu_text_input_begin(const char* initial,const char* prompt) {
    auto& s=*MacInput::state;
    std::scoped_lock lock(s.mutex);
    if (!s.available || s.text_active) return 0;
    s.initial=initial ? initial : ""; s.prompt=prompt ? prompt : "Text";
    s.text=s.initial; s.text_result=0; s.text_active=s.text_requested=true;
    MacInput::ClearMouse(s); MacInput::CapturesInput=true;
    return 1;
}
extern "C" int bbgpu_text_input_poll(char* out,uint64_t size) {
    auto& s=*MacInput::state;
    std::scoped_lock lock(s.mutex);
    MacInput::CopyText(out,size,s.text);
    return s.available ? s.text_result : 2;
}
extern "C" void bbgpu_mouse_take(BbMouseInput* out) {
    if (!out) return;
    auto& s=*MacInput::state;
    std::scoped_lock lock(s.mutex);
    s.mouse.wheel=0;
    const auto now=SDL_GetTicks();
    for (unsigned i=0;i<4;++i) if (s.wheel_until[i]>now) s.mouse.wheel|=1u<<i;
    *out=s.mouse; s.mouse.dx=s.mouse.dy=0;
}
extern "C" void bbgpu_input_configure(int available,int32_t toggle,int32_t reload) {
    auto& s=*MacInput::state;
    std::scoped_lock lock(s.mutex);
    s.mouse_available=available!=0; s.toggle=toggle; s.reload_key=reload;
    if (!s.mouse_available) { s.mouse_on=false; MacInput::ClearMouse(s); }
    else if (!s.configured) { s.mouse_on=true; s.configured=true; }
}
extern "C" int bbgpu_input_reload_requested() {
    auto& s=*MacInput::state;
    std::scoped_lock lock(s.mutex);
    return std::exchange(s.reload,false);
}
extern "C" void bbgpu_mouse_camera_configure(int direct,float) {
    // Preserve configured mouse-to-stick input when the optional Windows
    // direct-camera patch is requested; no unverified guest hooks on Darwin.
    static std::atomic<bool> reported{false};
    if (direct && !reported.exchange(true))
        std::fprintf(stderr,"Metal input: direct-camera mode uses the configured mouse-to-stick mapping.\n");
}
