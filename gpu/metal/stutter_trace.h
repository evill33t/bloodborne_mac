// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <mach/mach_time.h>

namespace Gpu::Metal::StutterTrace {
inline double Now() {
    static const double scale=[] {mach_timebase_info_data_t time;mach_timebase_info(&time);return 1e-9*double(time.numer)/time.denom;}();
    return mach_absolute_time()*scale; // same epoch as Metal presentation times
}
inline std::FILE* Output() {
    static std::FILE* output=[]() -> std::FILE* {
        const char* path=std::getenv("BB_METAL_STUTTER_TRACE");
        if (!path || !*path) return nullptr;
        std::error_code error;
        const auto destination=std::filesystem::weakly_canonical(path,error);
        if (error) return nullptr;
        for (const char* key:{"BB_SOURCE_GAME","BB_GAME_DIR"}) {
            const char* value=std::getenv(key);if (!value || !*value) continue;
            const auto source=std::filesystem::weakly_canonical(value,error);if (error) return nullptr;
            const auto relative=destination.lexically_relative(source);
            if (destination==source || (!relative.empty() && *relative.begin()!="..")) return nullptr;
        }
        const int fd=::open(destination.c_str(),O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC|O_NOFOLLOW,0600);
        if (fd<0) return nullptr;
        auto* file=::fdopen(fd,"a");if (!file) {::close(fd);return nullptr;}
        std::fprintf(file,"stage,start_s,end_s,ms,frame,a,b\n");std::fflush(file);
        return file;
    }();
    return output;
}
inline void Record(const char* stage,double start,double end,std::uint64_t frame=0,
                   std::uint64_t a=0,std::uint64_t b=0,double minimum_ms=10) {
    if ((end-start)*1000<minimum_ms) return;
    auto* file=Output();if (!file) return;
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    std::fprintf(file,"%s,%.9f,%.9f,%.3f,%llu,%llu,%llu\n",stage,start,end,(end-start)*1000,
        (unsigned long long)frame,(unsigned long long)a,(unsigned long long)b);
    std::fflush(file);
}
// Opt-in CPU timing only; does not serialize GPU command buffers or read pixels.
class Scope {
public:
    Scope(const char* stage,std::uint64_t frame=0,std::uint64_t a=0,std::uint64_t b=0,double minimum_ms=10)
        :stage(stage),frame(frame),a(a),b(b),minimum_ms(minimum_ms),start(Output()?Now():0) {}
    ~Scope() { End(); }
    void End() {if (start) {Record(stage,start,Now(),frame,a,b,minimum_ms);start=0;}}
private:
    const char* stage;
    std::uint64_t frame,a,b;
    double minimum_ms,start;
};
}
