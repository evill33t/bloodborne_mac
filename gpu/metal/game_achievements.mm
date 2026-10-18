// SPDX-License-Identifier: GPL-2.0-or-later
// Catalog/progress behavior adapted from pyL1nx/bloodborne_pc 64919f7.
// Native Cocoa browser and game-view banners; artwork is read from the user's TRP.
#include "game_achievements.h"
#include "src/runtime_trophy.h"
#include <SDL3/SDL.h>
#import <AppKit/AppKit.h>
#include <algorithm>
#include <array>
#include <deque>
#include <fstream>
#include <cstring>
#include <cstdio>
#include <vector>

namespace {
NSString* Text(const char* value) { return [NSString stringWithUTF8String:value]; }
NSString* Grade(int grade) { return @[@"",@"Platinum",@"Gold",@"Silver",@"Bronze"][std::clamp(grade,0,4)]; }
NSColor* GradeColor(int grade) {
    switch (grade) {
    case 1: return [NSColor colorWithRed:.76 green:.84 blue:.91 alpha:1];
    case 2: return [NSColor colorWithRed:.88 green:.71 blue:.36 alpha:1];
    case 3: return [NSColor colorWithRed:.72 green:.76 blue:.80 alpha:1];
    default: return [NSColor colorWithRed:.77 green:.49 blue:.29 alpha:1];
    }
}
void Cup(NSRect box,NSColor* color) {
    [color set];
    auto point=[&](CGFloat x,CGFloat y) { return NSMakePoint(box.origin.x+x*box.size.width,box.origin.y+y*box.size.height); };
    auto bowl=[NSBezierPath bezierPath];
    [bowl moveToPoint:point(.28,.15)]; [bowl lineToPoint:point(.72,.15)];
    [bowl curveToPoint:point(.50,.64) controlPoint1:point(.72,.52) controlPoint2:point(.66,.60)];
    [bowl curveToPoint:point(.28,.15) controlPoint1:point(.34,.60) controlPoint2:point(.28,.52)]; [bowl fill];
    auto handles=[NSBezierPath bezierPath]; handles.lineWidth=std::max(CGFloat(1),box.size.width*.07);
    [handles moveToPoint:point(.29,.24)]; [handles curveToPoint:point(.34,.51) controlPoint1:point(.02,.12) controlPoint2:point(.08,.51)];
    [handles moveToPoint:point(.71,.24)]; [handles curveToPoint:point(.66,.51) controlPoint1:point(.98,.12) controlPoint2:point(.92,.51)]; [handles stroke];
    NSRectFill(NSMakeRect(point(.46,.60).x,point(.46,.60).y,box.size.width*.08,box.size.height*.19));
    NSRectFill(NSMakeRect(point(.30,.79).x,point(.30,.79).y,box.size.width*.4,box.size.height*.09));
}
NSTextField* Label(NSString* value,CGFloat size,bool bold=false) {
    auto label=[NSTextField wrappingLabelWithString:value];
    label.font=bold ? [NSFont boldSystemFontOfSize:size] : [NSFont systemFontOfSize:size];
    label.textColor=NSColor.labelColor; return label;
}
NSStackView* Stack(NSArray<NSView*>* views,NSUserInterfaceLayoutOrientation orientation,CGFloat spacing) {
    auto stack=[NSStackView stackViewWithViews:views]; stack.orientation=orientation; stack.spacing=spacing;
    stack.alignment=orientation==NSUserInterfaceLayoutOrientationVertical ? NSLayoutAttributeLeading : NSLayoutAttributeCenterY;
    return stack;
}
std::uint64_t BE(const unsigned char* p,int bytes) { std::uint64_t value=0; while (bytes--) value=(value<<8)|*p++; return value; }
// The table is unencrypted; only bounded, unencrypted PNG entries are read.
// Missing/damaged archives use the grade cup, never block achievement tracking.
NSArray<NSImage*>* ReadArtwork(const std::filesystem::path& game) {
    NSMutableArray<NSImage*>* images=[NSMutableArray array];
    for (int id=0;id<BB_TROPHY_COUNT;++id) [images addObject:[NSImage new]];
    if (game.empty()) return images;
    std::ifstream file(game/"sce_sys/trophy/trophy00.trp",std::ios::binary|std::ios::ate);
    if (!file) return images;
    const auto length=file.tellg(); if (length<96 || length>64*1024*1024) return images;
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    file.seekg(0); if (!file.read(reinterpret_cast<char*>(bytes.data()),length)) return images;
    const auto entries=BE(bytes.data()+16,4),stride=BE(bytes.data()+20,4);
    if (BE(bytes.data(),4)!=0xdca24d00 || BE(bytes.data()+8,8)!=bytes.size() ||
        stride<64 || stride>4096 || entries>(bytes.size()-96)/stride) return images;
    for (std::uint64_t i=0;i<entries;++i) {
        const auto* entry=bytes.data()+96+i*stride;
        const std::string name(reinterpret_cast<const char*>(entry),strnlen(reinterpret_cast<const char*>(entry),32));
        const auto offset=BE(entry+32,8),size=BE(entry+40,8),flags=BE(entry+48,4);
        if (flags || size<8 || size>2*1024*1024 || offset>bytes.size() || size>bytes.size()-offset) continue;
        for (int id=0;id<BB_TROPHY_COUNT;++id) {
            char expected[32]; std::snprintf(expected,sizeof(expected),"TROP%03d.PNG",id);
            if (name!=expected || std::memcmp(bytes.data()+offset,"\211PNG\r\n\032\n",8)) continue;
            auto data=[NSData dataWithBytes:bytes.data()+offset length:size];
            if (auto image=[[NSImage alloc] initWithData:data]) images[id]=image;
        }
    }
    return images;
}
}

