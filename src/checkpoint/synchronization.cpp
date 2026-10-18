// SPDX-License-Identifier: GPL-2.0-or-later
// Guest synchronization ownership and wait records never contain pthreads or
// native stack addresses. CVs below only wake recreated native callers.
#include "synchronization.h"
extern "C" {
#include "../runtime.h"
void* runtime_thread_current(void);
}
#include <algorithm>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <sys/time.h>

namespace Bb::Checkpoint {
namespace {
using Clock=std::chrono::steady_clock;
constexpr std::uint64_t First=0x6f00000000ull,Last=0x7000000000ull;
constexpr std::size_t MaxObjects=1<<20;
constexpr std::int32_t Error(unsigned code) {return std::bit_cast<std::int32_t>(0x80020000u|code);}
constexpr std::int32_t Interrupted=std::bit_cast<std::int32_t>(0x81000001u);
enum class Phase:std::uint32_t {Condition=1,Mutex=2,Done=3};
struct Attr {std::uint32_t type=1,protocol=0;};
struct Mutex {Attr attr;std::uint64_t owner=0;std::uint32_t recursion=0;};
struct Wait {
    std::uint64_t thread=0,sequence=0,mutex=0,condition=0;
    Phase phase=Phase::Mutex;
    std::int32_t result=0;
    bool timed=false,attached=false,detachable=false;
    Clock::time_point deadline{};
};
struct Kernel {
    std::mutex lock;
    std::condition_variable changed;
    std::map<std::uint64_t,Attr> attrs;
    std::map<std::uint64_t,Mutex> mutexes;
    std::set<std::uint64_t> conditions;
    std::map<std::uint64_t,Wait> waits;
    std::uint64_t next=First,sequence=1;
    bool interrupted=false,paused=false;
    Clock::time_point paused_at{};
};
Kernel& Get() {static auto* k=new Kernel;return *k;}
using Lock=std::unique_lock<std::mutex>;
void Thawed(Kernel& k,Lock& lock) {k.changed.wait(lock,[&]{return !k.paused;});}
std::uint64_t Id(void* p) {return reinterpret_cast<std::uintptr_t>(p);}
std::uint64_t Thread() {return Id(runtime_thread_current());}
std::uint64_t Allocate(Kernel& k) {
    if(k.next>=Last){std::fprintf(stderr,"Checkpoint: synchronization identity range exhausted (live attrs %zu, mutexes %zu, conditions %zu)\n",k.attrs.size(),k.mutexes.size(),k.conditions.size());return 0;}
    auto id=k.next;k.next+=8;return id;
}
std::int32_t EnsureMutex(Kernel& k,void** slot,std::uint64_t& id) {
    if(!slot)return Error(22);
    id=__atomic_load_n(reinterpret_cast<std::uintptr_t*>(slot),__ATOMIC_ACQUIRE);
    if(id<2) {
        id=Allocate(k);if(!id)return Error(12);
        k.mutexes.emplace(id,Mutex{});__atomic_store_n(slot,reinterpret_cast<void*>(id),__ATOMIC_RELEASE);
    }
    if(!k.mutexes.contains(id)) {
        std::fprintf(stderr,"Checkpoint: unknown mutex handle %#llx in slot %p\n",static_cast<unsigned long long>(id),static_cast<void*>(slot));return Error(22);
    }
    return 0;
}
std::int32_t EnsureCond(Kernel& k,void** slot,std::uint64_t& id) {
    if(!slot)return Error(22);
    id=__atomic_load_n(reinterpret_cast<std::uintptr_t*>(slot),__ATOMIC_ACQUIRE);
    if(id<2) {
        id=Allocate(k);if(!id)return Error(12);
        k.conditions.insert(id);__atomic_store_n(slot,reinterpret_cast<void*>(id),__ATOMIC_RELEASE);
    }
    return k.conditions.contains(id)?0:Error(22);
}
std::int32_t Finish(Kernel& k,Lock& lock,std::uint64_t thread) {
    auto& w=k.waits.at(thread);
    for(;;) {
        Thawed(k,lock);
        if(k.interrupted && w.detachable) {w.attached=false;return Interrupted;}
        auto& mutex=k.mutexes.at(w.mutex);
        if(w.phase==Phase::Condition && w.timed && Clock::now()>=w.deadline) {
            w.phase=Phase::Mutex;w.result=Error(60);
        }
        if(w.phase==Phase::Mutex && !mutex.owner) {mutex.owner=thread;mutex.recursion=1;w.phase=Phase::Done;}
        if(w.phase==Phase::Done) {auto result=w.result;k.waits.erase(thread);return result;}
        if(!w.condition && w.timed && Clock::now()>=w.deadline) {k.waits.erase(thread);return Error(60);}
        if(w.timed && (w.phase==Phase::Condition || !w.condition)) k.changed.wait_until(lock,w.deadline);
        else k.changed.wait(lock);
    }
}
std::int32_t Complete(std::int32_t result) {
    if(result==Interrupted) {if(!bb_checkpoint_runtime_replay())std::abort();return 0;}
    return result;
}
std::int32_t AttrInit(void** slot) {
    if(!slot)return Error(22);
    auto& k=Get();Lock lock(k.lock);Thawed(k,lock);auto id=Allocate(k);if(!id)return Error(12);
    k.attrs.emplace(id,Attr{});*slot=reinterpret_cast<void*>(id);return 0;
}
std::int32_t AttrType(void** slot,int type) {
    auto& k=Get();Lock lock(k.lock);Thawed(k,lock);auto a=k.attrs.find(slot?Id(*slot):0);
    if(a==k.attrs.end() || type<1 || type>4) {
        std::fprintf(stderr,"Checkpoint: bad mutex attribute slot %p, value %#llx, type %d\n",static_cast<void*>(slot),static_cast<unsigned long long>(slot?Id(*slot):0),type);return Error(22);
    }
    a->second.type=static_cast<std::uint32_t>(type);return 0;
}
std::int32_t AttrProtocol(void** slot,int protocol) {
    auto& k=Get();Lock lock(k.lock);Thawed(k,lock);auto a=k.attrs.find(slot?Id(*slot):0);
    if(a==k.attrs.end() || protocol<0 || protocol>2)return Error(22);
    a->second.protocol=static_cast<std::uint32_t>(protocol);return 0;
}
std::int32_t AttrDestroy(void** slot) {
    auto& k=Get();Lock lock(k.lock);Thawed(k,lock);
    if(!slot || !k.attrs.erase(Id(*slot)))return Error(22);
    *slot=nullptr;return 0;
}
std::int32_t MutexInit(void** slot,void** attr,const char*) {
    if(!slot)return Error(22);
    auto& k=Get();Lock lock(k.lock);Thawed(k,lock);Attr a;
    if(attr) {auto found=k.attrs.find(Id(*attr));if(found==k.attrs.end())return Error(22);a=found->second;}
    auto id=Allocate(k);if(!id)return Error(12);
    k.mutexes.emplace(id,Mutex{a});*slot=reinterpret_cast<void*>(id);return 0;
}
std::int32_t LockMutex(void** slot,bool try_only,const std::uint32_t* timeout) {
    const auto thread=Thread();const bool detachable=bb_checkpoint_runtime_can_replay();
    auto& k=Get();Lock lock(k.lock);Thawed(k,lock);std::uint64_t id=0;
    if(auto error=EnsureMutex(k,slot,id))return error;
    if(auto found=k.waits.find(thread);found!=k.waits.end()) {
        if(found->second.attached || found->second.mutex!=id || found->second.condition)return Error(22);
        found->second.attached=true;found->second.detachable=detachable;return Complete(Finish(k,lock,thread));
    }
    auto& m=k.mutexes.at(id);
    if(m.owner==thread && m.attr.type==2) {
        if(m.recursion==UINT32_MAX)return Error(35);
        ++m.recursion;return 0;
    }
    if(!m.owner){m.owner=thread;m.recursion=1;return 0;}
    if(try_only)return Error(16);
    if(m.owner==thread && m.attr.type!=3)return Error(11);
    if(timeout && !*timeout)return Error(60);
    if(k.waits.size()>=65536 || k.sequence==UINT64_MAX)return Error(12);
    Wait w;w.thread=thread;w.sequence=k.sequence++;w.mutex=id;w.attached=true;w.detachable=detachable;
    w.timed=timeout;if(timeout)w.deadline=Clock::now()+std::chrono::microseconds(*timeout);
    k.waits.emplace(thread,w);return Complete(Finish(k,lock,thread));
}
std::int32_t MutexLock(void** slot) {return LockMutex(slot,false,nullptr);}
std::int32_t MutexTry(void** slot) {return LockMutex(slot,true,nullptr);}
std::int32_t MutexTimed(void** slot,std::uint32_t timeout) {return LockMutex(slot,false,&timeout);}
std::int32_t MutexUnlock(void** slot) {
    const auto thread=Thread();auto& k=Get();Lock lock(k.lock);Thawed(k,lock);
    if(!slot || Id(*slot)==2)return Error(22);
    if(Id(*slot)<2)return Error(1);
    auto found=k.mutexes.find(Id(*slot));if(found==k.mutexes.end())return Error(22);
    auto& m=found->second;if(m.owner!=thread)return Error(1);
    if(!--m.recursion){m.owner=0;k.changed.notify_all();}return 0;
}
std::int32_t MutexDestroy(void** slot) {
    auto& k=Get();Lock lock(k.lock);Thawed(k,lock);
    if(!slot || Id(*slot)==2)return Error(22);if(Id(*slot)<2)return 0;
    auto found=k.mutexes.find(Id(*slot));if(found==k.mutexes.end())return Error(22);
    if(found->second.owner)return Error(16);
    for(const auto& [thread,w]:k.waits){(void)thread;if(w.mutex==found->first)return Error(16);}
    k.mutexes.erase(found);*slot=reinterpret_cast<void*>(2);return 0;
}
std::int32_t CondInit(void** slot,void**,const char*) {
    if(!slot)return Error(22);auto& k=Get();Lock lock(k.lock);Thawed(k,lock);
    auto id=Allocate(k);if(!id)return Error(12);k.conditions.insert(id);*slot=reinterpret_cast<void*>(id);return 0;
}
std::int32_t CondDestroy(void** slot) {
    auto& k=Get();Lock lock(k.lock);Thawed(k,lock);if(!slot)return Error(22);if(Id(*slot)<2)return 0;
    if(!k.conditions.contains(Id(*slot)))return Error(22);
    for(const auto& [thread,w]:k.waits){(void)thread;if(w.condition==Id(*slot))return Error(16);}
    k.conditions.erase(Id(*slot));*slot=nullptr;return 0;
}
std::int32_t WaitCond(void** condition,void** mutex,const std::chrono::nanoseconds* timeout) {
    const auto thread=Thread();const bool detachable=bb_checkpoint_runtime_can_replay();
    auto& k=Get();Lock lock(k.lock);Thawed(k,lock);std::uint64_t id=0;
    if(auto error=EnsureCond(k,condition,id))return error;
    auto found=k.mutexes.find(mutex?Id(*mutex):0);if(found==k.mutexes.end())return Error(22);
    if(auto waiting=k.waits.find(thread);waiting!=k.waits.end()) {
        if(waiting->second.attached || waiting->second.condition!=id || waiting->second.mutex!=found->first)return Error(22);
        waiting->second.attached=true;waiting->second.detachable=detachable;return Complete(Finish(k,lock,thread));
    }
    auto& m=found->second;if(m.owner!=thread)return Error(1);
    if(m.recursion!=1){std::fprintf(stderr,"Checkpoint: condition wait with mutex recursion %u\n",m.recursion);return Error(22);}
    if(k.waits.size()>=65536 || k.sequence==UINT64_MAX)return Error(12);
    Wait w;w.thread=thread;w.sequence=k.sequence++;w.mutex=found->first;w.condition=id;w.phase=Phase::Condition;
    w.attached=true;w.detachable=detachable;w.timed=timeout;if(timeout)w.deadline=Clock::now()+*timeout;
    k.waits.emplace(thread,w);m.owner=0;m.recursion=0;k.changed.notify_all();return Complete(Finish(k,lock,thread));
}
std::int32_t CondWait(void** c,void** m) {return WaitCond(c,m,nullptr);}
std::int32_t CondTimed(void** c,void** m,std::uint32_t us) {const auto t=std::chrono::nanoseconds(std::uint64_t(us)*1000);return WaitCond(c,m,&t);}
std::int32_t Wake(void** slot,bool all) {
    auto& k=Get();Lock lock(k.lock);Thawed(k,lock);std::uint64_t id=0;
    if(auto error=EnsureCond(k,slot,id))return error;
    Wait* first=nullptr;
    for(auto& [thread,w]:k.waits) {
        (void)thread;if(w.condition!=id || w.phase!=Phase::Condition)continue;
        if(all)w.phase=Phase::Mutex;
        else if(!first || w.sequence<first->sequence)first=&w;
    }
    if(first)first->phase=Phase::Mutex;k.changed.notify_all();return 0;
}
std::int32_t CondSignal(void** c) {return Wake(c,false);}
std::int32_t CondBroadcast(void** c) {return Wake(c,true);}
int Posix(std::int32_t result) {return result?static_cast<int>(static_cast<std::uint32_t>(result)&0xffff):0;}
int PAttrInit(void** s){return Posix(AttrInit(s));}
int PAttrType(void** s,int t){return Posix(AttrType(s,t));}
int PAttrDestroy(void** s){return Posix(AttrDestroy(s));}
int PMutexInit(void** s,void** a){return Posix(MutexInit(s,a,nullptr));}
int PMutexDestroy(void** s){return Posix(MutexDestroy(s));}
int PMutexLock(void** s){return Posix(MutexLock(s));}
int PMutexTry(void** s){return Posix(MutexTry(s));}
int PMutexUnlock(void** s){return Posix(MutexUnlock(s));}
int PCondInit(void** s,void** a){return Posix(CondInit(s,a,nullptr));}
int PCondDestroy(void** s){return Posix(CondDestroy(s));}
int PCondWait(void** c,void** m){return Posix(CondWait(c,m));}
int PCondSignal(void** c){return Posix(CondSignal(c));}
int PCondBroadcast(void** c){return Posix(CondBroadcast(c));}
struct Timespec {std::int64_t sec,nsec;};
int PCondTimed(void** c,void** m,const Timespec* abs) {
    if(!abs || abs->sec<0 || abs->sec>INT64_MAX/1000000000-1 || abs->nsec<0 || abs->nsec>=1000000000)return 22;
    timespec now{};clock_gettime(CLOCK_REALTIME,&now);
    const auto remaining=std::max<std::int64_t>(0,(abs->sec-now.tv_sec)*1000000000+abs->nsec-now.tv_nsec);
    const auto timeout=std::chrono::nanoseconds(remaining);return Posix(WaitCond(c,m,&timeout));
}
}
void SyncInterrupt(bool value) {auto& k=Get();Lock lock(k.lock);k.interrupted=value;k.changed.notify_all();}
std::size_t SyncWaiters(bool condition) {
    auto& k=Get();Lock lock(k.lock);std::size_t count=0;
    for(const auto& [thread,w]:k.waits){(void)thread;if(bool(w.condition)==condition && w.phase!=Phase::Done)++count;}
    return count;
}
void SyncThaw() {
    auto& k=Get();Lock lock(k.lock);if(!k.paused)return;
    const auto elapsed=Clock::now()-k.paused_at;
    for(auto& [thread,w]:k.waits){(void)thread;if(w.timed)w.deadline+=elapsed;}
    k.paused=false;k.changed.notify_all();
}
bool SyncPause(std::vector<std::uint8_t>& out,std::string& error) {
    auto& k=Get();Lock lock(k.lock);if(k.paused){error="Synchronization already paused.";return false;}
    if(k.attrs.size()>MaxObjects || k.mutexes.size()>MaxObjects || k.conditions.size()>MaxObjects || k.waits.size()>65536) {
        error="Too many live synchronization objects for this checkpoint schema.";return false;
    }
    std::vector<std::uint8_t> bytes{'B','B','S','Y','N','C','1',0};
    const auto put=[&](std::uint64_t v){for(unsigned i=0;i<8;++i)bytes.push_back(std::uint8_t(v>>(8*i)));};
    put(k.next);put(k.sequence);put(k.attrs.size());put(k.mutexes.size());put(k.conditions.size());put(k.waits.size());
    for(const auto& [id,a]:k.attrs){put(id);put(a.type);put(a.protocol);}
    for(const auto& [id,m]:k.mutexes){put(id);put(m.attr.type);put(m.attr.protocol);put(m.owner);put(m.recursion);}
    for(auto id:k.conditions)put(id);
    const auto now=Clock::now();
    for(const auto& [thread,w]:k.waits) {
        put(thread);put(w.sequence);put(w.mutex);put(w.condition);put(static_cast<unsigned>(w.phase));
        put(static_cast<std::uint32_t>(w.result));put(w.timed);
        put(w.timed && w.deadline>now?std::chrono::duration_cast<std::chrono::nanoseconds>(w.deadline-now).count():0);
    }
    out=std::move(bytes);k.paused=true;k.paused_at=now;error.clear();return true;
}
bool SyncRestore(std::span<const std::uint8_t> bytes,std::string& error) {
    const auto fail=[&](const char* reason){error=reason;return false;};
    if(bytes.size()<56 || std::memcmp(bytes.data(),"BBSYNC1\0",8))return fail("Invalid synchronization format.");
    std::size_t at=8;const auto get=[&]{std::uint64_t v=0;for(unsigned i=0;i<8;++i)v|=std::uint64_t(bytes[at++])<<(8*i);return v;};
    const auto next=get(),sequence=get(),na=get(),nm=get(),nc=get(),nw=get();
    if(next<First || next>Last || (next-First)%8 || !sequence || na>MaxObjects || nm>MaxObjects || nc>MaxObjects || nw>65536 ||
       56+24*na+40*nm+8*nc+64*nw!=bytes.size())return fail("Invalid synchronization counts.");
    std::map<std::uint64_t,Attr> attrs;std::map<std::uint64_t,Mutex> mutexes;std::set<std::uint64_t> conditions,ids,sequences;
    std::map<std::uint64_t,Wait> waits;
    const auto id_valid=[&](std::uint64_t id){return id>=First && id<next && !((id-First)%8) && ids.insert(id).second;};
    for(std::size_t i=0;i<na;++i) {
        auto id=get(),type=get(),protocol=get();if(!id_valid(id) || type<1 || type>4 || protocol>2)return fail("Invalid mutex attributes.");
        attrs.emplace(id,Attr{std::uint32_t(type),std::uint32_t(protocol)});
    }
    for(std::size_t i=0;i<nm;++i) {
        auto id=get(),type=get(),protocol=get(),owner=get(),recursion=get();
        if(!id_valid(id) || type<1 || type>4 || protocol>2 || recursion>UINT32_MAX || bool(owner)!=bool(recursion) || (type!=2 && recursion>1))
            return fail("Invalid mutex owner or recursion.");
        mutexes.emplace(id,Mutex{Attr{std::uint32_t(type),std::uint32_t(protocol)},owner,std::uint32_t(recursion)});
    }
    for(std::size_t i=0;i<nc;++i){auto id=get();if(!id_valid(id))return fail("Invalid condition identity.");conditions.insert(id);}
    const auto now=Clock::now();
    for(std::size_t i=0;i<nw;++i) {
        Wait w;w.thread=get();w.sequence=get();w.mutex=get();w.condition=get();auto phase=get(),result=get(),timed=get(),remaining=get();
        const auto mutex=mutexes.find(w.mutex);
        if(!w.thread || !w.sequence || w.sequence>=sequence || !sequences.insert(w.sequence).second || waits.contains(w.thread) ||
           mutex==mutexes.end() || (w.condition && !conditions.contains(w.condition)) || phase<1 || phase>3 ||
           (phase==1 && !w.condition) || result>UINT32_MAX || timed>1 || (!timed && remaining) || remaining>INT64_MAX/2 ||
           (result && (result!=std::uint32_t(Error(60)) || !w.condition || phase==1 || !timed)) ||
           (phase==3 && (mutex->second.owner!=w.thread || mutex->second.recursion!=1)))return fail("Invalid synchronization waiter.");
        w.phase=static_cast<Phase>(phase);w.result=std::bit_cast<std::int32_t>(std::uint32_t(result));w.timed=timed;
        w.deadline=now+std::chrono::nanoseconds(remaining);waits.emplace(w.thread,w);
    }
    auto& k=Get();Lock lock(k.lock);
    if(k.paused || !k.attrs.empty() || !k.mutexes.empty() || !k.conditions.empty() || !k.waits.empty() || k.next!=First)
        return fail("Synchronization restore requires a fresh kernel.");
    k.attrs=std::move(attrs);k.mutexes=std::move(mutexes);k.conditions=std::move(conditions);k.waits=std::move(waits);
    k.next=next;k.sequence=sequence;k.paused=true;k.paused_at=now;error.clear();return true;
}
}

