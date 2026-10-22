// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <charconv>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace Gpu::Metal::CommandBudget {
inline constexpr std::uint32_t WorkLimit=400;
// Disabled unless explicitly requested. Keep very short budgets out of normal
// use: extra command buffers also carry a CPU/driver scheduling cost.
inline std::uint32_t Parse(const char* value) {
    if(!value || !*value)return 0;
    const std::string_view text=value;
    std::uint32_t result=0;
    const auto read=std::from_chars(text.data(),text.data()+text.size(),result);
    if(read.ec!=std::errc{} || read.ptr!=text.data()+text.size())return 0;
    return result>=250 && result<=16000 ? result : 0;
}
enum class Reason {None,WorkLimit,ElapsedTime};
// Called ONLY after a naturally required encoder end. It must never end an
// otherwise reusable render pass just to meet a time budget.
inline Reason Due(std::uint32_t work,std::uint32_t budget_us,double created,double now) {
    if(work>=WorkLimit)return Reason::WorkLimit;
    if(!work || !budget_us || !std::isfinite(created) || !std::isfinite(now) ||
       created<=0 || now<created)return Reason::None;
    return (now-created)*1e6>=budget_us ? Reason::ElapsedTime : Reason::None;
}
}
