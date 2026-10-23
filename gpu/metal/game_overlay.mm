// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_overlay.h"
#include "gpu/metal/bbhost/features.h"
#include "gpu/metal/bbhost/network.h"
#include "host/plugins.h"
#include "core/config.h"
#include "bbhost_plugin.h"
#include <SDL3/SDL.h>
#import <AppKit/AppKit.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include <array>
#include <map>

extern "C" void runtime_restart(void);
@interface BBSettingsActions : NSObject
@property(copy) void (^action)(id);
- (void)invoke:(id)sender;
@end
@implementation BBSettingsActions
- (void)invoke:(id)sender { if (self.action) self.action(sender); }
@end
@interface BBSettingsClipView : NSClipView
@end
@implementation BBSettingsClipView
- (BOOL)isFlipped { return YES; }
@end

namespace MacSettings {
std::atomic<bool> CapturesInput{false};
namespace {
constexpr int FrameCaps[]{30,60,90,120,144,165,240,0};
NSTextField* Label(NSString* text,CGFloat size=13,bool bold=false) {
    auto label=[NSTextField wrappingLabelWithString:text];
    label.font=bold ? [NSFont boldSystemFontOfSize:size] : [NSFont systemFontOfSize:size];
    label.textColor=NSColor.labelColor; return label;
}
NSStackView* Stack(NSArray<NSView*>* views,NSUserInterfaceLayoutOrientation orientation,CGFloat space=10) {
    auto stack=[NSStackView stackViewWithViews:views];
    stack.orientation=orientation; stack.spacing=space;
    stack.alignment=orientation==NSUserInterfaceLayoutOrientationVertical ? NSLayoutAttributeLeading : NSLayoutAttributeTop;
    return stack;
}
}
struct Overlay::Impl {
    SDL_Window* sdl;
    NSWindow* window;
    NSView* game_view;
    NSView* root;
    NSVisualEffectView* panel;
    BBSettingsActions* actions;
    id monitor;
    Values& values;
    Values startup;
    std::filesystem::path path;
    std::function<void()> apply;
    std::function<void()> show_achievements;
    NSPopUpButton *cap,*resolution,*upscaler,*sharpness,*model_detail,*anisotropy;
    NSButton* temporal_aa;
    NSButton* frame_generation;
    NSButton* optimized_lights;
    NSButton *vsync,*fullscreen,*fps,*aa,*blur,*dof,*ca,*ssao,*shadows,*save,*restart,*defaults,*close,*achievements;
    NSTextField *status,*message;
    NSTextField *server,*matchmaking,*account_status,*toast;
    NSButton *online,*sign_in,*sign_out;
    NSMenuItem* settings_item;
    std::map<NSControl*,std::function<void()>> plugin_actions;
    std::map<std::string,NSButton*> plugin_switches;
    std::map<std::string,bool> startup_plugins;
    Uint64 toast_until=0;
    bool plugins_changed=false;
    std::vector<NSControl*> controls;
    std::map<SDL_JoystickID,SDL_Gamepad*> pads;
    bool open=false, combo_held=false, metalfx_supported=false;
    int focus=0;
    Uint64 next_nav=0;
    Uint64 last_shortcut=0;
    int last_nav=0;
    explicit Impl(SDL_Window* s,Values& v,const std::filesystem::path& p,std::function<void()> fn)
        :sdl(s),values(v),startup(v),path(p),apply(std::move(fn)) {}