extern "C" std::uintptr_t bb_checkpoint_mutex_resolve(const char* name) {
    using namespace Bb::Checkpoint;
    const struct {const char* nid;std::uintptr_t fn;} table[]{
#define FN(n,f) {n,reinterpret_cast<std::uintptr_t>(&f)}
        FN("0TyVk4MSLt0#I#J",PCondInit),FN("RXXqi4CtF8w#I#J",PCondDestroy),FN("Op8TBGY5KHg#I#J",PCondWait),
        FN("27bAgiJmOh0#I#J",PCondTimed),FN("2MOy+rUfuhQ#I#J",PCondSignal),FN("mkx2fVhNMsg#I#J",PCondBroadcast),
        FN("2Tb92quprl0#p#J",CondInit),FN("g+PZd2hiacg#p#J",CondDestroy),FN("WKAXJ4XBPQ4#p#J",CondWait),
        FN("BmMjYxmew1w#p#J",CondTimed),FN("kDh-NfxgMtE#p#J",CondSignal),FN("JGgj7Uvrl+A#p#J",CondBroadcast),
        FN("IafI2PxcPnQ#p#J",MutexTimed),FN("dQHWEsJtoE4#I#J",PAttrInit),FN("mDmgMOGVUqg#I#J",PAttrType),
        FN("HF7lK46xzjY#I#J",PAttrDestroy),FN("ttHNfU+qDBU#I#J",PMutexInit),FN("ltCfaGr2JGE#I#J",PMutexDestroy),
        FN("7H0iTOciTLo#I#J",PMutexLock),FN("K-jXhbt2gn4#I#J",PMutexTry),FN("2Z+PpY6CaJg#I#J",PMutexUnlock),
        FN("F8bUHwAG284#p#J",AttrInit),FN("iMp8QpE+XO4#p#J",AttrType),FN("smWEktiyyG0#p#J",AttrDestroy),
        FN("1FGvU0i9saQ#p#J",AttrProtocol),FN("cmo1RIYva9o#p#J",MutexInit),FN("9UK1vLZQft4#p#J",MutexLock),
        FN("upoVrzMHFeE#p#J",MutexTry),FN("tn3VlD0hG60#p#J",MutexUnlock),FN("2Of0f+3mhhE#p#J",MutexDestroy)
#undef FN
    };
    for(const auto& entry:table)if(!std::strcmp(name,entry.nid))return entry.fn;
    return 0;
}
extern "C" int bb_checkpoint_mutex_replayable(const char* name) {
    const char* names[]{"9UK1vLZQft4#p#J","7H0iTOciTLo#I#J","IafI2PxcPnQ#p#J","WKAXJ4XBPQ4#p#J","BmMjYxmew1w#p#J","Op8TBGY5KHg#I#J","27bAgiJmOh0#I#J"};
    for(auto* candidate:names)if(!std::strcmp(name,candidate))return 1;return 0;
}
