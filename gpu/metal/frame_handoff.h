// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

namespace Gpu::Metal {
// One producer and one UI consumer. Hold the producer at the frame boundary
// until presentation has been queued, so later rendering cannot overtake it
// on the shared Metal command queue or overwrite the frame being presented.
// Frame generation may acknowledge earlier after retaining separately owned,
// already committed snapshots; its event protects later presentation reads.
class FrameHandoff {
public:
    template<class Wake> void PublishAndWait(void* texture,Wake wake) {
        std::unique_lock lock(mutex);
        frame=texture;
        auto notification=notifier;
        lock.unlock();
        if(notification)(*notification)();
        wake();
        lock.lock();
        consumed.wait(lock,[&] { return frame==nullptr; });
    }
    // The notification only wakes the consumer. It never acknowledges or
    // releases producer storage. Its captured state must survive the consumer.
    void SetNotifier(std::function<void()> notify) {
        auto next=notify ? std::make_shared<const std::function<void()>>(std::move(notify)) : nullptr;
        bool pending=false;
        {std::scoped_lock lock(mutex);notifier=next;pending=frame!=nullptr;}
        if(pending && next)(*next)();
    }
    void* Pending() {
        std::scoped_lock lock(mutex);
        return frame;
    }
    void Acknowledge() {
        { std::scoped_lock lock(mutex);frame=nullptr; }
        consumed.notify_one();
    }
private:
    std::mutex mutex;
    std::condition_variable consumed;
    void* frame=nullptr;
    std::shared_ptr<const std::function<void()>> notifier;
};
}
