// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
struct SDL_Window;
union SDL_Event;
namespace MacAchievements {
extern std::atomic<bool> CapturesInput;
class Browser {
public:
    Browser(SDL_Window* game,const std::filesystem::path& game_root);
    ~Browser();
    void Show();
    bool HandleEvent(const SDL_Event& event);
    void Tick(std::uint64_t milliseconds);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
