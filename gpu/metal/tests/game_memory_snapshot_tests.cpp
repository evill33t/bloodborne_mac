// SPDX-License-Identifier: GPL-2.0-or-later
#include "src/runtime_gpu_memory.h"
extern "C" {
#include "src/runtime.h"
}
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

extern "C" void runtime_restart() {std::abort();}
namespace {
constexpr std::size_t Page=16384;
bool Uniform(const std::vector<unsigned char>& bytes,unsigned char value) {
    return std::all_of(bytes.begin(),bytes.end(),[&](auto b){return b==value;});
}
struct MutationProbe {
    std::uintptr_t source=0;
    int (*protect)(void*,std::uint64_t,int)=nullptr;
    unsigned begins=0,ends=0;
    bool permit=true,nested=false,block=false,release=false;
    std::mutex mutex;
    std::condition_variable changed;
    std::promise<void> entered;
    static int Begin(void* context) {
        auto& p=*static_cast<MutationProbe*>(context);++p.begins;
        unsigned char value=0;
        // This read takes the actual VMA lock: begin must run before write_lock.
        assert(runtime_gpu_memory_snapshot(p.source,&value,1));
        assert(!runtime_gpu_memory_set_mutation_hooks(nullptr,nullptr,nullptr));
        if(p.block) {
            p.entered.set_value();std::unique_lock lock(p.mutex);
            p.changed.wait(lock,[&]{return p.release;});
        }
        if(!p.permit)return 0;
        if(p.nested)assert(!p.protect(reinterpret_cast<void*>(p.source),Page,3));
        return 1;
    }
    static void End(void* context) {
        auto& p=*static_cast<MutationProbe*>(context);++p.ends;
        unsigned char value=0;assert(runtime_gpu_memory_snapshot(p.source,&value,1));
    }
};
}
int main() {
    std::uint64_t physical=0;
    assert(!runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),8*Page,Page,0,&physical));
    std::uintptr_t address=0;
    assert(!runtime_gpu_memory_map(&address,3*Page,3,0,physical,Page));
    auto* backing=static_cast<unsigned char*>(runtime_gpu_memory_backing());assert(backing);
    for(std::size_t i=0;i<8*Page;++i)backing[physical+i]=static_cast<unsigned char>((i*37+(i>>8))%251);
    std::vector<unsigned char> result(3*Page,0);
    assert(runtime_gpu_memory_snapshot(address,result.data(),result.size()));
    assert(!std::memcmp(result.data(),backing+physical,result.size()));
    const auto immutable=result;
    backing[physical]^=0xff;assert(result==immutable);backing[physical]^=0xff;
    std::vector<unsigned char> unaligned(Page+71,0);
    assert(runtime_gpu_memory_snapshot(address+13,unaligned.data(),unaligned.size()));
    assert(!std::memcmp(unaligned.data(),backing+physical+13,unaligned.size()));

    auto unmap=reinterpret_cast<int(*)(void*,std::uint64_t)>(runtime_memory_resolve("cQke9UuBQOk#p#J"));
    auto protect=reinterpret_cast<int(*)(void*,std::uint64_t,int)>(runtime_memory_resolve("vSMAm3cxYTY#p#J"));
    assert(unmap && protect);
    assert(!unmap(reinterpret_cast<void*>(address+Page),Page));
    std::fill(result.begin(),result.end(),0xc3);
    assert(!runtime_gpu_memory_snapshot(address,result.data(),result.size()) && Uniform(result,0xc3));
    auto middle=address+Page;
    assert(!runtime_gpu_memory_map(&middle,Page,3,0x10,physical+4*Page,Page));
    assert(middle==address+Page);
    assert(runtime_gpu_memory_snapshot(address,result.data(),result.size()));
    assert(!std::memcmp(result.data(),backing+physical,Page));
    assert(!std::memcmp(result.data()+Page,backing+physical+4*Page,Page));
    assert(!std::memcmp(result.data()+2*Page,backing+physical+2*Page,Page));
    for(int permission:{1,2,4,0x10,0x20}) {
        assert(!protect(reinterpret_cast<void*>(middle),Page,permission));
        assert(runtime_gpu_memory_snapshot(address,result.data(),result.size()));
        assert(!std::memcmp(result.data()+Page,backing+physical+4*Page,Page));
    }
    assert(!protect(reinterpret_cast<void*>(middle),Page,0));
    std::fill(result.begin(),result.end(),0x91);
    assert(!runtime_gpu_memory_snapshot(address,result.data(),result.size()) && Uniform(result,0x91));
    assert(!protect(reinterpret_cast<void*>(middle),Page,3));

    std::uintptr_t alias=0;
    assert(!runtime_gpu_memory_map(&alias,Page,1,0,physical+4*Page,Page));
    std::vector<unsigned char> alias_copy(Page,0);
    assert(runtime_gpu_memory_snapshot(alias,alias_copy.data(),Page));
    assert(!std::memcmp(alias_copy.data(),backing+physical+4*Page,Page));
    const auto source_before=backing[physical];
    assert(!runtime_gpu_memory_snapshot(address,backing+physical+1,Page));
    assert(!runtime_gpu_memory_snapshot(address,reinterpret_cast<void*>(alias),Page));
    assert(backing[physical]==source_before);
    assert(!unmap(reinterpret_cast<void*>(alias),Page));

    std::fill(result.begin(),result.end(),0x57);
    assert(!runtime_gpu_memory_snapshot(0,result.data(),result.size()));
    assert(!runtime_gpu_memory_snapshot(address,nullptr,result.size()));
    assert(!runtime_gpu_memory_snapshot(address,result.data(),0));
    assert(!runtime_gpu_memory_snapshot(UINTPTR_MAX-7,result.data(),16));
    assert(!runtime_gpu_memory_snapshot(address,reinterpret_cast<void*>(UINTPTR_MAX-7),16));
    assert(!runtime_gpu_memory_snapshot(alias,result.data(),1));
    assert(Uniform(result,0x57));
    std::vector<unsigned char> host_source(256,0x31);
    assert(!runtime_gpu_memory_snapshot(reinterpret_cast<std::uintptr_t>(host_source.data()),result.data(),host_source.size()));
    assert(Uniform(result,0x57)); // Host/loader spans must use checked Mach fallback.

    auto flexible_map=reinterpret_cast<int(*)(void**,std::uint64_t,int,int)>(runtime_memory_resolve("IWIBBdTHit4#p#J"));
    assert(flexible_map);void* flexible=nullptr;assert(!flexible_map(&flexible,Page,3,0));
    std::memset(flexible,0x8e,Page);
    assert(runtime_gpu_memory_snapshot(reinterpret_cast<std::uintptr_t>(flexible),alias_copy.data(),Page));
    assert(Uniform(alias_copy,0x8e));
    assert(!unmap(flexible,Page));
    assert(!runtime_gpu_memory_snapshot(reinterpret_cast<std::uintptr_t>(flexible),alias_copy.data(),Page));
    assert(Uniform(alias_copy,0x8e));

    // Replacing a VMA must never mix physical versions within one snapshot.
    std::memset(backing+physical+6*Page,0x36,Page);
    std::memset(backing+physical+7*Page,0xa7,Page);
    std::uintptr_t changing=0;
    assert(!runtime_gpu_memory_map(&changing,Page,3,0,physical+6*Page,Page));
    std::atomic<bool> start=false;
    std::thread writer([&] {
        while(!start.load(std::memory_order_acquire))std::this_thread::yield();
        for(unsigned i=0;i<2000;++i) {
            auto target=changing;
            assert(!runtime_gpu_memory_map(&target,Page,3,0x10,physical+(6+(i&1))*Page,Page));
            assert(target==changing);
        }
    });
    start.store(true,std::memory_order_release);
    for(unsigned i=0;i<2000;++i) {
        assert(runtime_gpu_memory_snapshot(changing,alias_copy.data(),Page));
        assert(Uniform(alias_copy,0x36) || Uniform(alias_copy,0xa7));
    }
    writer.join();
    puts("Mapped snapshots: exact bytes, physical discontinuities, permissions, aliases, rejection and concurrent remapping passed");

    MutationProbe probe;probe.source=changing;probe.protect=protect;probe.nested=true;
    assert(!runtime_gpu_memory_set_mutation_hooks(&probe,MutationProbe::Begin,nullptr));
    assert(runtime_gpu_memory_set_mutation_hooks(&probe,MutationProbe::Begin,MutationProbe::End));
    assert(!protect(reinterpret_cast<void*>(changing),Page,1));
    assert(probe.begins==1 && probe.ends==1); // The nested protect shares one lease.
    probe.nested=false;probe.permit=false;
    assert(protect(reinterpret_cast<void*>(changing),Page,0));
    assert(unmap(reinterpret_cast<void*>(changing),Page));
    auto replacement=changing;
    assert(runtime_gpu_memory_map(&replacement,Page,3,0x10,physical+5*Page,Page));
    assert(replacement==changing);
    void* denied_flexible=nullptr;assert(flexible_map(&denied_flexible,Page,3,0) && !denied_flexible);
    auto reserve=reinterpret_cast<int(*)(void**,std::uint64_t,int,std::uint64_t)>(runtime_memory_resolve("7oxv3PPCumo#p#J"));
    assert(reserve);void* denied_reserve=nullptr;
    assert(reserve(&denied_reserve,Page,0,Page) && !denied_reserve);
    auto release_physical=reinterpret_cast<int(*)(std::uint64_t,std::uint64_t)>(runtime_memory_resolve("MBuItvba6z8#p#J"));
    assert(release_physical && release_physical(physical,8*Page));
    assert(runtime_gpu_memory_snapshot(changing,alias_copy.data(),Page));
    assert(Uniform(alias_copy,0x36) || Uniform(alias_copy,0xa7));
    assert(probe.ends==1); // Failure never changes mappings or invokes end.
    assert(runtime_gpu_memory_set_mutation_hooks(nullptr,nullptr,nullptr));

    // Unregistration must retain callback/context ownership until the paired
    // end, while allowing an active callback to read the mapping table.
    MutationProbe blocked;blocked.source=changing;blocked.protect=protect;blocked.block=true;
    assert(runtime_gpu_memory_set_mutation_hooks(&blocked,MutationProbe::Begin,MutationProbe::End));
    auto started=blocked.entered.get_future();
    auto mutation=std::async(std::launch::async,[&]{return protect(reinterpret_cast<void*>(changing),Page,3);});
    started.get();
    auto unregister=std::async(std::launch::async,[]{return runtime_gpu_memory_set_mutation_hooks(nullptr,nullptr,nullptr);});
    assert(unregister.wait_for(std::chrono::milliseconds(5))==std::future_status::timeout);
    assert(runtime_gpu_memory_snapshot(changing,alias_copy.data(),Page));
    {std::lock_guard lock(blocked.mutex);blocked.release=true;}
    blocked.changed.notify_one();assert(!mutation.get());assert(unregister.get());
    assert(blocked.begins==1 && blocked.ends==1);
    puts("Mapping mutation guards: nested admission, safe refusal, paired lifetime and concurrent unregister passed");
}
