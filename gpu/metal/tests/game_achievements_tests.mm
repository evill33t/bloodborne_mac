// SPDX-License-Identifier: GPL-2.0-or-later
// Hidden Cocoa windows and an isolated save folder; never launches the game.
#import <AppKit/AppKit.h>
#include <SDL3/SDL.h>
#include "gpu/metal/game_achievements.h"
#include "src/runtime_trophy.h"
#include "gpu/bbgpu.h"
#include <cassert>
#include <cstdio>
#include <unistd.h>
#include <vector>
extern "C" void runtime_restart() { std::abort(); }
extern "C" std::uintptr_t runtime_services_resolve(const char*);
static NSView* Find(NSView* view,NSString* label) {
    if ([view.accessibilityLabel isEqualToString:label] ||
        ([view isKindOfClass:NSTextField.class] && [((NSTextField*)view).stringValue isEqualToString:label])) return view;
    for (NSView* child in view.subviews) if (auto found=Find(child,label)) return found;
    return nil;
}
static NSView* Banner(NSView* view) {
    for (NSView* child in view.subviews) if ([NSStringFromClass(child.class) isEqualToString:@"BBTrophyBanner"]) return child;
    return nil;
}
static void Render(NSView* view,NSString* path) {
    [view layoutSubtreeIfNeeded];
    [view.effectiveAppearance performAsCurrentDrawingAppearance:^{
        [view displayIfNeeded];
        auto bitmap=[view bitmapImageRepForCachingDisplayInRect:view.bounds]; assert(bitmap);
        [view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];
        auto png=[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}]; assert(png);
        assert([png writeToFile:path atomically:YES]);
    }];
}
int main(int argc,char** argv) {
    @autoreleasepool {
        if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr,"Skipped Cocoa fixture: %s\n",SDL_GetError()); return 77; }
        auto game=SDL_CreateWindow("Achievement fixture",1000,720,SDL_WINDOW_HIDDEN|SDL_WINDOW_RESIZABLE);
        if (!game) { SDL_Quit(); return 77; }
        auto native=(__bridge NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(game),SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,nullptr); assert(native);
        char folder[]="/tmp/bb-achievement-ui-XXXXXX"; assert(mkdtemp(folder)); runtime_trophy_configure(folder);
        {
            MacAchievements::Browser achievements(game,argc>1 ? argv[1] : "");
            NSWindow* browser=nil;
            for (NSWindow* window in NSApp.windows) if ([window.title isEqualToString:@"Achievements"]) browser=window;
            assert(browser && !browser.visible && !bbgpu_overlay_captures_input());
            assert(Find(browser.contentView,@"0 of 40 earned  ·  0%"));
            auto filter=(NSSegmentedControl*)Find(browser.contentView,@"Achievement filter"); assert(filter);
            auto search=(NSSearchField*)Find(browser.contentView,@"Search achievements"); assert(search);
            filter.selectedSegment=1; [filter sendAction:filter.action to:filter.target];
            assert(Find(browser.contentView,@"No achievements earned yet. Your next hunt awaits."));
            int32_t platinum;
            const auto unlock=reinterpret_cast<int32_t(*)(int32_t,int32_t,int32_t,int32_t*)>(runtime_services_resolve("28xmRUFao68#l#m")); assert(unlock);
            assert(!unlock(1,1,30,&platinum) && !unlock(1,1,21,&platinum));
            achievements.Tick(1000); achievements.Tick(1300);
            assert(Find(browser.contentView,@"2 of 40 earned  ·  5%"));
            for (NSString* title in @[@"Cleric Beast",@"Rune Contact"]) {
                auto card=Find(browser.contentView,title).superview.superview;
                for (NSView* child in card.subviews) if ([NSStringFromClass(child.class) isEqualToString:@"BBTrophyArtView"]) {
                    assert(child.bounds.size.width==52 && child.bounds.size.height==52);
                    if (argc>1) assert([((NSImage*)[child valueForKey:@"artwork"]) isValid]);
                    child.needsDisplay=YES;
                }
            }
            auto banner=Banner(native.contentView); assert(banner && !banner.hidden && banner.alphaValue==1);
            assert([banner.accessibilityLabel containsString:@"Rune Contact"]);
            assert(![banner hitTest:NSMakePoint(15,15)] && !bbgpu_overlay_captures_input());
            assert(banner.frame.origin.x>=0 && NSMaxX(banner.frame)<=native.contentView.bounds.size.width);
            if (argc>1) assert([((NSImage*)[banner valueForKey:@"artwork"]) size].width>0);
            if (const char* output=std::getenv("BB_TROPHY_PREVIEW_DIR")) {
                std::filesystem::create_directories(output);
                Render(banner,[[NSString stringWithUTF8String:output] stringByAppendingPathComponent:@"achievement-banner.png"]);
                Render(browser.contentView,[[NSString stringWithUTF8String:output] stringByAppendingPathComponent:@"achievement-window.png"]);
            }
            search.stringValue=@"Rune";
            [(id<NSTextFieldDelegate>)search.delegate controlTextDidChange:[NSNotification notificationWithName:NSControlTextDidChangeNotification object:search]];
            assert(Find(browser.contentView,@"Rune Contact") && !Find(browser.contentView,@"Cleric Beast"));
            search.stringValue=@"does not match";
            [(id<NSTextFieldDelegate>)search.delegate controlTextDidChange:[NSNotification notificationWithName:NSControlTextDidChangeNotification object:search]];
            assert(Find(browser.contentView,@"No matching achievements."));
            achievements.Tick(8000); achievements.Tick(8300);
            assert(!banner.hidden && [banner.accessibilityLabel containsString:@"Cleric Beast"]);
            achievements.Tick(15000); assert(banner.hidden);
            assert(runtime_trophy_unlock(30,&platinum)==int32_t(0x8055160c));
            achievements.Tick(16000); assert(banner.hidden);
            runtime_trophy_configure(folder); achievements.Tick(17000); assert(banner.hidden);
            SDL_SetWindowSize(game,640,480); SDL_PumpEvents();
            assert(!runtime_trophy_unlock(22,&platinum)); achievements.Tick(20000); achievements.Tick(20300);
            assert(NSMaxX(banner.frame)<=native.contentView.bounds.size.width && NSMinY(banner.frame)>=0);
            auto delegate=browser.delegate;
            [delegate windowDidBecomeKey:[NSNotification notificationWithName:NSWindowDidBecomeKeyNotification object:browser]];
            assert(bbgpu_overlay_captures_input());
            [delegate windowDidResignKey:[NSNotification notificationWithName:NSWindowDidResignKeyNotification object:browser]];
            assert(!bbgpu_overlay_captures_input());
        }
        std::filesystem::remove_all(folder); SDL_DestroyWindow(game); SDL_Quit();
        std::puts("Cocoa achievements: browser, search/filter, queued banners, artwork, resize and input passed");
    }
}
