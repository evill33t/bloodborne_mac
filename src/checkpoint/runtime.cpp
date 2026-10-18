// SPDX-License-Identifier: GPL-2.0-or-later
#include "runtime.h"
#include "coordinator.h"
#include "import_proxy.h"
#include "semaphore.h"
#include "synchronization.h"
#include "rwlock.h"
#include "thread.h"
#include "process.h"
#include "graphics.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <sys/mman.h>
#include <thread>
#include <utility>
extern "C" void *runtime_low_map(size_t,int);
extern "C" void *runtime_low_map_at(uintptr_t,size_t,int);
extern "C" void *runtime_thread_current(void);

namespace {
using namespace Bb::Checkpoint;
struct Bootstrap;
// Deliberately process-lifetime: guest workers may still exist when exit runs
// static destructors. A game shutdown joins them separately.
struct State {
    Coordinator coordinator;
    Semaphores semaphores;
    std::mutex metadata,operation;
    std::list<ImportProxy> imports;
    std::map<std::uint64_t,std::pair<std::uint64_t,std::uint64_t>> stacks;
    std::map<std::uint64_t,Bootstrap*> active;
    std::map<std::uint64_t,std::string> labels;
    std::vector<std::pair<std::uint64_t,std::uint64_t>> code;
    void *return_stub=nullptr;
    std::atomic<bool> healthy{true};
    BbCheckpointGraphicsHooks graphics{};
};
State& Get() {static auto *state=new State;return *state;}
struct Bootstrap {
    std::uint64_t id=0,result=0;
    std::atomic<std::uint64_t> import{0};
    BbCheckpointCpu host{};
};
thread_local Bootstrap *bootstrap=nullptr;
struct Frozen {
    std::unique_lock<std::mutex> operation;
    Coordinator::Frozen guest;
    std::vector<BbCheckpointRuntimeThread> threads;
    std::vector<std::uint8_t> semaphores;
    std::vector<std::uint8_t> synchronization;
    std::vector<std::uint8_t> rwlocks;
    std::vector<BbCheckpointThreadRecord> records;
    std::vector<BbCheckpointThreadAttrRecord> attributes;
    BbCheckpointProcessRecord process{};
    std::vector<BbCheckpointExitRecord> exits;
    BbCheckpointGraphicsHooks graphics{};
    void *graphics_token=nullptr;
    std::vector<BbCheckpointGraphicsChunk> graphics_chunks;
    Frozen(std::unique_lock<std::mutex> o,Coordinator::Frozen f):operation(std::move(o)),guest(std::move(f)) {}
    ~Frozen() {
        RwThaw();RwInterrupt(false);SyncThaw();SyncInterrupt(false);Get().semaphores.Thaw();Get().semaphores.Interrupt(false);
        if(graphics_token)graphics.release(graphics_token);
        guest.Resume();
    }
};
bool In(std::uint64_t at,std::uint64_t first,std::uint64_t size) {return at>=first && at-first<size;}
bool Valid(std::uint64_t id,const BbCheckpointCpu& cpu) {
    if(!bb_checkpoint_cpu_valid(&cpu))return false;
    auto& s=Get();std::lock_guard lock(s.metadata);
    const auto found=s.stacks.find(id);
    if(found==s.stacks.end())return false;
    const auto [stack,size]=found->second;
    if(cpu.gpr[BB_CK_RSP]<stack+1024 || cpu.gpr[BB_CK_RSP]>stack+size-8)return false;
    return std::any_of(s.code.begin(),s.code.end(),[&](const auto& r){return In(cpu.rip,r.first,r.second);});
}
void Observe(void*,BoundaryKind kind,const ImportProxy& import,const BbCheckpointCpu& cpu) {
    if(!bootstrap)return;
    bootstrap->import.store(kind==BoundaryKind::BeforeImport?import.identity:0,std::memory_order_relaxed);
    if(kind==BoundaryKind::AfterImport && (std::uint32_t(cpu.gpr[BB_CK_RAX])==0x80020016u || cpu.gpr[BB_CK_RAX]==22)) {
        auto& s=Get();std::lock_guard lock(s.metadata);const auto label=s.labels.find(import.identity);
        if(label!=s.labels.end() && (label->second.find("utex")!=std::string::npos || label->second.find("ond")!=std::string::npos)) {
            BbCheckpointCpu entry{};CurrentImportEntry(entry);
            std::fprintf(stderr,"Checkpoint sync EINVAL: %s at guest PC %#llx, args %#llx %#llx %#llx\n",label->second.c_str(),
                static_cast<unsigned long long>(entry.rip),static_cast<unsigned long long>(entry.gpr[BB_CK_RDI]),
                static_cast<unsigned long long>(entry.gpr[BB_CK_RSI]),static_cast<unsigned long long>(entry.gpr[BB_CK_RDX]));
        }
    }
    if(!Get().coordinator.Requested())return;
    if(!Valid(bootstrap->id,cpu)) {Get().healthy.store(false);return;}
    Get().coordinator.Boundary(bootstrap->id,cpu);
}
[[noreturn]] void Return(std::uint64_t result) {
    if(!bootstrap)std::abort();
    AbandonCurrentImports();bootstrap->result=result;
    bootstrap->host.gpr[BB_CK_RAX]=1;
    bb_checkpoint_cpu_restore(&bootstrap->host);
}
void* ReturnStub(std::uint64_t address=0) {
    auto& s=Get();std::lock_guard lock(s.metadata);
    if(s.return_stub)return !address || address==reinterpret_cast<std::uintptr_t>(s.return_stub)?s.return_stub:nullptr;
    auto* p=static_cast<std::uint8_t*>(address?runtime_low_map_at(address,16384,PROT_READ|PROT_WRITE):runtime_low_map(16384,PROT_READ|PROT_WRITE));
    if(!p)return nullptr;
    // ret from guest -> rdi=result, fake return slot for the noreturn host
    // function's SysV stack alignment. This code is rebound in each process.
    const std::uint8_t prefix[]{0x48,0x89,0xc7,0x6a,0,0x48,0xb8};
    std::memcpy(p,prefix,sizeof(prefix));
    auto target=reinterpret_cast<std::uintptr_t>(&Return);std::memcpy(p+7,&target,8);
    p[15]=0xff;p[16]=0xe0;
    if(mprotect(p,16384,PROT_READ|PROT_EXEC))return nullptr;
    s.return_stub=p;return p;
}
void Error(char* out,std::size_t size,const std::string& message) {
    if(out && size)std::snprintf(out,size,"%s",message.c_str());
}
bool LeafImport(const char* name) {
    // Keep this explicit: an API that can invoke a guest callback must retain
    // its native-frame guard even when no capture has yet been requested.
    static const char* leaves[]{"memcpy","memmove","memset","memcmp","strlen","__error","scePthreadSelf","pthread_self",
        "sceKernelGetProcessTime","sceKernelGetProcessTimeCounter","sceKernelGetProcessTimeCounterFrequency",
        "sceKernelReadTsc","sceKernelGetTscFrequency","sceKernelClockGettime","clock_gettime","sceKernelGettimeofday","gettimeofday",
        "scePthreadMutexUnlock","pthread_mutex_unlock","scePthreadMutexTrylock","pthread_mutex_trylock",
        "scePthreadMutexattrInit","pthread_mutexattr_init","scePthreadMutexattrDestroy","pthread_mutexattr_destroy",
        "scePthreadMutexattrSettype","pthread_mutexattr_settype","scePthreadMutexattrSetprotocol","scePthreadMutexInit","pthread_mutex_init",
        "scePthreadMutexDestroy","pthread_mutex_destroy","scePthreadCondSignal","pthread_cond_signal",
        "scePthreadCondBroadcast","pthread_cond_broadcast","scePthreadCondInit","pthread_cond_init","scePthreadCondDestroy","pthread_cond_destroy",
        "scePthreadRwlockUnlock","scePthreadRwlockTryrdlock","scePthreadRwlockTrywrlock","scePthreadRwlockInit","scePthreadRwlockDestroy",
        "sceKernelCreateSema","sceKernelSignalSema","sceKernelPollSema","sceKernelCancelSema","sceKernelDeleteSema"};
    for(auto* leaf:leaves)if(!std::strcmp(name,leaf))return true;return false;
}
void StartAudit() {
    static std::once_flag once;
    std::call_once(once,[] {
        const char* setting=std::getenv("BB_CHECKPOINT_AUDIT_FILE");
        if(!setting || !*setting)return;
        const std::string path=setting;
        std::thread([path] {
            std::uint64_t previous=0;
            {std::ifstream input(path);input>>previous;}
            for(;;) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                std::uint64_t request=0;
                {std::ifstream input(path);if(!(input>>request) || request==previous)continue;}
                previous=request;char error[4096]{};void* token=nullptr;
                std::string result;
                if(bb_checkpoint_runtime_freeze(3000,&token,error,sizeof(error))) {
                    const BbCheckpointRuntimeThread* threads=nullptr;
                    const auto count=bb_checkpoint_runtime_threads(token,&threads);
                    const BbCheckpointGraphicsChunk* chunks=nullptr;
                    const auto graphics=bb_checkpoint_runtime_graphics_chunks(token,&chunks);
                    result="Guest "+std::string(graphics?"CPU/GPU boundary":"CPU rendezvous")+" passed for "+std::to_string(count)+" threads";
                    if(graphics) {
                        std::uint64_t bytes=0;for(std::size_t n=0;n<graphics;++n)bytes+=chunks[n].size;
                        result+=", "+std::to_string(graphics)+" GPU chunks ("+std::to_string(bytes)+" bytes available; not read)";
                    }
                    result+=". No playable checkpoint was saved.\n";
                    bb_checkpoint_runtime_thaw(token);
                } else result=std::string("Guest CPU rendezvous failed: ")+error+"\n";
                std::fprintf(stderr,"Checkpoint audit: %s",result.c_str());
                std::ofstream output(path+".result",std::ios::trunc);output<<request<<"\n"<<result;
            }
        }).detach();
    });
}
}
extern "C" int bb_checkpoint_runtime_enabled() {
    static const bool enabled=[] {const char* p=std::getenv("BB_CHECKPOINT_ENABLE");return p && !std::strcmp(p,"1");}();
    return enabled;
}
extern "C" int bb_checkpoint_runtime_code(std::uint64_t address,std::uint64_t size) {
    if(!address || address>=1ull<<40 || !size || size>(1ull<<40)-address)return 0;
    auto& s=Get();std::lock_guard lock(s.metadata);s.code.emplace_back(address,size);return 1;
}
extern "C" std::uintptr_t bb_checkpoint_runtime_import(void* code,std::uintptr_t target,std::uint64_t identity,int replayable) {
    if(!bb_checkpoint_runtime_enabled())return target;
    auto& s=Get();std::lock_guard lock(s.metadata);
    s.imports.push_back({reinterpret_cast<void*>(target),identity,nullptr,Observe,nullptr,replayable!=0});
    s.imports.back().guest_only=true;
    if(!EmitImportProxy({static_cast<std::uint8_t*>(code),32},s.imports.back())) {s.imports.pop_back();return 0;}
    s.code.emplace_back(reinterpret_cast<std::uintptr_t>(code),32);
    return reinterpret_cast<std::uintptr_t>(code);
}
extern "C" void bb_checkpoint_runtime_import_label(std::uint64_t identity,const char* label) {
    if(!label)return;
    auto& s=Get();std::lock_guard lock(s.metadata);s.labels[identity]=label;
    for(auto& import:s.imports)if(import.identity==identity) {
        import.requested=s.coordinator.RequestFlag();import.leaf=LeafImport(label);break;
    }
}
extern "C" std::uint64_t bb_checkpoint_runtime_run(std::uint64_t id,void* entry,void* arg0,void* arg1,
                                                   void* stack,std::size_t size,const BbCheckpointCpu* restored) {
    if(!bb_checkpoint_runtime_enabled() || bootstrap || !id || !stack || size<16384 || !ReturnStub())std::abort();
    auto& s=Get();
    {std::lock_guard lock(s.metadata);if(!s.stacks.emplace(id,std::pair{reinterpret_cast<std::uintptr_t>(stack),size}).second)std::abort();}
    // Admission during an ongoing capture waits outside guest execution. The
    // creating guest can finish pthread_create and reach its own boundary.
    Bootstrap local;local.id=id;bootstrap=&local;
    {std::lock_guard lock(s.metadata);s.active.emplace(id,&local);}
    while(!s.coordinator.Register(id))std::this_thread::yield();
    StartAudit();
    if(!bb_checkpoint_cpu_save(&local.host)) {
        BbCheckpointCpu guest{};
        if(restored) {guest=*restored;if(!Valid(id,guest))std::abort();}
        else {
            std::memcpy(guest.fx,local.host.fx,sizeof(guest.fx));
            guest.features=1;guest.rflags=0x202;guest.rip=reinterpret_cast<std::uintptr_t>(entry);
            guest.gpr[BB_CK_RSP]=(reinterpret_cast<std::uintptr_t>(stack)+size-64)&~15ull;
            guest.gpr[BB_CK_RSP]-=8;
            *reinterpret_cast<std::uintptr_t*>(guest.gpr[BB_CK_RSP])=reinterpret_cast<std::uintptr_t>(s.return_stub);
            guest.gpr[BB_CK_RDI]=reinterpret_cast<std::uintptr_t>(arg0);guest.gpr[BB_CK_RSI]=reinterpret_cast<std::uintptr_t>(arg1);
        }
        bb_checkpoint_cpu_restore(&guest);
    }
    const auto result=local.result;bootstrap=nullptr;
    if(!s.coordinator.Unregister(id))std::abort();
    {std::lock_guard lock(s.metadata);s.stacks.erase(id);s.active.erase(id);}
    return result;
}
extern "C" int bb_checkpoint_runtime_exit(void* result) {
    if(!bootstrap)return 0;
    Return(reinterpret_cast<std::uintptr_t>(result));
}
extern "C" int bb_checkpoint_runtime_requested() {return bb_checkpoint_runtime_enabled() && Get().coordinator.Requested();}
extern "C" int bb_checkpoint_runtime_can_replay() {return bootstrap && CanReplayCurrentImport();}
extern "C" int bb_checkpoint_runtime_replay() {return bootstrap && ReplayCurrentImport();}
extern "C" void bb_checkpoint_runtime_enter_callback() {if(bb_checkpoint_runtime_enabled())EnterNativeGuestCallback();}
extern "C" void bb_checkpoint_runtime_leave_callback() {if(bb_checkpoint_runtime_enabled())LeaveNativeGuestCallback();}
extern "C" int bb_checkpoint_runtime_replayable_import(const char* name) {
    return name && (!std::strcmp(name,"Zxa0VhQVTsk#p#J") || !std::strcmp(name,"onNY9Byn-W8#p#J") ||
        !std::strcmp(name,"h9CcP3J0oVM#I#J") || bb_checkpoint_mutex_replayable(name) || bb_checkpoint_rwlock_replayable(name));
}
extern "C" std::uint64_t bb_checkpoint_runtime_current() {return bootstrap?bootstrap->id:0;}
extern "C" std::uint64_t bb_checkpoint_runtime_return_stub() {return reinterpret_cast<std::uintptr_t>(ReturnStub());}
extern "C" int bb_checkpoint_runtime_bind_return_stub(std::uint64_t address) {return address && ReturnStub(address);}
extern "C" int bb_checkpoint_runtime_valid_continuation(const BbCheckpointCpu* cpu,std::uint64_t stack,std::uint64_t size) {
    if(!cpu || !bb_checkpoint_cpu_valid(cpu) || size<16384 || stack>=1ull<<40 || size>(1ull<<40)-stack ||
       cpu->gpr[BB_CK_RSP]<stack+1024 || cpu->gpr[BB_CK_RSP]>stack+size-8)return 0;
    auto& s=Get();std::lock_guard lock(s.metadata);
    return std::any_of(s.code.begin(),s.code.end(),[&](const auto& r){return In(cpu->rip,r.first,r.second);});
}
extern "C" int bb_checkpoint_runtime_freeze(std::uint32_t timeout,void** token,char* error,std::size_t capacity) {
    if(!token) {Error(error,capacity,"Missing checkpoint token output.");return 0;}
    *token=nullptr;
    auto& s=Get();std::unique_lock operation(s.operation,std::try_to_lock);
    if(!operation || !bb_checkpoint_runtime_enabled() || !s.healthy.load() || !ImportProxiesHealthy()) {
        Error(error,capacity,"Checkpoint runtime disabled, unhealthy, or another request is active.");return 0;
    }
    try {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
        std::string why;auto stopped=s.coordinator.Freeze(std::chrono::milliseconds(timeout),why,[&]{s.semaphores.Interrupt(true);SyncInterrupt(true);RwInterrupt(true);});
        if(!stopped) {
            s.semaphores.Interrupt(false);
            SyncInterrupt(false);
            RwInterrupt(false);
            std::lock_guard lock(s.metadata);
            for(const auto& [id,thread]:s.active) {
                const auto import=thread->import.load(std::memory_order_relaxed);
                if(!import)continue;
                const auto label=s.labels.find(import);char description[128];
                std::snprintf(description,sizeof(description)," [thread 0x%llx, import %llu: %s]",
                    static_cast<unsigned long long>(id),static_cast<unsigned long long>(import),
                    label==s.labels.end()?"unlabelled":label->second.c_str());
                why+=description;
            }
            Error(error,capacity,why);return 0;
        }
        auto f=std::make_unique<Frozen>(std::move(operation),std::move(*stopped));
        if(s.graphics.pause) {
            const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
            if(remaining<=0){Error(error,capacity,"No time remains to quiesce graphics.");return 0;}
            f->graphics=s.graphics;
            if(!f->graphics.pause(std::uint32_t(remaining),&f->graphics_token,error,capacity))return 0;
            if(!f->graphics_token){Error(error,capacity,"Graphics provider returned an empty capture.");return 0;}
            std::size_t count=0;
            if(f->graphics.describe(f->graphics_token,nullptr,0,&count) || !count || count>65536) {
                Error(error,capacity,"Invalid graphics checkpoint manifest.");return 0;
            }
            f->graphics_chunks.resize(count);
            if(f->graphics.describe(f->graphics_token,f->graphics_chunks.data(),count,&count) || count!=f->graphics_chunks.size()) {
                Error(error,capacity,"Graphics checkpoint manifest changed.");return 0;
            }
            auto sorted=f->graphics_chunks;std::sort(sorted.begin(),sorted.end(),[](auto a,auto b){return a.id<b.id;});
            for(std::size_t n=0;n<sorted.size();++n)if(!sorted[n].size || (n && sorted[n-1].id==sorted[n].id)) {
                Error(error,capacity,"Invalid graphics checkpoint chunk.");return 0;
            }
        }
        Semaphores::Image image;
        if(!s.semaphores.Pause(image,why) || !Semaphores::Encode(image,f->semaphores,why) || !SyncPause(f->synchronization,why) || !RwPause(f->rwlocks,why)) {
            Error(error,capacity,why);return 0;
        }
        std::size_t records=0,attributes=0;
        if(runtime_thread_checkpoint_export(nullptr,0,&records) || records>4096 ||
           runtime_thread_checkpoint_attr_export(nullptr,0,&attributes) || attributes>4096) {
            Error(error,capacity,"Cannot export guest thread/attribute metadata.");return 0;
        }
        f->records.resize(records);f->attributes.resize(attributes);
        if(runtime_thread_checkpoint_export(f->records.data(),records,&records) ||
           runtime_thread_checkpoint_attr_export(f->attributes.data(),attributes,&attributes)) {
            Error(error,capacity,"Guest threads changed while exporting metadata.");return 0;
        }
        std::size_t live=0;
        for(const auto& record:f->records) {
            const auto found=std::find_if(f->guest.Threads().begin(),f->guest.Threads().end(),[&](const auto& t){return t.id==record.id;});
            if(record.flags&2) {
                if(found!=f->guest.Threads().end()) {Error(error,capacity,"Finished guest thread still has an execution continuation.");return 0;}
            } else {
                ++live;
                if(found==f->guest.Threads().end()) {Error(error,capacity,"Guest thread creation, cleanup, or an untracked callback is still active.");return 0;}
            }
        }
        if(live!=f->guest.Threads().size()) {Error(error,capacity,"Guest thread continuation manifest is incomplete.");return 0;}
        if(runtime_checkpoint_process_export(&f->process,nullptr,0)) {Error(error,capacity,"Cannot export guest process state.");return 0;}
        f->exits.resize(f->process.handlers);
        if(runtime_checkpoint_process_export(&f->process,f->exits.data(),f->exits.size())) {
            Error(error,capacity,"Guest process state contains an unsupported native callback or pointer.");return 0;
        }
        std::lock_guard lock(s.metadata);
        for(const auto& t:f->guest.Threads()) {
            const auto [base,size]=s.stacks.at(t.id);
            f->threads.push_back({t.id,base,size,t.cpu});
        }
        if(std::chrono::steady_clock::now()>deadline){Error(error,capacity,"Runtime checkpoint preparation timed out.");return 0;}
        *token=f.release();Error(error,capacity,"");return 1;
    } catch(const std::exception& e) {s.semaphores.Interrupt(false);SyncInterrupt(false);RwInterrupt(false);Error(error,capacity,e.what());return 0;}
}
extern "C" int bb_checkpoint_runtime_graphics_hooks(const BbCheckpointGraphicsHooks* hooks) {
    if(!hooks || !hooks->pause || !hooks->release || !hooks->describe || !hooks->read)return 0;
    auto& s=Get();std::unique_lock lock(s.operation,std::try_to_lock);
    if(!lock || s.graphics.pause)return 0;s.graphics=*hooks;return 1;
}
extern "C" std::size_t bb_checkpoint_runtime_graphics_chunks(void* token,const BbCheckpointGraphicsChunk** chunks) {
    if(!token || !chunks)return 0;auto& f=*static_cast<Frozen*>(token);*chunks=f.graphics_chunks.data();return f.graphics_chunks.size();
}
extern "C" int bb_checkpoint_runtime_graphics_read(void* token,std::uint64_t id,std::uint64_t offset,void* bytes,std::size_t size,char* error,std::size_t capacity) {
    if(!token || (!bytes && size)){Error(error,capacity,"Invalid graphics checkpoint read.");return 0;}
    auto& f=*static_cast<Frozen*>(token);
    const auto c=std::find_if(f.graphics_chunks.begin(),f.graphics_chunks.end(),[&](const auto& v){return v.id==id;});
    if(c==f.graphics_chunks.end() || offset>c->size || size>c->size-offset){Error(error,capacity,"Graphics checkpoint read is out of range.");return 0;}
    return f.graphics.read(f.graphics_token,id,offset,bytes,size,error,capacity);
}
extern "C" std::size_t bb_checkpoint_runtime_threads(void* token,const BbCheckpointRuntimeThread** out) {
    if(!token || !out)return 0;
    auto& f=*static_cast<Frozen*>(token);*out=f.threads.data();return f.threads.size();
}
extern "C" void bb_checkpoint_runtime_thaw(void* token) {delete static_cast<Frozen*>(token);}
extern "C" std::size_t bb_checkpoint_runtime_thread_records(void* token,const BbCheckpointThreadRecord** records) {
    if(!token || !records)return 0;auto& f=*static_cast<Frozen*>(token);*records=f.records.data();return f.records.size();
}
extern "C" std::size_t bb_checkpoint_runtime_thread_attributes(void* token,const BbCheckpointThreadAttrRecord** records) {
    if(!token || !records)return 0;auto& f=*static_cast<Frozen*>(token);*records=f.attributes.data();return f.attributes.size();
}
extern "C" int bb_checkpoint_runtime_process(void* token,const BbCheckpointProcessRecord** record,const BbCheckpointExitRecord** exits) {
    if(!token || !record || !exits)return 0;auto& f=*static_cast<Frozen*>(token);*record=&f.process;*exits=f.exits.data();return 1;
}
extern "C" std::int32_t bb_checkpoint_sema_create(std::uint32_t* out,std::uint32_t attr,std::int32_t initial,std::int32_t maximum) {
    return Get().semaphores.Create(out,attr,initial,maximum);
}
extern "C" std::int32_t bb_checkpoint_sema_wait(std::uint32_t id,std::int32_t need,std::uint32_t* timeout) {
    const auto thread=reinterpret_cast<std::uintptr_t>(runtime_thread_current());
    const bool detachable=bb_checkpoint_runtime_can_replay();
    auto& s=Get().semaphores;
    const auto r=s.HasWait(thread)?s.Resume(thread,detachable):s.Wait(thread,id,need,timeout?std::optional{*timeout}:std::nullopt,detachable);
    if(r.interrupted) {if(!bb_checkpoint_runtime_replay())std::abort();return 0;}
    if(timeout)*timeout=r.remaining_us;
    return r.value;
}
extern "C" std::int32_t bb_checkpoint_sema_poll(std::uint32_t id,std::int32_t need) {return Get().semaphores.Poll(id,need);}
extern "C" std::int32_t bb_checkpoint_sema_signal(std::uint32_t id,std::int32_t count) {return Get().semaphores.Signal(id,count);}
extern "C" std::int32_t bb_checkpoint_sema_cancel(std::uint32_t id,std::int32_t count,std::int32_t* waiters) {return Get().semaphores.Cancel(id,count,waiters);}
extern "C" std::int32_t bb_checkpoint_sema_delete(std::uint32_t id) {return Get().semaphores.Delete(id);}
extern "C" unsigned bb_checkpoint_sema_waiters(std::uint32_t id) {return static_cast<unsigned>(Get().semaphores.Pending(id));}
extern "C" std::size_t bb_checkpoint_runtime_semaphores(void* token,const unsigned char** bytes) {
    if(!token || !bytes)return 0;
    auto& f=*static_cast<Frozen*>(token);*bytes=f.semaphores.data();return f.semaphores.size();
}
extern "C" int bb_checkpoint_runtime_restore_semaphores(const unsigned char* bytes,std::size_t size,char* error,std::size_t capacity) {
    if(!bytes && size)return 0;
    Semaphores::Image image;std::string why;
    try {
        if(!Semaphores::Decode({bytes,size},image,why) || !Get().semaphores.Restore(image,why)) {Error(error,capacity,why);return 0;}
        Error(error,capacity,"");return 1;
    } catch(const std::exception& e) {Error(error,capacity,e.what());return 0;}
}
extern "C" void bb_checkpoint_runtime_activate_semaphores() {Get().semaphores.Thaw();}
extern "C" std::size_t bb_checkpoint_runtime_synchronization(void* token,const unsigned char** bytes) {
    if(!token || !bytes)return 0;
    auto& f=*static_cast<Frozen*>(token);*bytes=f.synchronization.data();return f.synchronization.size();
}
extern "C" int bb_checkpoint_runtime_restore_synchronization(const unsigned char* bytes,std::size_t size,char* error,std::size_t capacity) {
    if(!bytes && size)return 0;
    std::string why;
    try {const bool ok=SyncRestore({bytes,size},why);Error(error,capacity,why);return ok;}
    catch(const std::exception& e){Error(error,capacity,e.what());return 0;}
}
extern "C" void bb_checkpoint_runtime_activate_synchronization() {SyncThaw();}
extern "C" std::size_t bb_checkpoint_runtime_rwlocks(void* token,const unsigned char** bytes) {
    if(!token || !bytes)return 0;auto& f=*static_cast<Frozen*>(token);*bytes=f.rwlocks.data();return f.rwlocks.size();
}
extern "C" int bb_checkpoint_runtime_restore_rwlocks(const unsigned char* bytes,std::size_t size,char* error,std::size_t capacity) {
    if(!bytes && size)return 0;std::string why;
    try {const bool ok=RwRestore({bytes,size},why);Error(error,capacity,why);return ok;}
    catch(const std::exception& e){Error(error,capacity,e.what());return 0;}
}
extern "C" void bb_checkpoint_runtime_activate_rwlocks() {RwThaw();}