@interface BBTrophyActions : NSObject <NSWindowDelegate,NSSearchFieldDelegate>
@property(copy) void (^invoke)(id);
@property(copy) void (^focusChanged)(BOOL);
- (void)perform:(id)sender;
@end
@implementation BBTrophyActions
- (void)perform:(id)sender { if (self.invoke) self.invoke(sender); }
- (void)controlTextDidChange:(NSNotification*)notification { if (self.invoke) self.invoke(notification.object); }
- (void)windowDidBecomeKey:(NSNotification*)notification { if (self.focusChanged) self.focusChanged(YES); }
- (void)windowDidResignKey:(NSNotification*)notification { if (self.focusChanged) self.focusChanged(NO); }
- (void)windowWillClose:(NSNotification*)notification { if (self.focusChanged) self.focusChanged(NO); }
@end
@interface BBTrophyClipView : NSClipView
@end
@implementation BBTrophyClipView
- (BOOL)isFlipped { return YES; }
@end
@interface BBTrophyBackground : NSView
@end
@implementation BBTrophyBackground
- (void)drawRect:(NSRect)dirty { [[NSColor colorWithWhite:.10 alpha:1] set]; NSRectFill(self.bounds); }
@end
@interface BBTrophyArtView : NSView
@property(strong) NSImage* artwork;
@property NSInteger grade;
@property BOOL earned;
@end
@implementation BBTrophyArtView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirty {
    auto bounds=self.bounds;
    [[NSColor colorWithWhite:.04 alpha:1] set]; NSRectFill(bounds);
    if (self.artwork.size.width>0) [self.artwork drawInRect:NSInsetRect(bounds,1,1) fromRect:NSZeroRect
        operation:NSCompositingOperationSourceOver fraction:self.earned ? 1 : .35 respectFlipped:YES hints:nil];
    else Cup(NSInsetRect(bounds,9,9),self.earned ? GradeColor(int(self.grade)) : NSColor.tertiaryLabelColor);
    [[NSColor colorWithWhite:.65 alpha:self.earned ? .5 : .18] set];
    auto border=[NSBezierPath bezierPathWithRect:NSInsetRect(bounds,.5,.5)]; border.lineWidth=1; [border stroke];
}
@end
@interface BBTrophyBanner : NSView
@property(strong) NSImage* artwork;
@property NSInteger trophyID;
@end
@implementation BBTrophyBanner
- (BOOL)isFlipped { return YES; }
- (NSView*)hitTest:(NSPoint)point { return nil; }
- (void)drawRect:(NSRect)dirty {
    const auto* trophy=runtime_trophy_catalog(nullptr)+self.trophyID;
    const auto bounds=self.bounds;
    auto gradient=[[NSGradient alloc] initWithStartingColor:[NSColor colorWithWhite:0 alpha:.96]
        endingColor:[NSColor colorWithWhite:.025 alpha:.89]];
    [gradient drawInRect:bounds angle:0];
    [[NSColor colorWithWhite:.7 alpha:.7] set];
    [[NSBezierPath bezierPathWithRect:NSInsetRect(bounds,.5,.5)] stroke];
    const CGFloat scale=bounds.size.height/72;
    const auto rect=[&](CGFloat x,CGFloat y,CGFloat w,CGFloat h) { return NSMakeRect(x*scale,y*scale,w*scale,h*scale); };
    if (self.artwork.size.width>0) [self.artwork drawInRect:rect(12,12,48,48) fromRect:NSZeroRect
        operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
    else Cup(rect(16,13,44,44),GradeColor(trophy->grade));
    Cup(rect(73,16,16,16),GradeColor(trophy->grade));
    auto paragraph=[NSMutableParagraphStyle new]; paragraph.lineBreakMode=NSLineBreakByTruncatingTail;
    [Text(trophy->name) drawInRect:NSMakeRect(97*scale,14*scale,bounds.size.width-109*scale,23*scale)
        withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:16*scale weight:NSFontWeightRegular],
            NSForegroundColorAttributeName:NSColor.whiteColor,NSParagraphStyleAttributeName:paragraph}];
    [@"You have earned a trophy!" drawInRect:NSMakeRect(73*scale,39*scale,bounds.size.width-85*scale,20*scale)
        withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:12*scale],
            NSForegroundColorAttributeName:[NSColor colorWithWhite:.85 alpha:1],NSParagraphStyleAttributeName:paragraph}];
}
@end

