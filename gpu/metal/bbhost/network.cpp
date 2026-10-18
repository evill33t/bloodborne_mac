// SPDX-License-Identifier: GPL-3.0-or-later
#include "network.h"
#include "gpu/metal/game_settings.h"
#include "gpu/bbnet/thd/thd.h"
#include "core/libraries/libs.h"
#include "core/config.h"
#include "core/portable.h"
#include "host/plugins.h"
#include "net/account.h"
#include <curl/curl.h>
#include <mutex>
#include <cstdlib>
#include <algorithm>

extern "C" void bbmac_register_nid(const char*,void*);
void hle_register_http();
namespace Core::Loader {
void SymbolsResolver::AddSymbol(const char* nid,const char*,const char*,SymbolType,u64 address) {
    bbmac_register_nid(nid,reinterpret_cast<void*>(address));
}
}
namespace BbEngine {
bool host_read_safe(const void* p,void* out,std::size_t n) { return ::host_read_safe(p,out,n); }
}
namespace MacNetwork {
std::string AccountName() { return net::account_name(); }
std::string AccountToken() { return net::account_token(); }
Reply Request(const std::string& url,const std::string* post,int timeout_ms,bool verify,
    const std::vector<std::pair<std::string,std::string>>& headers) {
    static std::once_flag init;
    std::call_once(init,[] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    Reply r;
    if (!url.starts_with("https://") && !url.starts_with("http://")) { r.error="Server must use HTTP or HTTPS"; return r; }
    auto c=curl_easy_init(); if (!c) { r.error="Could not initialize HTTP"; return r; }
    curl_slist* list=nullptr;
    for (const auto& [k,v]:headers) {
        if (k.find_first_of("\r\n")!=std::string::npos || v.find_first_of("\r\n")!=std::string::npos) continue;
        list=curl_slist_append(list,(k+": "+v).c_str());
    }
    char error[CURL_ERROR_SIZE]{};
    curl_easy_setopt(c,CURLOPT_URL,url.c_str());
    curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"http,https");
    curl_easy_setopt(c,CURLOPT_HTTPHEADER,list);
    curl_easy_setopt(c,CURLOPT_ERRORBUFFER,error);
    curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(c,CURLOPT_TIMEOUT_MS,long(std::clamp(timeout_ms,1,60000)));
    curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,verify ? 1L : 0L);
    curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,verify ? 2L : 0L);
    curl_easy_setopt(c,CURLOPT_WRITEDATA,&r.body);
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,+[](char* p,std::size_t a,std::size_t b,void* data)->std::size_t {
        const auto n=a*b; auto& body=*static_cast<std::string*>(data);
        if (n>16*1024*1024 || body.size()>16*1024*1024-n) return 0;
        body.append(p,n); return n;
    });
    if (post) { curl_easy_setopt(c,CURLOPT_POST,1L); curl_easy_setopt(c,CURLOPT_POSTFIELDS,post->data());
        curl_easy_setopt(c,CURLOPT_POSTFIELDSIZE_LARGE,curl_off_t(post->size())); }
    const auto result=curl_easy_perform(c);
    if (result!=CURLE_OK) r.error=*error ? error : curl_easy_strerror(result);
    else { long status=0; curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&status); r.status=int(status); }
    curl_slist_free_all(list); curl_easy_cleanup(c); return r;
}
std::string Ruleset() { return plugins_ruleset(); }
void Configure(const MacSettings::Values& values) {
    const auto automatic=[](const std::string& key,const std::string& value) {
        const auto marker="BB_MAC_AUTO_"+key;
        const char *current=std::getenv(key.c_str()),*previous=std::getenv(marker.c_str());
        if (!current || (previous && std::string(current)==previous)) {
            setenv(key.c_str(),value.c_str(),1); setenv(marker.c_str(),value.c_str(),1);
        }
    };
    auto server=values.server_url; while (server.ends_with('/')) server.pop_back();
    const auto split=server.find("://");
    automatic("BB_ONLINE",values.online ? "1" : "0");
    automatic("BB_THD_GAME_HOST",server.substr(split+3));
    automatic("BB_THD_SCHEME",server.substr(0,split));
    automatic("BB_THD_AUTH",server);
    automatic("BB_THD_SERVER",values.matchmaking_url);
    automatic("BB_CLIENT_VERSION","Bloodborne-Mac/bbhost-566184a");
    setenv("BB_NET_BACKEND","thd",1);
    const auto value=[](const char* key,const char* fallback) { auto p=std::getenv(key); return p ? p : fallback; };
    config_set_online(value("BB_THD_GAME_HOST","thehuntersdream.com"),value("BB_THD_SCHEME","https"),true,true,
                      value("BB_THD_AUTH","https://thehuntersdream.com"));
    LoadAccount(value("BB_THD_AUTH","https://thehuntersdream.com"));
}
void Register() { if (BbThd::Enabled()) { Core::Loader::SymbolsResolver resolver; BbThd::Register(&resolver); hle_register_http(); } }
void Initialize() { if (BbThd::Enabled()) BbThd::Initialize(); }
void SetImage(unsigned char* image,std::uint64_t size) { if (BbThd::Enabled()) BbThd::SetImage(image,size); }
}
