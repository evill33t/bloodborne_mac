// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <condition_variable>
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
        lock.unlock();
        wake();
        lock.lock();
        consumed.wait(lock,[&] { return frame==nullptr; });
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
};
}
