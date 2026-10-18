// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <memory>
struct SDL_Window;
union SDL_Event;

namespace MacInput {
extern std::atomic<bool> CapturesInput;
// SDL and Cocoa operations stay on the window thread. The bbgpu entry points
// exchange requests and accumulated input with the guest's pad/IME threads.
class Bridge {
public:
    explicit Bridge(SDL_Window* window);
    ~Bridge();
    bool HandleEvent(const SDL_Event& event);
    void Tick(bool other_ui_captures_input);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