    NSPopUpButton* Popup(NSArray<NSString*>* titles,NSString* name) {
        auto control=[[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
        [control addItemsWithTitles:titles]; [control setAccessibilityLabel:name];
        control.target=actions; control.action=@selector(invoke:);
        controls.push_back(control); return control;
    }
    NSButton* Check(NSString* title) {
        auto control=[NSButton checkboxWithTitle:title target:actions action:@selector(invoke:)];
        [control setAccessibilityLabel:title]; controls.push_back(control); return control;
    }
    NSButton* Button(NSString* title) {
        auto control=[NSButton buttonWithTitle:title target:actions action:@selector(invoke:)];
        controls.push_back(control); return control;
    }
    NSView* Field(NSString* name,NSView* control) {
        return Stack(@[Label(name,12,true),control],NSUserInterfaceLayoutOrientationVertical,3);
    }
    NSTextField* TextField(NSString* name) {
        auto field=[NSTextField textFieldWithString:@""];
        field.placeholderString=name; [field setAccessibilityLabel:name];
        field.target=actions; field.action=@selector(invoke:);
        [field.widthAnchor constraintGreaterThanOrEqualToConstant:280].active=YES;
        controls.push_back(field); return field;
    }
    NSView* PluginFields() {
        NSMutableArray<NSView*>* rows=[NSMutableArray arrayWithObjects:Label(@"MODS & PLUGINS",12,true),
            Label(@"Plugin selections are saved immediately and apply at restart. Live options and actions take effect while playing. Gameplay plugins change your save's world.",12),nil];
        auto catalog=plugins_scan();
        for (const auto& live:plugins_catalog()) {
            auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto& entry) { return entry.name==live.name; });
            if (found==catalog.end()) catalog.push_back(live); else *found=live;
        }
        for (const auto& p:catalog) {
            auto enabled=Check([NSString stringWithUTF8String:p.title.c_str()]);
            enabled.state=p.enabled;
            plugin_switches[p.name]=enabled; startup_plugins[p.name]=p.loaded && p.enabled;
            plugin_actions[enabled]=[this,enabled,name=p.name] {
                plugins_set_enabled(name,enabled.state==NSControlStateValueOn); plugins_changed=true; RefreshMessage();
            };
            [rows addObject:enabled];
            auto detail=Label([NSString stringWithUTF8String:p.description.c_str()],11);
            [detail.widthAnchor constraintLessThanOrEqualToConstant:620].active=YES; [rows addObject:detail];
            for (const auto& o:p.options) {
                const auto label=[NSString stringWithUTF8String:o.label.c_str()];
                const auto current=plugins_option_value(p,o);
                if (o.type==BB_OPT_HEADING) { [rows addObject:Label(label,11,true)]; continue; }
                NSControl* control=nil;
                if (o.type==BB_OPT_BOOL) {
                    auto check=Check(label); check.state=current=="true" || current=="1"; control=check;
                    plugin_actions[check]=[this,check,p,o] { plugins_set_option(p.name,o.key,check.state==NSControlStateValueOn ? "true" : "false"); if (!o.live) plugins_changed=true; RefreshMessage(); };
                } else if (o.type==BB_OPT_CHOICE) {
                    auto options=[[NSString stringWithUTF8String:o.choices.c_str()] componentsSeparatedByString:@"|"];
                    auto popup=Popup(options,label); [popup selectItemWithTitle:[NSString stringWithUTF8String:current.c_str()]]; control=popup;
                    plugin_actions[popup]=[this,popup,p,o] { plugins_set_option(p.name,o.key,popup.titleOfSelectedItem.UTF8String); if (!o.live) plugins_changed=true; RefreshMessage(); };
                } else if (o.type==BB_OPT_ACTION) {
                    auto button=Button(label); button.enabled=p.loaded && p.enabled; control=button;
                    plugin_actions[button]=[p,o] { plugins_run_action(p.name,o.key); };
                } else {
                    auto field=TextField(label); field.stringValue=[NSString stringWithUTF8String:current.c_str()]; control=field;
                    plugin_actions[field]=[this,field,p,o] {
                        std::string value=field.stringValue.UTF8String;
                        if (o.type==BB_OPT_INT || o.type==BB_OPT_FLOAT) {
                            try { std::size_t used=0; const double n=std::stod(value,&used);
                                if (used!=value.size() || !std::isfinite(n) || n<o.min || n>o.max || (o.type==BB_OPT_INT && std::floor(n)!=n)) throw std::invalid_argument("range");
                            } catch (...) { message.stringValue=@"Enter a number within this option's range."; return; }
                        }
                        plugins_set_option(p.name,o.key,value); if (!o.live) plugins_changed=true; RefreshMessage();
                    };
                }
                [control setAccessibilityHelp:[NSString stringWithUTF8String:o.help.c_str()]];
                control.toolTip=[NSString stringWithUTF8String:o.help.c_str()];
                if (o.type==BB_OPT_BOOL || o.type==BB_OPT_ACTION) [rows addObject:control];
                else [rows addObject:Field(label,control)];
            }
        }
        return Stack(rows,NSUserInterfaceLayoutOrientationVertical,10);
    }
    void Build(Overlay* owner) {
        window=(__bridge NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(sdl),
            SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,nullptr);
        game_view=window.contentView;
        root=[[NSView alloc] initWithFrame:game_view.bounds];
        root.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
        root.wantsLayer=YES; root.layer.backgroundColor=[NSColor colorWithWhite:0 alpha:.62].CGColor;
        root.appearance=[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        root.hidden=YES;
        [game_view addSubview:root positioned:NSWindowAbove relativeTo:nil];
        panel=[NSVisualEffectView new]; panel.material=NSVisualEffectMaterialHUDWindow;
        panel.blendingMode=NSVisualEffectBlendingModeWithinWindow; panel.state=NSVisualEffectStateActive;
        panel.wantsLayer=YES; panel.layer.cornerRadius=16; panel.layer.masksToBounds=YES;
        panel.layer.borderWidth=1; panel.layer.borderColor=[NSColor colorWithWhite:.6 alpha:.3].CGColor;
        panel.translatesAutoresizingMaskIntoConstraints=NO; [root addSubview:panel];
        auto width=[panel.widthAnchor constraintEqualToConstant:760]; width.priority=750;
        auto height=[panel.heightAnchor constraintEqualToConstant:600]; height.priority=750;
        [NSLayoutConstraint activateConstraints:@[width,height,
            [panel.widthAnchor constraintLessThanOrEqualToAnchor:root.widthAnchor constant:-32],
            [panel.heightAnchor constraintLessThanOrEqualToAnchor:root.heightAnchor constant:-32],
            [panel.centerXAnchor constraintEqualToAnchor:root.centerXAnchor],
            [panel.centerYAnchor constraintEqualToAnchor:root.centerYAnchor]]];
        actions=[BBSettingsActions new];
        actions.action=^(id sender) { this->Action(sender); };
        settings_item=[[NSMenuItem alloc] initWithTitle:@"Settings…" action:@selector(invoke:) keyEquivalent:@","];
        settings_item.target=actions;
        if (NSApp.mainMenu.itemArray.firstObject.submenu) [NSApp.mainMenu.itemArray.firstObject.submenu insertItem:settings_item atIndex:1];
        cap=Popup(@[@"30 FPS",@"60 FPS",@"90 FPS",@"120 FPS",@"144 FPS",@"165 FPS",@"240 FPS",@"Uncapped"],@"Frame cap");
        cap.enabled=std::getenv("BB_MAC_TIMESTEP")!=nullptr;
        NSMutableArray* sizes=[NSMutableArray array];
        for (int k=0;k<ResolutionCount;++k) [sizes addObject:[NSString stringWithFormat:@"%d × %d",Widths[k],Heights[k]]];
        resolution=Popup(sizes,@"Render resolution (restart required)");
        upscaler=Popup(@[@"Bilinear",@"MetalFX Spatial"],@"Upscaling");
        sharpness=Popup(@[@"Off",@"10%",@"20%",@"30%",@"40%",@"50%",@"60%",@"70%",@"80%",@"90%",@"100%"],@"Additional sharpening");
        anisotropy=Popup(@[@"Game default",@"2×",@"4×",@"8×",@"16×"],@"Texture filtering");
        frame_generation=Check(@"2× MetalFX frame generation (experimental)");
        frame_generation.toolTip=@"Adds intermediate display frames at native 1080p. Requires supported Apple Silicon, macOS 26 and V-sync. Input and simulation follow the rendered FPS.";
        temporal_aa=Check(@"Temporal anti-aliasing (experimental)");
        temporal_aa.toolTip=@"Stabilizes scene edges before the HUD. Uses camera motion and depth rejection; moving objects may show ghosting.";
        vsync=Check(@"V-sync"); fullscreen=Check(@"Fullscreen"); fps=Check(@"FPS in title bar");
        aa=Check(@"Game anti-aliasing"); blur=Check(@"Motion blur"); dof=Check(@"Depth of field");
        ca=Check(@"Chromatic aberration"); ssao=Check(@"Ambient occlusion"); shadows=Check(@"Dynamic light shadows");
        optimized_lights=Check(@"Optimize light grid (experimental)");
        model_detail=Popup(@[@"Highest",@"Original",@"Lower",@"Lowest"],@"Model detail");
        auto left=Stack(@[Label(@"DISPLAY & PERFORMANCE",11,true),Field(@"Frame cap · restart required",cap),
            Field(@"Render resolution · restart required",resolution),Field(@"Upscaling",upscaler),
            Field(@"Additional sharpening",sharpness),Field(@"Texture filtering",anisotropy),temporal_aa,frame_generation,
            vsync,fullscreen,fps],NSUserInterfaceLayoutOrientationVertical,10);
        auto note=Label(@"Frame rate, resolution and effects apply after a restart. Rates above 120 FPS are experimental. MetalFX scales the game HUD with the scene.",12);
        note.textColor=NSColor.secondaryLabelColor;
        auto right=Stack(@[Label(@"GRAPHICS · RESTART REQUIRED",11,true),aa,blur,dof,ca,ssao,shadows,note,
            Label(@"LIVE PERFORMANCE",11,true),optimized_lights,Field(@"Model detail",model_detail)],
            NSUserInterfaceLayoutOrientationVertical,16);
        auto columns=Stack(@[left,right],NSUserInterfaceLayoutOrientationHorizontal,32);
        [left.widthAnchor constraintEqualToAnchor:right.widthAnchor].active=YES;
        [note.widthAnchor constraintLessThanOrEqualToConstant:310].active=YES;
        for (auto popup : {cap,resolution,upscaler,sharpness}) [popup.widthAnchor constraintEqualToConstant:280].active=YES;
        auto scroll=[NSScrollView new]; scroll.drawsBackground=NO; scroll.hasVerticalScroller=YES;
        scroll.contentView=[BBSettingsClipView new];
        scroll.contentView.drawsBackground=NO;
        online=Check(@"Play online after restart");
        server=TextField(@"Game and account server"); matchmaking=TextField(@"Matchmaking server");
        sign_in=Button(@"Sign in with Discord"); sign_out=Button(@"Sign out");
        account_status=Label(@"Sign in to play online.",12);
        auto network=Stack(@[Label(@"ONLINE · PRIVATE SERVER",12,true),online,
            Field(@"Game and account server",server),Field(@"Matchmaking server",matchmaking),
            Stack(@[sign_in,sign_out],NSUserInterfaceLayoutOrientationHorizontal),account_status,
            Label(@"Server changes apply after restart. Use the same server as the other hunters. Your account token is kept in macOS Keychain.",12)],NSUserInterfaceLayoutOrientationVertical,10);
        auto document=Stack(@[columns,network,PluginFields()],NSUserInterfaceLayoutOrientationVertical,28);
        scroll.documentView=document; document.translatesAutoresizingMaskIntoConstraints=NO;
        [document.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor].active=YES;
        [document.topAnchor constraintEqualToAnchor:scroll.contentView.topAnchor].active=YES;
        status=Label(@"Starting the renderer…",12); status.textColor=NSColor.secondaryLabelColor;
        message=Label(@"F1 / L3 + R3 to close. The game keeps running while settings are open.",12);
        message.textColor=NSColor.secondaryLabelColor;
        defaults=Button(@"Restore defaults"); save=Button(@"Apply & save");
        restart=Button(@"Apply & restart"); close=Button(@"Resume");
        achievements=Button(@"Achievements…"); achievements.enabled=bool(show_achievements);
        auto buttons=Stack(@[defaults,save,restart,achievements,close],NSUserInterfaceLayoutOrientationHorizontal,10);
        auto content=Stack(@[Label(@"Settings",26,true),Label(@"BLOODBORNE  ·  DIRECT METAL",11,true),
            status,scroll,message,buttons],NSUserInterfaceLayoutOrientationVertical,12);
        content.translatesAutoresizingMaskIntoConstraints=NO; [panel addSubview:content];
        [NSLayoutConstraint activateConstraints:@[
            [content.topAnchor constraintEqualToAnchor:panel.topAnchor constant:24],
            [content.bottomAnchor constraintEqualToAnchor:panel.bottomAnchor constant:-24],
            [content.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor constant:24],
            [content.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor constant:-24],
            [scroll.widthAnchor constraintEqualToAnchor:content.widthAnchor],
            [scroll.heightAnchor constraintGreaterThanOrEqualToConstant:100]]];
        [scroll setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationVertical];
        toast=Label(@"",13); toast.hidden=YES; toast.backgroundColor=[NSColor colorWithWhite:0 alpha:.85];
        toast.drawsBackground=YES; toast.textColor=NSColor.whiteColor;
        toast.translatesAutoresizingMaskIntoConstraints=NO; [game_view addSubview:toast positioned:NSWindowAbove relativeTo:root];
        [NSLayoutConstraint activateConstraints:@[[toast.centerXAnchor constraintEqualToAnchor:game_view.centerXAnchor],
            [toast.bottomAnchor constraintEqualToAnchor:game_view.bottomAnchor constant:-30],
            [toast.widthAnchor constraintLessThanOrEqualToAnchor:game_view.widthAnchor constant:-64]]];
        monitor=[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent*(NSEvent* e) {
            // SDL also observes native key events. A local monitor covers
            // keys while a Cocoa control or popup is the first responder.
            if (e.keyCode==122 || e.keyCode==114 ||
                ((e.modifierFlags & NSEventModifierFlagCommand) && [e.characters isEqualToString:@","])) {
                if (!e.isARepeat) owner->Shortcut(); return nil;
            }
            if (this->open && e.keyCode==53) { owner->Toggle(); return nil; }
            return e;
        }];
    }
    Values Read() const {
        Values v=values;
        v.frame_cap=FrameCaps[std::clamp<int>(int(cap.indexOfSelectedItem),0,int(std::size(FrameCaps))-1)];
        int r=std::clamp<int>(int(resolution.indexOfSelectedItem),0,ResolutionCount-1);
        v.render_width=Widths[r]; v.render_height=Heights[r];
        v.metalfx=upscaler.indexOfSelectedItem==1;
        const int saved_sharpness=std::clamp(int(std::round(values.sharpness*10)),0,10);
        v.sharpness=int(sharpness.indexOfSelectedItem)==saved_sharpness ? values.sharpness : float(sharpness.indexOfSelectedItem)/10;
        constexpr int filters[]{0,2,4,8,16};
        v.anisotropy=filters[std::clamp(int(anisotropy.indexOfSelectedItem),0,4)];
        v.temporal_aa=temporal_aa.state==NSControlStateValueOn;
        v.frame_generation=frame_generation.state==NSControlStateValueOn;
        v.vsync=vsync.state==NSControlStateValueOn; v.fullscreen=fullscreen.state==NSControlStateValueOn;
        v.show_fps=fps.state==NSControlStateValueOn;
        v.game_aa=aa.state==NSControlStateValueOn; v.motion_blur=blur.state==NSControlStateValueOn;
        v.dof=dof.state==NSControlStateValueOn; v.chromatic_aberration=ca.state==NSControlStateValueOn;
        v.ssao=ssao.state==NSControlStateValueOn; v.dynamic_shadows=shadows.state==NSControlStateValueOn;
        v.online=online.state==NSControlStateValueOn;
        v.optimized_lights=optimized_lights.state==NSControlStateValueOn;
        static constexpr int lods[]{-2,0,1,2};
        v.model_lod=lods[std::clamp(int(model_detail.indexOfSelectedItem),0,3)];
        v.server_url=server.stringValue.UTF8String; v.matchmaking_url=matchmaking.stringValue.UTF8String;
        Validate(v); return v;
    }
    void Populate(const Values& v) {
        const auto found=std::find(std::begin(FrameCaps),std::end(FrameCaps),v.frame_cap);
        [cap selectItemAtIndex:found==std::end(FrameCaps) ? 0 : found-std::begin(FrameCaps)];
        for (int k=0;k<ResolutionCount;++k) if (v.render_width==Widths[k]) [resolution selectItemAtIndex:k];
        [upscaler selectItemAtIndex:v.metalfx ? 1 : 0];
        [anisotropy selectItemAtIndex:v.anisotropy==0 ? 0 : v.anisotropy==2 ? 1 : v.anisotropy==4 ? 2 : v.anisotropy==8 ? 3 : 4];
        temporal_aa.state=v.temporal_aa;
        frame_generation.state=v.frame_generation;
        [sharpness selectItemAtIndex:std::clamp(int(std::round(v.sharpness*10)),0,10)];
        vsync.state=v.vsync; fullscreen.state=v.fullscreen; fps.state=v.show_fps;
        aa.state=v.game_aa; blur.state=v.motion_blur; dof.state=v.dof;
        ca.state=v.chromatic_aberration; ssao.state=v.ssao; shadows.state=v.dynamic_shadows;
        online.state=v.online; server.stringValue=[NSString stringWithUTF8String:v.server_url.c_str()];
        optimized_lights.state=v.optimized_lights;
        [model_detail selectItemAtIndex:v.model_lod==-2 ? 0 : v.model_lod==0 ? 1 : v.model_lod==1 ? 2 : 3];
        matchmaking.stringValue=[NSString stringWithUTF8String:v.matchmaking_url.c_str()];
        for (const auto& [name,control]:plugin_switches) {
            const auto selected=config_value("plugins."+name);
            const bool enabled=selected.empty() ? startup_plugins[name] : selected=="true" || selected=="1";
            control.state=enabled;
            if (enabled!=startup_plugins[name]) plugins_changed=true;
        }
        RefreshMessage();
    }
    void RefreshMessage() {
        const bool pending=NeedsRestart(Read(),startup) || plugins_changed;
        restart.enabled=pending;
        message.stringValue=pending ? @"Changes need a restart. Restart reloads your last game save."
            : @"F1 / L3 + R3 to close. The game keeps running while settings are open.";
    }
    void SetOpen(bool value) {
        std::fprintf(stderr,"Settings menu: %s\n",value ? "open" : "closed");
        open=value; CapturesInput.store(value,std::memory_order_release);
        root.hidden=!value;
        if (value) { Populate(values); focus=0; [window makeFirstResponder:controls[0]]; }
        else [window makeFirstResponder:game_view];
    }
    void Action(id sender) {
        if (sender==achievements) { if (show_achievements) show_achievements(); return; }
        if (sender==settings_item) { SetOpen(!open); return; }
        if (sender==sign_in) {
            if (Read().server_url!=startup.server_url) { message.stringValue=@"Apply and restart before signing into a different server."; return; }
            MacNetwork::LinkAccount(); return;
        }
        if (sender==sign_out) { MacNetwork::SignOut(); return; }
        if (auto found=plugin_actions.find(sender);found!=plugin_actions.end()) { found->second(); return; }
        if (sender==close) { SetOpen(false); return; }
        if (sender==defaults) { Populate(Values{}); return; }
        if (sender==save || sender==restart) {
            [window makeFirstResponder:save]; // commit a plugin text field before applying
            if (!ValidServer(server.stringValue.UTF8String) || !ValidServer(matchmaking.stringValue.UTF8String)) {
                message.stringValue=@"Enter an HTTP or HTTPS server address, with an optional port."; return;
            }
            Values requested=Read(); std::string error;
            if (!Save(path,requested,error)) {
                message.stringValue=[NSString stringWithFormat:@"Could not save: %s",error.c_str()]; return;
            }
            values=requested; apply(); RefreshMessage();
            std::fprintf(stderr,"Settings applied: cap=%d render=%dx%d MetalFX=%d restart=%d\n",
                values.frame_cap,values.render_width,values.render_height,values.metalfx,NeedsRestart(values,startup));
            if (sender==restart && (NeedsRestart(values,startup) || plugins_changed)) runtime_restart();
            if (!NeedsRestart(values,startup) && !plugins_changed) message.stringValue=@"Settings applied and saved.";
            return;
        }
        RefreshMessage();
    }
    void Navigate(int command) {
        if (!open) return;
        if (command==1 || command==2) {
            const int step=command==1 ? -1 : 1;
            do { focus=(focus+step+int(controls.size()))%int(controls.size()); } while (!controls[focus].enabled);
            [window makeFirstResponder:controls[focus]];
            [controls[focus] scrollRectToVisible:controls[focus].bounds];
        } else if (command==3 || command==4 || command==5) {
            auto control=controls[focus];
            if ([control isKindOfClass:NSPopUpButton.class]) {
                auto popup=(NSPopUpButton*)control;
                const int step=command==3 ? -1 : 1;
                int n=int(popup.numberOfItems),selected=int(popup.indexOfSelectedItem);
                do { selected=(selected+step+n)%n; } while (![popup itemAtIndex:selected].enabled);
                [popup selectItemAtIndex:selected]; Action(popup);
            } else if (command==5 && [control isKindOfClass:NSButton.class]) [(NSButton*)control performClick:nil];
        } else if (command==6) SetOpen(false);
    }
};
Overlay::Overlay(SDL_Window* window,Values& settings,const std::filesystem::path& path,std::function<void()> apply,std::function<void()> achievements)
    :impl(std::make_unique<Impl>(window,settings,path,std::move(apply))) {
    impl->show_achievements=std::move(achievements); impl->Build(this);
}
Overlay::~Overlay() {
    [NSEvent removeMonitor:impl->monitor]; [impl->root removeFromSuperview];
    [impl->toast removeFromSuperview]; [impl->settings_item.menu removeItem:impl->settings_item];
    for (auto [id,pad] : impl->pads) SDL_CloseGamepad(pad);
    CapturesInput.store(false,std::memory_order_release);
}
void Overlay::Toggle() { impl->SetOpen(!impl->open); }
void Overlay::Shortcut() {
    // SDL's Cocoa event watcher may enqueue the key before AppKit's local
    // monitor consumes it. Treat both observations as one physical press.
    const auto now=SDL_GetTicksNS();
    if (impl->last_shortcut && now-impl->last_shortcut<150000000ull) return;
    impl->last_shortcut=now; Toggle();
}
bool Overlay::HandleEvent(const SDL_Event& e) {
    if (e.type==SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key==SDLK_GRAVE && !impl->open) {
        MacFeatures::RequestDebugToggle(); return true;
    }
    if (e.type==SDL_EVENT_KEY_DOWN && !e.key.repeat && (e.key.key==SDLK_F1 || e.key.key==SDLK_INSERT)) {
        Shortcut(); return true;
    }
    if (impl->open && e.type==SDL_EVENT_KEY_DOWN && e.key.key==SDLK_ESCAPE) {
        impl->SetOpen(false); return true;
    }
    return impl->open;
}
void Overlay::Tick() {
    const auto text=MacFeatures::TakeMessage();
    if (!text.empty()) {
        impl->toast.stringValue=[NSString stringWithUTF8String:text.c_str()];
        impl->toast.hidden=NO; impl->toast_until=SDL_GetTicks()+8000;
    }
    if (impl->toast_until && SDL_GetTicks()>=impl->toast_until) impl->toast.hidden=YES;
    impl->account_status.stringValue=[NSString stringWithUTF8String:MacNetwork::AccountStatus().c_str()];
    impl->sign_in.enabled=!MacNetwork::AccountBusy();
    const auto link=MacNetwork::TakeLinkUrl();
    if (!link.empty()) [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:[NSString stringWithUTF8String:link.c_str()]]];
    if (MacFeatures::TakeRestartRequest()) {
        NSAlert* alert=[NSAlert new]; alert.messageText=@"Restart to apply settings?";
        alert.informativeText=@"The game will reload your last save.";
        [alert addButtonWithTitle:@"Restart"]; [alert addButtonWithTitle:@"Cancel"];
        const bool captured=CapturesInput.exchange(true);
        const auto answer=[alert runModal]; CapturesInput=captured;
        if (answer==NSAlertFirstButtonReturn) runtime_restart();
    }
    SDL_UpdateGamepads();
    int count=0; auto ids=SDL_GetGamepads(&count);
    std::array<bool,SDL_GAMEPAD_BUTTON_COUNT> buttons{};
    bool combo=false;
    for (int k=0;k<count;++k) {
        auto& pad=impl->pads[ids[k]];
        if (!pad) pad=SDL_OpenGamepad(ids[k]);
        if (!pad) continue;
        for (int b=0;b<SDL_GAMEPAD_BUTTON_COUNT;++b)
            buttons[b] |= SDL_GetGamepadButton(pad,SDL_GamepadButton(b));
        combo |= SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_LEFT_STICK) &&
                 SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    }
    SDL_free(ids);
    for (auto it=impl->pads.begin();it!=impl->pads.end();) {
        if (!it->second || !SDL_GamepadConnected(it->second)) {
            if (it->second) SDL_CloseGamepad(it->second);
            it=impl->pads.erase(it);
        } else ++it;
    }
    auto held=[&](SDL_GamepadButton b) { return buttons[b]; };
    if (combo && !impl->combo_held) Toggle();
    impl->combo_held=combo;
    CapturesInput.store(impl->open || combo,std::memory_order_release);
    int nav=0;
    if (!combo) {
        if (held(SDL_GAMEPAD_BUTTON_DPAD_UP)) nav=1;
        else if (held(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) nav=2;
        else if (held(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) nav=3;
        else if (held(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) nav=4;
        else if (held(SDL_GAMEPAD_BUTTON_SOUTH)) nav=5;
        else if (held(SDL_GAMEPAD_BUTTON_EAST)) nav=6;
    }
    const auto now=SDL_GetTicks();
    if (nav && (nav!=impl->last_nav || (nav<=4 && now>=impl->next_nav))) {
        impl->Navigate(nav); impl->next_nav=now+(nav!=impl->last_nav ? 380 : 120);
    }
    impl->last_nav=nav;
}
void Overlay::UpdateStatus(double fps,int w,int h,bool supported,const char* scaling) {
    impl->metalfx_supported=supported;
    [impl->upscaler itemAtIndex:1].enabled=supported;
    impl->status.stringValue=[NSString stringWithFormat:@"%.1f FPS  ·  Render %d × %d  ·  Output %d × %d\n%s",
        fps,impl->startup.render_width,impl->startup.render_height,w,h,scaling];
}
}
