// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <system_error>

namespace Gpu::Metal::PipelineRecipeIdentity {
inline constexpr std::size_t LegacyDirectories=32;
inline constexpr std::size_t InventoryEntries=4096;
inline constexpr const char* Architecture() {
#if defined(__aarch64__)
    return "arm64";
#elif defined(__x86_64__)
    return "x86_64";
#else
    return "unknown";
#endif
}
inline constexpr std::string_view AlternateArchitecture(std::string_view architecture) {
    return architecture=="arm64" ? "x86_64" : architecture=="x86_64" ? "arm64" : "";
}
inline std::string OsVersion(std::uint64_t major,std::uint64_t minor,std::uint64_t patch) {
    return std::to_string(major)+"."+std::to_string(minor)+"."+std::to_string(patch);
}
// Inputs for a SHA-256 directory key. Device registry IDs are deliberately not
// inputs: they identify a boot's IORegistry entry, not persistent source recipes.
inline std::string Material(std::string_view name,std::string_view architecture,
                            std::uint64_t major,std::uint64_t minor,std::uint64_t patch) {
    std::string result="bloodborne-metal-source-recipes-v2";
    const auto os=OsVersion(major,minor,patch);
    for(const auto part:{name,architecture,std::string_view(os)}) {
        result+=':';result+=std::to_string(part.size());result+=':';result.append(part);
    }
    return result;
}
inline bool LegacyDirectory(std::string_view name,std::string_view os) {
    const auto dash=name.find('-');
    if(dash==std::string_view::npos || !dash || dash>16 || name.substr(dash+1)!=os)return false;
    for(const char c:name.substr(0,dash))if(!((c>='0' && c<='9') || (c>='a' && c<='f')))return false;
    return true;
}
// The data root may itself have been a user-selected alias; callers resolve it
// first. Every existing cache component below it must be an ordinary directory.
inline bool PlainDirectories(const std::filesystem::path& data,const std::filesystem::path& target,
                             bool allow_missing) {
    const auto base=data.lexically_normal(),destination=target.lexically_normal();
    const auto relative=destination.lexically_relative(base);
    if(relative.empty() || relative.is_absolute())return false;
    for(const auto& part:relative)if(part=="..")return false;
    std::error_code error;
    auto current=base;
    const auto plain=[&](const std::filesystem::path& path) {
        const auto status=std::filesystem::symlink_status(path,error);
        return !error && std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status);
    };
    if(!plain(current))return false;
    for(const auto& part:relative) {
        if(part==".")continue;
        current/=part;
        const auto status=std::filesystem::symlink_status(current,error);
        if(error==std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status)))return allow_missing;
        if(error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status))return false;
    }
    return true;
}
} // namespace Gpu::Metal::PipelineRecipeIdentity
