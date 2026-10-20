// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>

namespace Gpu::Metal::CommandTimeline {
struct Submission {
    std::uint64_t id,frame,vertex,fragment,compute;
    double created,committed;
    bool waited;
};
// Observes existing submissions only. It never cuts, waits for, or encodes GPU work.
// Completion blocks retain the log so asynchronous callbacks cannot outlive it.
class Log {
public:
    explicit Log(std::FILE* file):file(file) {
        std::fputs("submission,frame,created_s,commit_s,scheduled_s,gpu_start_s,gpu_end_s,completed_s,wait_requested,vs,ps,cs,status,error_code\n",file);
        std::fflush(file);
    }
    ~Log() {std::fclose(file);}
    void Complete(const Submission& s,double scheduled,double gpu_start,double gpu_end,
                  double completed,long status,long error) {
        std::scoped_lock guard(mutex);
        std::fprintf(file,"%llu,%llu,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%u,%#llx,%#llx,%#llx,%ld,%ld\n",
            (unsigned long long)s.id,(unsigned long long)s.frame,s.created,s.committed,scheduled,
            gpu_start,gpu_end,completed,unsigned(s.waited),(unsigned long long)s.vertex,
            (unsigned long long)s.fragment,(unsigned long long)s.compute,status,error);
        std::fflush(file);
    }
private:
    std::FILE* file;
    std::mutex mutex;
};
inline std::shared_ptr<Log> Open() {
    const char* path=std::getenv("BB_METAL_COMMAND_TIMELINE_CSV");
    if(!path || !*path)return {};
    std::error_code error;
    const auto destination=std::filesystem::weakly_canonical(path,error);
    if(error)return {};
    for(const char* key:{"BB_SOURCE_GAME","BB_GAME_DIR"}) {
        const char* value=std::getenv(key);if(!value || !*value)continue;
        const auto source=std::filesystem::weakly_canonical(value,error);if(error)return {};
        const auto relative=destination.lexically_relative(source);
        if(destination==source || (!relative.empty() && *relative.begin()!=".."))return {};
    }
    const int fd=::open(destination.c_str(),O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)return {};
    auto* file=::fdopen(fd,"a");if(!file){::close(fd);return {};}
    return std::make_shared<Log>(file);
}
} // namespace Gpu::Metal::CommandTimeline
