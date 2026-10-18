// SPDX-License-Identifier: GPL-3.0-or-later
// Local fixture only: no public server, account, game entry point or window.
#include "gpu/metal/bbhost/network.h"
#include "core/config.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <cstdio>

extern "C" void runtime_restart() { std::abort(); }
extern "C" std::uintptr_t bbgpu_resolve(const char*);
namespace Hle { std::string Nid(std::string_view); }
void hle_register_http();
template<class T> T resolve(const char* name) {
    const auto p=bbgpu_resolve(Hle::Nid(name).c_str()); assert(p); return reinterpret_cast<T>(p);
}
struct Event { std::uint32_t events,detail; int id; void* user; };
int main(int argc,char** argv) {
    assert(argc==2); const std::string url=argv[1],body="{\"fixture\":true}";
    auto reply=MacNetwork::Request(url,&body,5000,true,{{"X-BBHost-Ruleset","vanilla"}});
    assert(reply.error.empty() && reply.status==200 && reply.body==body);
    assert(!MacNetwork::Request("file:///etc/hosts",nullptr,5000,true,{}).error.empty());
    config_set_online("127.0.0.1","http",true,false,"http://127.0.0.1");
    hle_register_http();
    const auto ctx=resolve<int(*)(int,int,std::uint64_t)>("sceHttpInit")(0,0,16384); assert(ctx>0);
    const auto tmpl=resolve<int(*)(int,const char*,int,int)>("sceHttpCreateTemplate")(ctx,"Mac local fixture",2,0); assert(tmpl>0);
    const auto conn=resolve<int(*)(int,const char*,int)>("sceHttpCreateConnectionWithURL")(tmpl,url.c_str(),0); assert(conn>0);
    const auto req=resolve<int(*)(int,int,const char*,std::uint64_t)>("sceHttpCreateRequestWithURL")(conn,1,url.c_str(),body.size()); assert(req>0);
    assert(resolve<int(*)(int,int)>("sceHttpSetNonblock")(req,1)==0);
    void* ep=nullptr;
    assert(resolve<int(*)(int,void**)>("sceHttpCreateEpoll")(ctx,&ep)==0 && ep);
    void* user=reinterpret_cast<void*>(0x1234);
    assert(resolve<int(*)(int,void*,void*)>("sceHttpSetEpoll")(req,ep,user)==0);
    assert(resolve<int(*)(int,const void*,std::uint64_t)>("sceHttpSendRequest")(req,body.data(),body.size())==0);
    Event event{};
    assert(resolve<int(*)(void*,Event*,int,int)>("sceHttpWaitRequest")(ep,&event,1,5000000)==1);
    assert(event.id==req && event.user==user && event.detail==0);
    int status=0; assert(resolve<int(*)(int,int*)>("sceHttpGetStatusCode")(req,&status)==0 && status==200);
    std::uint64_t length=0;
    assert(resolve<int(*)(int,std::uint64_t*)>("sceHttpGetResponseContentLength")(req,&length)==0 && length==body.size());
    char response[128]{};
    assert(resolve<int(*)(int,void*,std::uint64_t)>("sceHttpReadData")(req,response,sizeof(response))==int(body.size()));
    assert(std::string(response,body.size())==body);
    assert(resolve<int(*)(int)>("sceHttpDeleteRequest")(req)==0);
    assert(resolve<int(*)(int)>("sceHttpDeleteConnection")(conn)==0);
    assert(resolve<int(*)(int)>("sceHttpDeleteTemplate")(tmpl)==0);
    assert(resolve<int(*)(int,void*)>("sceHttpDestroyEpoll")(ctx,ep)==0);
    assert(resolve<int(*)()>("sceHttpTerm")()==0);
    std::puts("Mac HTTP: curl transport, HLE resolution, nonblocking events and response reads passed");
}
