// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Bb::Checkpoint {
// Logical semaphore state, independent of native wait stacks/condition-variable
// storage. Thread IDs are guest identities supplied by the runtime, not pthreads.
class Semaphores {
public:
    struct Object {
        std::uint32_t id=0,attributes=0;
        std::int32_t count=0,initial=0,maximum=0;
        bool deleted=false;
        bool operator==(const Object&) const=default;
    };
    struct Waiter {
        std::uint64_t thread=0,sequence=0;
        std::uint32_t semaphore=0;
        std::int32_t need=0,result=0;
        bool done=false,timed=false;
        std::uint64_t remaining_ns=0;
        bool operator==(const Waiter&) const=default;
    };
    struct Image {
        std::uint32_t next_id=1;
        std::uint64_t next_sequence=1;
        std::vector<Object> objects;
        std::vector<Waiter> waiters;
        bool operator==(const Image&) const=default;
    };
    struct Result {std::int32_t value=0;std::uint32_t remaining_us=0;bool interrupted=false;};
    ~Semaphores(); // Owner must join all callers before destroying the kernel.
    std::int32_t Create(std::uint32_t* id,std::uint32_t attributes,std::int32_t initial,std::int32_t maximum);
    Result Wait(std::uint64_t thread,std::uint32_t id,std::int32_t need,std::optional<std::uint32_t> timeout_us,bool detachable=false);
    Result Resume(std::uint64_t thread,bool detachable=false); // Attach a fresh native thread to a restored wait.
    // Detach the native caller while retaining the logical request, including
    // an already granted result. The import adapter must replay at entry.
    void Interrupt(bool requested);
    bool HasWait(std::uint64_t thread) const;
    std::int32_t Poll(std::uint32_t id,std::int32_t need);
    std::int32_t Signal(std::uint32_t id,std::int32_t count);
    std::int32_t Cancel(std::uint32_t id,std::int32_t count,std::int32_t* waiters=nullptr);
    std::int32_t Delete(std::uint32_t id);
    // Pause prevents host timeouts/signals from mutating the exported state.
    // Thaw shifts deadlines by the paused interval. Call Thaw on every exit.
    bool Pause(Image& image,std::string& error);
    void Thaw();
    // Fresh, empty kernel only. Restored deadlines remain paused until Thaw,
    // allowing the runtime to finish rebuilding its other resources first.
    bool Restore(const Image& image,std::string& error);
    static bool Encode(const Image&,std::vector<std::uint8_t>&,std::string& error);
    static bool Decode(std::span<const std::uint8_t>,Image&,std::string& error);
    std::size_t Pending() const;
    std::size_t Pending(std::uint32_t semaphore) const;
private:
    using Clock=std::chrono::steady_clock;
    struct Request {Waiter state;Clock::time_point deadline{};bool attached=false,detachable=false;};
    void AwaitThaw(std::unique_lock<std::mutex>& lock);
    Result Finish(std::unique_lock<std::mutex>& lock,std::uint64_t thread);
    void Cleanup(std::uint32_t id);
    static bool Validate(const Image& image,std::string& error);
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::map<std::uint32_t,Object> objects;
    std::map<std::uint64_t,Request> requests;
    std::uint32_t next_id=1;
    std::uint64_t next_sequence=1;
    std::size_t active_callers=0;
    bool paused=false;
    bool interrupted=false;
    Clock::time_point paused_at{};
};
}
