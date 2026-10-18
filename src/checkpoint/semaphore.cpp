// SPDX-License-Identifier: GPL-2.0-or-later
#include "semaphore.h"
#include <algorithm>
#include <cassert>
#include <bit>
#include <cstring>
#include <limits>
#include <set>

namespace Bb::Checkpoint {
namespace {
constexpr std::int32_t Error(unsigned n) {return static_cast<std::int32_t>(0x80020000u|n);}
constexpr std::size_t MaxObjects=65536,MaxWaiters=65536;
}
Semaphores::~Semaphores() {std::lock_guard lock(mutex);assert(!active_callers && !paused);}
void Semaphores::AwaitThaw(std::unique_lock<std::mutex>& lock) {changed.wait(lock,[&]{return !paused;});}
std::int32_t Semaphores::Create(std::uint32_t* id,std::uint32_t attributes,std::int32_t initial,std::int32_t maximum) {
    if(!id || attributes>2 || maximum<=0 || initial<0 || initial>maximum)return Error(22);
    std::unique_lock lock(mutex);AwaitThaw(lock);
    if(objects.size()>=MaxObjects)return Error(12);
    if(next_id==UINT32_MAX)return Error(28);
    const auto created=next_id++;
    objects.emplace(created,Object{created,attributes,initial,initial,maximum,false});*id=created;return 0;
}
Semaphores::Result Semaphores::Wait(std::uint64_t thread,std::uint32_t id,std::int32_t need,std::optional<std::uint32_t> timeout,bool detachable) {
    std::unique_lock lock(mutex);AwaitThaw(lock);
    auto object=objects.find(id);
    if(!thread || requests.contains(thread))return {Error(22)};
    if(object==objects.end() || object->second.deleted)return {Error(3)};
    auto& semaphore=object->second;
    if(need<=0 || need>semaphore.maximum)return {Error(22)};
    if(semaphore.count>=need){semaphore.count-=need;return {0,timeout.value_or(0)};}
    if(timeout && !*timeout)return {Error(60)};
    if(requests.size()>=MaxWaiters || next_sequence==UINT64_MAX)return {Error(12)};
    Request request;request.state={thread,next_sequence++,id,need,0,false,timeout.has_value(),0};
    if(timeout)request.deadline=Clock::now()+std::chrono::microseconds(*timeout);
    request.attached=true;request.detachable=detachable;requests.emplace(thread,request);++active_callers;
    return Finish(lock,thread);
}
Semaphores::Result Semaphores::Resume(std::uint64_t thread,bool detachable) {
    std::unique_lock lock(mutex);AwaitThaw(lock);
    auto found=requests.find(thread);
    if(found==requests.end() || found->second.attached)return {Error(22)};
    found->second.attached=true;found->second.detachable=detachable;++active_callers;return Finish(lock,thread);
}
Semaphores::Result Semaphores::Finish(std::unique_lock<std::mutex>& lock,std::uint64_t thread) {
    auto& request=requests.at(thread);
    for(;;) {
        AwaitThaw(lock);
        if(interrupted && request.detachable) {
            request.attached=false;--active_callers;return {0,0,true};
        }
        if(request.state.done)break;
        if(request.state.timed) {
            if(Clock::now()>=request.deadline){request.state.done=true;request.state.result=Error(60);break;}
            changed.wait_until(lock,request.deadline);
        } else changed.wait(lock);
    }
    Result result{request.state.result,0};
    const auto now=Clock::now();
    if(!result.value && request.state.timed && now<request.deadline)
        result.remaining_us=static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(request.deadline-now).count());
    const auto id=request.state.semaphore;requests.erase(thread);--active_callers;Cleanup(id);return result;
}
std::int32_t Semaphores::Poll(std::uint32_t id,std::int32_t need) {
    std::unique_lock lock(mutex);AwaitThaw(lock);auto found=objects.find(id);
    if(found==objects.end() || found->second.deleted)return Error(3);
    auto& semaphore=found->second;
    if(need<=0 || need>semaphore.maximum)return Error(22);
    if(semaphore.count<need)return Error(16);
    semaphore.count-=need;return 0;
}
std::int32_t Semaphores::Signal(std::uint32_t id,std::int32_t count) {
    std::unique_lock lock(mutex);AwaitThaw(lock);auto found=objects.find(id);
    if(found==objects.end() || found->second.deleted)return Error(3);
    auto& semaphore=found->second;
    if(count<=0 || count>semaphore.maximum-semaphore.count)return Error(22);
    semaphore.count+=count;
    std::vector<Request*> queue;
    for(auto& [thread,request]:requests) {
        (void)thread;
        if(request.state.semaphore==id && !request.state.done)queue.push_back(&request);
    }
    std::sort(queue.begin(),queue.end(),[](const Request* a,const Request* b){return a->state.sequence<b->state.sequence;});
    // Match the existing runtime: serve satisfiable waiters in FIFO order,
    // allowing a smaller request to pass an unsatisfied larger request.
    for(auto* request:queue)if(request->state.need<=semaphore.count) {
        semaphore.count-=request->state.need;request->state.done=true;request->state.result=0;
    }
    changed.notify_all();return 0;
}
std::int32_t Semaphores::Cancel(std::uint32_t id,std::int32_t count,std::int32_t* waiters) {
    std::unique_lock lock(mutex);AwaitThaw(lock);auto found=objects.find(id);
    if(found==objects.end() || found->second.deleted)return Error(3);
    auto& semaphore=found->second;if(count>semaphore.maximum)return Error(22);
    std::int32_t canceled=0;
    for(auto& [thread,request]:requests) {
        (void)thread;
        if(request.state.semaphore==id && !request.state.done) {
            request.state.done=true;request.state.result=Error(85);++canceled;
        }
    }
    semaphore.count=count<0 ? semaphore.initial : count;if(waiters)*waiters=canceled;
    changed.notify_all();return 0;
}
std::int32_t Semaphores::Delete(std::uint32_t id) {
    std::unique_lock lock(mutex);AwaitThaw(lock);auto found=objects.find(id);
    if(found==objects.end() || found->second.deleted)return Error(3);
    found->second.deleted=true;
    for(auto& [thread,request]:requests) {
        (void)thread;
        if(request.state.semaphore==id && !request.state.done){request.state.done=true;request.state.result=Error(13);}
    }
    Cleanup(id);changed.notify_all();return 0;
}
void Semaphores::Cleanup(std::uint32_t id) {
    const auto found=objects.find(id);
    if(found==objects.end() || !found->second.deleted)return;
    for(const auto& [thread,request]:requests){(void)thread;if(request.state.semaphore==id)return;}
    objects.erase(found);
}
bool Semaphores::Pause(Image& image,std::string& error) {
    std::lock_guard lock(mutex);
    if(paused){error="Semaphore checkpoint is already paused.";return false;}
    // Construct the copy before changing the pause state, so allocation failure
    // cannot leave callers blocked. No native synchronization bytes are copied.
    const auto now=Clock::now();Image copy;copy.next_id=next_id;copy.next_sequence=next_sequence;
    for(const auto& [id,object]:objects){(void)id;copy.objects.push_back(object);}
    for(const auto& [thread,request]:requests) {
        (void)thread;auto waiter=request.state;
        waiter.remaining_ns=waiter.timed && request.deadline>now
            ? static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(request.deadline-now).count()) : 0;
        copy.waiters.push_back(waiter);
    }
    image=std::move(copy);paused_at=now;paused=true;changed.notify_all();error.clear();return true;
}
void Semaphores::Thaw() {
    std::lock_guard lock(mutex);if(!paused)return;
    const auto elapsed=Clock::now()-paused_at;
    for(auto& [thread,request]:requests){(void)thread;if(request.state.timed)request.deadline+=elapsed;}
    paused=false;changed.notify_all();
}
bool Semaphores::Validate(const Image& image,std::string& error) {
    const auto fail=[&](const char* message){error=message;return false;};
    if(!image.next_id || !image.next_sequence || image.objects.size()>MaxObjects || image.waiters.size()>MaxWaiters)
        return fail("Invalid semaphore checkpoint counts.");
    std::map<std::uint32_t,Object> objects;
    for(const auto& object:image.objects) {
        if(!object.id || object.id>=image.next_id || !objects.emplace(object.id,object).second ||
           object.attributes>2 || object.maximum<=0 || object.initial<0 ||
           object.initial>object.maximum || object.count<0 || object.count>object.maximum)
            return fail("Invalid semaphore checkpoint object.");
    }
    std::set<std::uint64_t> threads,sequences;
    for(const auto& waiter:image.waiters) {
        const auto object=objects.find(waiter.semaphore);
        if(!waiter.thread || !waiter.sequence || waiter.sequence>=image.next_sequence ||
           !threads.insert(waiter.thread).second || !sequences.insert(waiter.sequence).second ||
           object==objects.end() || waiter.need<=0 || waiter.need>object->second.maximum ||
           (!waiter.done && (waiter.result || object->second.deleted || waiter.need<=object->second.count)) ||
           (waiter.done && waiter.result!=0 && waiter.result!=Error(13) && waiter.result!=Error(60) && waiter.result!=Error(85)) ||
           (!waiter.timed && (waiter.remaining_ns || waiter.result==Error(60))) ||
           (waiter.result==Error(13) && !object->second.deleted) || waiter.remaining_ns>UINT64_C(4294967295000))
            return fail("Invalid semaphore checkpoint waiter.");
    }
    return true;
}
bool Semaphores::Restore(const Image& image,std::string& error) {
    if(!Validate(image,error))return false;
    std::map<std::uint32_t,Object> restored_objects;
    std::map<std::uint64_t,Request> restored_requests;
    const auto now=Clock::now();
    for(const auto& object:image.objects)restored_objects.emplace(object.id,object);
    for(const auto& waiter:image.waiters)restored_requests.emplace(waiter.thread,Request{
        waiter,now+std::chrono::nanoseconds(waiter.remaining_ns),false});
    std::lock_guard lock(mutex);
    if(paused || active_callers || !objects.empty() || !requests.empty()) {
        error="Semaphore restore requires a fresh kernel.";return false;
    }
    objects=std::move(restored_objects);requests=std::move(restored_requests);
    next_id=image.next_id;next_sequence=image.next_sequence;
    paused_at=now;paused=true;error.clear();return true;
}
std::size_t Semaphores::Pending() const {std::lock_guard lock(mutex);return requests.size();}
void Semaphores::Interrupt(bool value) {std::lock_guard lock(mutex);interrupted=value;changed.notify_all();}
bool Semaphores::HasWait(std::uint64_t thread) const {std::lock_guard lock(mutex);return requests.contains(thread);}
std::size_t Semaphores::Pending(std::uint32_t id) const {
    std::lock_guard lock(mutex);std::size_t n=0;
    for(const auto& [thread,r]:requests){(void)thread;if(r.state.semaphore==id && !r.state.done)++n;}
    return n;
}
bool Semaphores::Encode(const Image& image,std::vector<std::uint8_t>& out,std::string& error) {
    if(!Validate(image,error))return false;
    std::vector<std::uint8_t> bytes{'B','B','S','E','M','A','1',0};
    const auto put=[&](std::uint64_t n,unsigned count=4){for(unsigned k=0;k<count;++k)bytes.push_back(std::uint8_t(n>>(k*8)));};
    put(image.next_id);put(0);put(image.next_sequence,8);put(image.objects.size());put(image.waiters.size());
    for(const auto& object:image.objects) {
        put(object.id);put(object.attributes);put(std::uint32_t(object.count));
        put(std::uint32_t(object.initial));put(std::uint32_t(object.maximum));put(object.deleted);
    }
    for(const auto& waiter:image.waiters) {
        put(waiter.thread,8);put(waiter.sequence,8);put(waiter.semaphore);put(std::uint32_t(waiter.need));
        put(std::uint32_t(waiter.result));put(unsigned(waiter.done)|(unsigned(waiter.timed)<<1));
        put(waiter.remaining_ns,8);put(0,8);
    }
    out=std::move(bytes);error.clear();return true;
}
bool Semaphores::Decode(std::span<const std::uint8_t> bytes,Image& out,std::string& error) {
    const auto fail=[&](const char* message){error=message;return false;};
    if(bytes.size()<32 || std::memcmp(bytes.data(),"BBSEMA1\0",8))return fail("Invalid semaphore checkpoint format.");
    std::size_t at=8;
    const auto get=[&](unsigned count=4){
        std::uint64_t n=0;for(unsigned k=0;k<count;++k)n|=std::uint64_t(bytes[at++])<<(k*8);return n;
    };
    Image image;image.next_id=std::uint32_t(get());if(get())return fail("Invalid semaphore checkpoint header.");
    image.next_sequence=get(8);const auto objects=get(),waiters=get();
    if(objects>MaxObjects || waiters>MaxWaiters || 32+objects*24+waiters*48!=bytes.size())
        return fail("Invalid semaphore checkpoint sizes.");
    const auto signed32=[&]{return std::bit_cast<std::int32_t>(std::uint32_t(get()));};
    for(std::size_t k=0;k<objects;++k) {
        Object object;object.id=std::uint32_t(get());object.attributes=std::uint32_t(get());
        object.count=signed32();object.initial=signed32();object.maximum=signed32();
        const auto deleted=get();if(deleted>1)return fail("Invalid semaphore deletion flag.");
        object.deleted=deleted!=0;image.objects.push_back(object);
    }
    for(std::size_t k=0;k<waiters;++k) {
        Waiter waiter;waiter.thread=get(8);waiter.sequence=get(8);waiter.semaphore=std::uint32_t(get());
        waiter.need=signed32();waiter.result=signed32();const auto flags=get();
        if(flags&~3ull)return fail("Invalid semaphore waiter flags.");
        waiter.done=flags&1;waiter.timed=flags&2;waiter.remaining_ns=get(8);
        if(get(8))return fail("Invalid semaphore waiter padding.");
        image.waiters.push_back(waiter);
    }
    if(!Validate(image,error))return false;
    out=std::move(image);error.clear();return true;
}
}