namespace MacAchievements {
std::atomic<bool> CapturesInput{false};
struct Browser::Impl {
    NSView* game_view;
    NSWindow* window;
    BBTrophyActions* actions;
    NSMenuItem* menu_item;
    NSTextField *summary,*grades,*error;
    NSProgressIndicator* progress;
    NSSegmentedControl* filter;
    NSSearchField* search;
    NSStackView* list;
    NSScrollView* scroll;
    BBTrophyBanner* banner;
    NSArray<NSImage*>* artwork;
    RuntimeTrophySnapshot state{};
    std::uint64_t revision=UINT64_MAX,shown_at=0;
    std::deque<RuntimeTrophyEvent> pending;
    bool active=false;
    explicit Impl(SDL_Window* game,const std::filesystem::path& root) {
        auto native=(__bridge NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(game),SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,nullptr);
        game_view=native.contentView; artwork=ReadArtwork(root);
        actions=[BBTrophyActions new];
        actions.invoke=^(id sender) { if (sender==this->menu_item) Show(); else Rebuild(); };
        actions.focusChanged=^(BOOL key) { CapturesInput.store(key,std::memory_order_release); };
        window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,720,720)
            styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable
            backing:NSBackingStoreBuffered defer:NO];
        window.title=@"Achievements"; window.releasedWhenClosed=NO; window.delegate=actions;
        window.tabbingMode=NSWindowTabbingModeDisallowed;
        window.collectionBehavior=NSWindowCollectionBehaviorFullScreenAuxiliary;
        window.minSize=NSMakeSize(560,430); window.appearance=[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        window.contentView=[[BBTrophyBackground alloc] initWithFrame:window.contentView.bounds];
        [window center];
        menu_item=[[NSMenuItem alloc] initWithTitle:@"Achievements…" action:@selector(perform:) keyEquivalent:@"t"];
        menu_item.keyEquivalentModifierMask=NSEventModifierFlagCommand|NSEventModifierFlagShift;
        menu_item.target=actions;
        auto app_menu=NSApp.mainMenu.itemArray.firstObject.submenu;
        if (app_menu) [app_menu insertItem:menu_item atIndex:std::min(NSInteger(2),app_menu.numberOfItems)];
        auto title=Label(@"Achievements",27,true);
        summary=Label(@"",14,true); grades=Label(@"",12); grades.textColor=NSColor.secondaryLabelColor;
        progress=[NSProgressIndicator new]; progress.indeterminate=NO; progress.minValue=0; progress.maxValue=40;
        progress.style=NSProgressIndicatorStyleBar; progress.accessibilityLabel=@"Achievement progress";
        filter=[NSSegmentedControl segmentedControlWithLabels:@[@"All",@"Earned",@"Locked"] trackingMode:NSSegmentSwitchTrackingSelectOne
            target:actions action:@selector(perform:)]; filter.selectedSegment=0; filter.accessibilityLabel=@"Achievement filter";
        search=[NSSearchField new]; search.placeholderString=@"Search achievements"; search.delegate=actions;
        search.accessibilityLabel=@"Search achievements";
        auto filters=Stack(@[filter,search],NSUserInterfaceLayoutOrientationHorizontal,16);
        [search.widthAnchor constraintGreaterThanOrEqualToConstant:190].active=YES;
        scroll=[NSScrollView new]; scroll.drawsBackground=NO; scroll.hasVerticalScroller=YES; scroll.contentView=[BBTrophyClipView new];
        scroll.contentView.drawsBackground=NO;
        list=Stack(@[],NSUserInterfaceLayoutOrientationVertical,8); list.translatesAutoresizingMaskIntoConstraints=NO;
        scroll.documentView=list;
        [list.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor].active=YES;
        error=Label(@"",11); error.textColor=NSColor.systemOrangeColor; error.hidden=YES;
        auto content=Stack(@[title,summary,grades,progress,filters,scroll,error],NSUserInterfaceLayoutOrientationVertical,12);
        content.translatesAutoresizingMaskIntoConstraints=NO; [window.contentView addSubview:content];
        [NSLayoutConstraint activateConstraints:@[
            [content.leadingAnchor constraintEqualToAnchor:window.contentView.leadingAnchor constant:24],
            [content.trailingAnchor constraintEqualToAnchor:window.contentView.trailingAnchor constant:-24],
            [content.topAnchor constraintEqualToAnchor:window.contentView.topAnchor constant:24],
            [content.bottomAnchor constraintEqualToAnchor:window.contentView.bottomAnchor constant:-20],
            [progress.widthAnchor constraintEqualToAnchor:content.widthAnchor],
            [filters.widthAnchor constraintEqualToAnchor:content.widthAnchor],
            [scroll.widthAnchor constraintEqualToAnchor:content.widthAnchor],
            [scroll.heightAnchor constraintGreaterThanOrEqualToConstant:100]]];
        [scroll setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationVertical];
        banner=[BBTrophyBanner new]; banner.hidden=YES; banner.wantsLayer=YES;
        banner.accessibilityRole=NSAccessibilityStaticTextRole;
        [game_view addSubview:banner positioned:NSWindowAbove relativeTo:nil];
        Refresh();
    }
    NSView* Card(const RuntimeTrophy& t) {
        const bool earned=state.unlocked[t.id]!=0;
        auto icon=[BBTrophyArtView new]; icon.artwork=artwork[t.id]; icon.grade=t.grade; icon.earned=earned;
        icon.accessibilityElement=YES; icon.accessibilityLabel=[NSString stringWithFormat:@"%@ trophy, %@",Grade(t.grade),earned ? @"earned" : @"locked"];
        [icon.widthAnchor constraintEqualToConstant:52].active=YES; [icon.heightAnchor constraintEqualToConstant:52].active=YES;
        auto name=Label(Text(t.name),14,true); name.textColor=earned ? NSColor.labelColor : NSColor.secondaryLabelColor;
        auto description=Label(Text(t.description),12); description.textColor=NSColor.secondaryLabelColor;
        auto words=Stack(@[name,description],NSUserInterfaceLayoutOrientationVertical,4);
        [words setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
        [NSLayoutConstraint activateConstraints:@[[name.widthAnchor constraintEqualToAnchor:words.widthAnchor],
            [description.widthAnchor constraintEqualToAnchor:words.widthAnchor],
            [words.widthAnchor constraintGreaterThanOrEqualToConstant:100]]];
        auto grade=Label(Grade(t.grade),11,true); grade.textColor=GradeColor(t.grade); grade.alignment=NSTextAlignmentRight;
        NSString* date=@"Locked";
        if (earned) date=[NSDateFormatter localizedStringFromDate:[NSDate dateWithTimeIntervalSince1970:state.unlocked[t.id]]
            dateStyle:NSDateFormatterMediumStyle timeStyle:NSDateFormatterShortStyle];
        auto when=Label(date,10); when.textColor=NSColor.secondaryLabelColor; when.alignment=NSTextAlignmentRight;
        auto info=Stack(@[grade,when],NSUserInterfaceLayoutOrientationVertical,4); info.alignment=NSLayoutAttributeTrailing;
        [info.widthAnchor constraintEqualToConstant:120].active=YES;
        [grade.widthAnchor constraintEqualToAnchor:info.widthAnchor].active=YES;
        [when.widthAnchor constraintEqualToAnchor:info.widthAnchor].active=YES;
        auto card=[NSView new]; card.wantsLayer=YES; card.layer.cornerRadius=7;
        card.layer.backgroundColor=[NSColor colorWithWhite:.15 alpha:1].CGColor;
        card.layer.borderWidth=1; card.layer.borderColor=[NSColor colorWithWhite:.35 alpha:.4].CGColor;
        for (NSView* child in @[icon,words,info]) { child.translatesAutoresizingMaskIntoConstraints=NO; [card addSubview:child]; }
        [NSLayoutConstraint activateConstraints:@[
            [icon.leadingAnchor constraintEqualToAnchor:card.leadingAnchor constant:12],
            [icon.centerYAnchor constraintEqualToAnchor:card.centerYAnchor],
            [words.leadingAnchor constraintEqualToAnchor:icon.trailingAnchor constant:14],
            [words.trailingAnchor constraintEqualToAnchor:info.leadingAnchor constant:-14],
            [words.topAnchor constraintEqualToAnchor:card.topAnchor constant:12],
            [words.bottomAnchor constraintEqualToAnchor:card.bottomAnchor constant:-12],
            [info.trailingAnchor constraintEqualToAnchor:card.trailingAnchor constant:-12],
            [info.centerYAnchor constraintEqualToAnchor:card.centerYAnchor],
            [card.heightAnchor constraintGreaterThanOrEqualToConstant:78]]];
        return card;
    }
    void Refresh() {
        runtime_trophy_snapshot(&state);
        if (revision==state.revision) return;
        revision=state.revision; Rebuild();
    }
    void Rebuild() {
        const auto* catalog=runtime_trophy_catalog(nullptr);
        int earned=0,counts[5]{};
        for (int id=0;id<BB_TROPHY_COUNT;++id) if (state.unlocked[id]) { ++earned; ++counts[catalog[id].grade]; }
        summary.stringValue=[NSString stringWithFormat:@"%d of 40 earned  ·  %d%%",earned,earned*100/40]; progress.doubleValue=earned;
        grades.stringValue=[NSString stringWithFormat:@"Platinum %d   ·   Gold %d   ·   Silver %d   ·   Bronze %d",counts[1],counts[2],counts[3],counts[4]];
        error.stringValue=Text(state.error); error.hidden=!state.error[0];
        for (NSView* row in [list.arrangedSubviews copy]) { [list removeArrangedSubview:row]; [row removeFromSuperview]; }
        int shown=0;
        for (int group=0;group<=1;++group) {
            NSMutableArray<NSView*>* cards=[NSMutableArray array]; int group_earned=0;
            for (int id=0;id<BB_TROPHY_COUNT;++id) {
                const auto& t=catalog[id]; if (t.group!=group) continue;
                const bool unlocked=state.unlocked[id]!=0; group_earned+=unlocked;
                if ((filter.selectedSegment==1 && !unlocked) || (filter.selectedSegment==2 && unlocked)) continue;
                if (search.stringValue.length && ![Text(t.name) localizedCaseInsensitiveContainsString:search.stringValue] &&
                    ![Text(t.description) localizedCaseInsensitiveContainsString:search.stringValue]) continue;
                [cards addObject:Card(t)];
            }
            if (!cards.count) continue;
            auto heading=Label([NSString stringWithFormat:@"%@   %d / %d",group ? @"The Old Hunters" : @"Bloodborne",group_earned,group ? 6 : 34],15,true);
            [list addArrangedSubview:heading];
            for (NSView* card in cards) { [list addArrangedSubview:card]; [card.widthAnchor constraintEqualToAnchor:list.widthAnchor constant:-2].active=YES; ++shown; }
        }
        if (!shown) [list addArrangedSubview:Label(search.stringValue.length ? @"No matching achievements." :
            filter.selectedSegment==1 ? @"No achievements earned yet. Your next hunt awaits." : @"Every achievement is earned. Hats off!",13)];
        [list layoutSubtreeIfNeeded];
    }
    void Show() { Refresh(); [window makeKeyAndOrderFront:nil]; [window makeFirstResponder:search]; }
    void Tick(std::uint64_t now) {
        Refresh();
        RuntimeTrophyEvent event; while (runtime_trophy_take_event(&event)) pending.push_back(event);
        if (active && now-shown_at>=6500) { active=false; banner.hidden=YES; }
        if (!active && !pending.empty()) {
            const auto event=pending.front(); pending.pop_front(); shown_at=now; active=true;
            banner.trophyID=event.id; banner.artwork=artwork[event.id]; banner.needsDisplay=YES;
            const auto* t=runtime_trophy_catalog(nullptr)+event.id;
            banner.accessibilityLabel=[NSString stringWithFormat:@"%@. You have earned a %@ trophy!",Text(t->name),Grade(t->grade)];
            if (game_view.window.isVisible) NSAccessibilityPostNotificationWithUserInfo(banner,NSAccessibilityAnnouncementRequestedNotification,
                @{NSAccessibilityAnnouncementKey:banner.accessibilityLabel,NSAccessibilityPriorityKey:@(NSAccessibilityPriorityMedium)});
        }
        if (active) {
            const auto bounds=game_view.bounds; const CGFloat scale=std::clamp(bounds.size.height/720,CGFloat(.75),CGFloat(1.4));
            const CGFloat width=std::min(CGFloat(400)*scale,std::max(CGFloat(0),bounds.size.width-24));
            const CGFloat height=72*scale,top=std::max(CGFloat(20),bounds.size.height*.125);
            banner.frame=NSMakeRect(12,game_view.isFlipped ? top : bounds.size.height-top-height,width,height);
            const double elapsed=double(now-shown_at);
            const bool reduced=NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
            banner.alphaValue=reduced ? 1 : std::clamp(std::min(elapsed/200,(6500-elapsed)/350),0.0,1.0);
            banner.hidden=NO;
        }
    }
};
Browser::Browser(SDL_Window* game,const std::filesystem::path& root):impl(std::make_unique<Impl>(game,root)) {}
Browser::~Browser() {
    impl->window.delegate=nil; [impl->window close];
    [impl->menu_item.menu removeItem:impl->menu_item]; [impl->banner removeFromSuperview];
    impl->actions.invoke=nil; impl->actions.focusChanged=nil; CapturesInput=false;
}
void Browser::Show() { impl->Show(); }
bool Browser::HandleEvent(const SDL_Event& event) {
    if (event.type==SDL_EVENT_KEY_DOWN && event.key.key==SDLK_F2 && !event.key.repeat) { Show(); return true; }
    return false;
}
void Browser::Tick(std::uint64_t milliseconds) { impl->Tick(milliseconds); }
}
