// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "game_settings.h"
#include <atomic>
#include <functional>
#include <memory>
struct SDL_Window;
union SDL_Event;
namespace MacSettings {
extern std::atomic<bool> CapturesInput;
class Overlay {
public:
    Overlay(SDL_Window* window,Values& settings,const std::filesystem::path& path,
            std::function<void()> apply,std::function<void()> achievements = {});
    ~Overlay();
    bool HandleEvent(const SDL_Event& event);
    void Tick();
    void UpdateStatus(double fps,int width,int height,bool metalfx_supported,const char* scaling);
    void Toggle();
private:
    void Shortcut();
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
