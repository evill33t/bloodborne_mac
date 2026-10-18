// SPDX-License-Identifier: GPL-3.0-or-later
#include "network.h"
#include "net/account.h"
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>

namespace MacNetwork {
namespace {
std::mutex mutex;
std::string server,status,url;
std::atomic<bool> busy{false},cancel{false};
NSString* String(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }
NSDictionary* Query() {
    return @{(__bridge id)kSecClass:(__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService:String("bloodborne_mac:"+server),(__bridge id)kSecAttrAccount:@"device-link"};
}
bool SaveAccount() {
    auto payload=[NSJSONSerialization dataWithJSONObject:@{@"name":String(net::account_name()),@"token":String(net::account_token())} options:0 error:nil];
    auto query=[Query() mutableCopy]; auto attributes=@{(__bridge id)kSecValueData:payload};
    auto result=SecItemUpdate((__bridge CFDictionaryRef)query,(__bridge CFDictionaryRef)attributes);
    if (result==errSecItemNotFound) {
        [query addEntriesFromDictionary:attributes];
        query[(__bridge id)kSecAttrAccessible]=(__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly;
        result=SecItemAdd((__bridge CFDictionaryRef)query,nullptr);
    }
    return result==errSecSuccess;
}
void Say(std::string text) { std::scoped_lock lock(mutex); status=std::move(text); }
}
void LoadAccount(const std::string& origin) {
    server=origin;
    const auto name=std::getenv("BB_THD_ONLINE_ID"),token=std::getenv("BB_THD_TOKEN");
    if (name && token && *token) { net::account_set(name,token); return; }
    auto query=[Query() mutableCopy]; query[(__bridge id)kSecReturnData]=@YES;
    query[(__bridge id)kSecUseAuthenticationUI]=(__bridge id)kSecUseAuthenticationUIFail;
    CFTypeRef result=nullptr;
    if (SecItemCopyMatching((__bridge CFDictionaryRef)query,&result)!=errSecSuccess) return;
    auto payload=CFBridgingRelease(result);
    auto object=[NSJSONSerialization JSONObjectWithData:payload options:0 error:nil];
    if ([object isKindOfClass:NSDictionary.class] && [object[@"name"] isKindOfClass:NSString.class] && [object[@"token"] isKindOfClass:NSString.class])
        net::account_set([object[@"name"] UTF8String],[object[@"token"] UTF8String]);
}
void LinkAccount() {
    if (busy.exchange(true)) return;
    cancel=false; Say("Requesting a sign-in code…");
    std::thread([] { @autoreleasepool {
        net::DeviceLink link; std::string error;
        if (!net::account_link_start(link,error)) { Say(error); busy=false; return; }
        auto target=[NSURLComponents componentsWithString:String(link.verification_uri)];
        auto origin=[NSURLComponents componentsWithString:String(server)];
        if (![target.scheme isEqualToString:origin.scheme] || ![target.host isEqualToString:origin.host] ||
            !((!target.port && !origin.port) || [target.port isEqual:origin.port]) || target.user || target.password) {
            Say("The server returned a sign-in address outside its own website."); busy=false; return;
        }
        { std::scoped_lock lock(mutex); url=link.verification_uri; status="Approve code "+link.user_code+" on the server website."; }
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(std::clamp(link.expires_in,30,1200));
        auto interval=std::clamp(link.interval,1,30);
        while (!cancel && std::chrono::steady_clock::now()<end) {
            for (int n=0;n<interval*10 && !cancel;++n) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (cancel) break;
            const auto reply=net::account_link_poll(link,error);
            if (reply=="approved") {
                if (cancel) { net::account_clear(); break; }
                const bool saved=SaveAccount();
                Say("Signed in as "+net::account_name()+(saved ? "." : ". This sign-in could not be saved in Keychain."));
                busy=false; return;
            }
            if (reply=="slow_down") { interval=std::min(interval+5,30); continue; }
            if (reply!="pending") { Say(error.empty() ? "Sign-in "+reply+"." : error); busy=false; return; }
        }
        Say(cancel ? "Sign-in cancelled." : "The sign-in code expired."); busy=false;
    }}).detach();
}
void SignOut() {
    cancel=true;
    // The token is removed locally at once; the server's logout is a separate user action.
    net::account_clear(); SecItemDelete((__bridge CFDictionaryRef)Query()); Say("Signed out on this Mac.");
}
bool AccountBusy() { return busy.load(); }
std::string AccountStatus() {
    std::scoped_lock lock(mutex);
    if (!status.empty()) return status;
    return net::account_logged_in() ? "Signed in as "+net::account_name()+"." : "Sign in to play online.";
}
std::string TakeLinkUrl() { std::scoped_lock lock(mutex); return std::exchange(url,{}); }
}
