// SPDX-License-Identifier: GPL-2.0-or-later
#include "rwlock.h"
extern "C" {
#include "../runtime.h"
void* runtime_thread_current(void);
}
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <sys/time.h>

namespace Bb::Checkpoint {
namespace {
using Clock=std::chrono::steady_clock;
constexpr std::uint64_t First=0x6e00000000ull,Last=0x6f00000000ull;
constexpr std::int32_t Error(unsigned code) {return std::bit_cast<std::int32_t>(0x80020000u|code);}
struct Object {std::uint64_t writer=0;std::map<std::uint64_t,std::uint32_t> readers;};
struct Wait {
    std::uint64_t thread=0,object=0;
    bool writer=false,timed=false,attached=false,detachable=false;
    Clock::time_point deadline{};
};
struct Kernel {
    std::mutex lock;std::condition_variable changed;
    std::map<std::uint64_t,Object> objects;
    std::map<std::uint64_t,Wait> waits;
    std::uint64_t next=First;
    bool paused=false,interrupted=false;
    Clock::time_point paused_at{};
};
Kernel& Get() {static auto* k=new Kernel;return *k;}
using Lock=std::unique_lock<std::mutex>;
void Thawed(Kernel& k,Lock& lock) {k.changed.wait(lock,[&]{return !k.paused;});}
std::uint64_t Id(void* pointer) {return reinterpret_cast<std::uintptr_t>(pointer);}
std::int32_t Create(Kernel& k,void** out) {
    if(k.next>=Last)return Error(12);const auto id=k.next;k.next+=8;k.objects.emplace(id,Object{});*out=reinterpret_cast<void*>(id);return 0;
}
std::int32_t Init(void** out,void*const* attr,const char*) {
    if(!out || attr)return Error(22);auto& k=Get();Lock lock(k.lock);Thawed(k,lock);return Create(k,out);
}
std::int32_t Destroy(void** slot) {
    if(!slot)return Error(22);auto& k=Get();Lock lock(k.lock);Thawed(k,lock);if(!*slot)return 0;
    const auto id=Id(*slot);auto found=k.objects.find(id);if(found==k.objects.end())return Error(22);
    if(found->second.writer || !found->second.readers.empty())return Error(16);
    for(const auto& [thread,w]:k.waits){(void)thread;if(w.object==id)return Error(16);}
    k.objects.erase(found);*slot=reinterpret_cast<void*>(1);return 0;
}
bool Available(const Object& object,bool writer) {return !object.writer && (!writer || object.readers.empty());}
std::int32_t Acquire(Object& object,std::uint64_t thread,bool writer) {
    if(writer)object.writer=thread;
    else {
        auto& count=object.readers[thread];if(count==UINT32_MAX)return Error(35);++count;
    }
    return 0;
}
std::int32_t Finish(Kernel& k,Lock& lock,std::uint64_t thread) {
    auto& w=k.waits.at(thread);
    for(;;) {
        Thawed(k,lock);
        if(k.interrupted && w.detachable) {
            w.attached=false;if(!bb_checkpoint_runtime_replay())std::abort();return 0;
        }
        auto& object=k.objects.at(w.object);
        if(Available(object,w.writer)) {auto result=Acquire(object,thread,w.writer);k.waits.erase(thread);return result;}
        if(w.timed && Clock::now()>=w.deadline){k.waits.erase(thread);return Error(60);}
        if(w.timed)k.changed.wait_until(lock,w.deadline);else k.changed.wait(lock);
    }
}
struct Time {std::int64_t sec,nsec;};
std::int32_t LockRw(void** slot,bool writer,bool attempt,bool timed,const Time* time) {
    if(!slot)return Error(22);
    const auto thread=Id(runtime_thread_current());const bool detachable=bb_checkpoint_runtime_can_replay();
    auto& k=Get();Lock lock(k.lock);Thawed(k,lock);
    if(!*slot){if(auto result=Create(k,slot))return result;}
    const auto id=Id(*slot);auto found=k.objects.find(id);if(found==k.objects.end())return Error(22);
    if(auto waiting=k.waits.find(thread);waiting!=k.waits.end()) {
        if(waiting->second.attached || waiting->second.object!=id || waiting->second.writer!=writer)return Error(22);
        waiting->second.attached=true;waiting->second.detachable=detachable;return Finish(k,lock,thread);
    }
    auto& object=found->second;
    if(object.writer==thread || (writer && object.readers.contains(thread)))return Error(attempt?16:11);
    if(Available(object,writer))return Acquire(object,thread,writer);
    if(attempt)return Error(16);
    Clock::time_point deadline{};
    if(timed) {
        if(!time || time->nsec<0 || time->nsec>=1000000000 || time->sec>INT64_MAX/1000000000-1)return Error(22);
        timespec now{};clock_gettime(CLOCK_REALTIME,&now);
        if(time->sec<now.tv_sec || (time->sec==now.tv_sec && time->nsec<=now.tv_nsec))return Error(60);
        deadline=Clock::now()+std::chrono::nanoseconds((time->sec-now.tv_sec)*1000000000+time->nsec-now.tv_nsec);
    }
    if(k.waits.size()>=65536)return Error(12);
    k.waits.emplace(thread,Wait{thread,id,writer,timed,true,detachable,deadline});return Finish(k,lock,thread);
}
std::int32_t Read(void** r){return LockRw(r,false,false,false,nullptr);}
std::int32_t Write(void** r){return LockRw(r,true,false,false,nullptr);}
std::int32_t TryRead(void** r){return LockRw(r,false,true,false,nullptr);}
std::int32_t TryWrite(void** r){return LockRw(r,true,true,false,nullptr);}
std::int32_t TimedRead(void** r,const Time* t){return LockRw(r,false,false,true,t);}
std::int32_t TimedWrite(void** r,const Time* t){return LockRw(r,true,false,true,t);}
std::int32_t Unlock(void** slot) {
    if(!slot)return Error(22);const auto thread=Id(runtime_thread_current());auto& k=Get();Lock lock(k.lock);Thawed(k,lock);
    auto found=k.objects.find(Id(*slot));if(found==k.objects.end())return Error(22);auto& object=found->second;
    if(object.writer==thread)object.writer=0;
    else {auto reader=object.readers.find(thread);if(reader==object.readers.end())return Error(1);if(!--reader->second)object.readers.erase(reader);}
    k.changed.notify_all();return 0;
}
}
void RwInterrupt(bool value){auto& k=Get();Lock lock(k.lock);k.interrupted=value;k.changed.notify_all();}
std::size_t RwWaiters(){auto& k=Get();Lock lock(k.lock);return k.waits.size();}
void RwThaw() {
    auto& k=Get();Lock lock(k.lock);if(!k.paused)return;const auto elapsed=Clock::now()-k.paused_at;
    for(auto& [thread,w]:k.waits){(void)thread;if(w.timed)w.deadline+=elapsed;}k.paused=false;k.changed.notify_all();
}
bool RwPause(std::vector<std::uint8_t>& out,std::string& error) {
    auto& k=Get();Lock lock(k.lock);if(k.paused){error="Read/write locks already paused.";return false;}
    std::vector<std::uint8_t> bytes{'B','B','R','W','L','K','1',0};
    const auto put=[&](std::uint64_t value){for(unsigned i=0;i<8;++i)bytes.push_back(std::uint8_t(value>>(i*8)));};
    std::uint64_t holders=0;for(const auto& [id,o]:k.objects){(void)id;holders+=o.readers.size();}
    if(holders>65536 || k.objects.size()>(1<<20)){error="Too many read/write lock owners for this checkpoint schema.";return false;}
    put(k.next);put(k.objects.size());put(holders);put(k.waits.size());
    for(const auto& [id,o]:k.objects){put(id);put(o.writer);put(o.readers.size());for(const auto& [thread,count]:o.readers){put(thread);put(count);}}
    const auto now=Clock::now();
    for(const auto& [thread,w]:k.waits){put(thread);put(w.object);put(w.writer);put(w.timed);put(w.timed && w.deadline>now?std::chrono::duration_cast<std::chrono::nanoseconds>(w.deadline-now).count():0);}
    out=std::move(bytes);k.paused=true;k.paused_at=now;error.clear();return true;
}
bool RwRestore(std::span<const std::uint8_t> bytes,std::string& error) {
    const auto fail=[&](const char* why){error=why;return false;};
    if(bytes.size()<40 || std::memcmp(bytes.data(),"BBRWLK1\0",8))return fail("Invalid read/write lock format.");
    std::size_t at=8;const auto get=[&]{std::uint64_t value=0;for(unsigned i=0;i<8;++i)value|=std::uint64_t(bytes[at++])<<(i*8);return value;};
    const auto next=get(),count=get(),holders=get(),waiters=get();
    if(next<First || next>Last || (next-First)%8 || count>(1<<20) || holders>65536 || waiters>65536 ||
       bytes.size()!=40+24*count+16*holders+40*waiters)return fail("Invalid read/write lock counts.");
    std::map<std::uint64_t,Object> objects;std::map<std::uint64_t,Wait> waits;std::uint64_t remaining=holders;
    for(std::size_t i=0;i<count;++i) {
        // Reader counts inside each variable-length record are checked before
        // using them to advance the cursor; aggregate size alone is not enough.
        if(bytes.size()-at<24)return fail("Truncated read/write lock object.");
        const auto id=get(),writer=get(),readers=get();
        if(id<First || id>=next || (id-First)%8 || objects.contains(id) || readers>remaining || readers>(bytes.size()-at)/16 ||
           (writer && readers))return fail("Invalid read/write lock ownership.");
        Object object;object.writer=writer;
        for(std::uint64_t r=0;r<readers;++r) {
            const auto thread=get(),recursion=get();
            if(!thread || !recursion || recursion>UINT32_MAX || !object.readers.emplace(thread,std::uint32_t(recursion)).second)return fail("Invalid read owner.");
        }
        remaining-=readers;objects.emplace(id,std::move(object));
    }
    if(remaining || bytes.size()-at!=waiters*40)return fail("Invalid read/write lock holder sizes.");
    const auto now=Clock::now();
    for(std::size_t i=0;i<waiters;++i) {
        Wait w;w.thread=get();w.object=get();const auto writer=get(),timed=get(),ns=get();const auto object=objects.find(w.object);
        if(!w.thread || object==objects.end() || waits.contains(w.thread) || writer>1 || timed>1 || (!timed && ns) || ns>INT64_MAX/2 ||
           object->second.writer==w.thread || (writer && object->second.readers.contains(w.thread)))return fail("Invalid read/write lock waiter.");
        w.writer=writer;w.timed=timed;w.deadline=now+std::chrono::nanoseconds(ns);waits.emplace(w.thread,w);
    }
    auto& k=Get();Lock lock(k.lock);
    if(k.paused || !k.objects.empty() || !k.waits.empty() || k.next!=First)return fail("Read/write lock restore requires a fresh kernel.");
    k.objects=std::move(objects);k.waits=std::move(waits);k.next=next;k.paused=true;k.paused_at=now;error.clear();return true;
}
}
extern "C" std::uintptr_t bb_checkpoint_rwlock_resolve(const char* name) {
    using namespace Bb::Checkpoint;
    const struct {const char* name;std::uintptr_t target;} functions[]{
#define FN(n,f) {n,reinterpret_cast<std::uintptr_t>(&f)}
        FN("6ULAa0fq4jA#p#J",Init),FN("BB+kb08Tl9A#p#J",Destroy),FN("Ox9i0c7L5w0#p#J",Read),FN("mqdNorrB+gI#p#J",Write),
        FN("XD3mDeybCnk#p#J",TryRead),FN("bIHoZCTomsI#p#J",TryWrite),FN("+L98PIbGttk#p#J",Unlock),
        FN("iPtZRWICjrM#p#J",TimedRead),FN("adh--6nIqTk#p#J",TimedWrite)
#undef FN
    };
    for(const auto& f:functions)if(!std::strcmp(name,f.name))return f.target;return 0;
}
extern "C" int bb_checkpoint_rwlock_replayable(const char* name) {
    const char* functions[]{"Ox9i0c7L5w0#p#J","mqdNorrB+gI#p#J","iPtZRWICjrM#p#J","adh--6nIqTk#p#J"};
    for(auto* f:functions)if(!std::strcmp(name,f))return 1;return 0;
}
