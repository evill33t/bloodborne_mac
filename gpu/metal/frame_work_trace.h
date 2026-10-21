// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <fcntl.h>
#include <unistd.h>

namespace Gpu::Metal::FrameWork {
// Optional inclusive CPU-stage sums. No GPU cuts, waits or pixel copies.
// The renderer lock protects State. Bind routes scopes from every CP ring
// into the same renderer-owned frame, without changing their execution.
inline constexpr std::array Names{
    "draw_record", "dispatch_record", "resource_resolve", "descriptor_resolve",
    "texture_resolve", "texture_change_check", "texture_upload",
    "texture_gpu_upload_encode", "texture_mip_upload", "texture_detile",
    "texture_replace_region", "texture_hash_initial", "memory_map_refresh",
    "texture_gpu_source_copy", "texture_gpu_snapshot_hash",
    "texture_gpu_compressed_upload",
    "texture_gpu_snapshot_allocate", "texture_gpu_scratch_allocate",
    "texture_gpu_compute_encode", "texture_gpu_blit_encode", "texture_gpu_job_metadata",
    "render_pass_begin", "render_pass_end", "render_encoder_create",
    "shader_decode", "shader_msl", "pipeline_wait", "texture_collection",
    "frame_handoff", "submit_record", "dma_record",
    "draw_pipeline_bind", "draw_fixed_bind", "draw_bind_resources",
    "draw_vertex_texture_bind", "draw_fragment_texture_bind", "draw_encode",
    "draw_direct_ps_root", "draw_direct_vs_root", "tessellation_pipeline"};
class Log {
    std::FILE* file=nullptr;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::string> pending;
    std::jthread writer;
    unsigned dropped=0;
public:
    Log() {
        const char* path=std::getenv("BB_METAL_FRAME_WORK_CSV");
        if(!path || !*path)return;
        std::error_code error;
        const auto destination=std::filesystem::weakly_canonical(path,error);
        if(error)return;
        for(const char* key:{"BB_SOURCE_GAME","BB_GAME_DIR"}) {
            const char* value=std::getenv(key);if(!value || !*value)continue;
            const auto source=std::filesystem::weakly_canonical(value,error);if(error)return;
            const auto relative=destination.lexically_relative(source);
            if(destination==source || (!relative.empty() && *relative.begin()!=".."))return;
        }
        const int fd=::open(destination.c_str(),O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC|O_NOFOLLOW,0600);
        if(fd<0)return;
        file=::fdopen(fd,"a");if(!file){::close(fd);return;}
        std::fputs("frame,stage,begin_s,end_s,calls,inclusive_ms,covered_ms,max_ms,max_a,max_b\n",file);std::fflush(file);
        writer=std::jthread([this](std::stop_token stop) {
            for(;;) {
                std::string line;
                {
                    std::unique_lock lock(mutex);
                    changed.wait(lock,[&]{return stop.stop_requested() || !pending.empty();});
                    if(pending.empty())return;
                    line=std::move(pending.front());pending.pop_front();
                }
                std::fputs(line.c_str(),file);std::fflush(file);
            }
        });
    }
    ~Log() {
        if(writer.joinable()){writer.request_stop();changed.notify_one();writer.join();}
        if(file)std::fclose(file);
        if(dropped)std::fprintf(stderr,"Frame-work diagnostic dropped %u records\n",dropped);
    }
    bool Enabled() const {return file!=nullptr;}
    void Push(std::string line) {
        {std::scoped_lock lock(mutex);if(pending.size()==4096){pending.pop_front();++dropped;}pending.push_back(std::move(line));}
        changed.notify_one();
    }
};
inline Log& Output(){static Log output;return output;}
inline bool Enabled(){return Output().Enabled();}
inline constexpr std::uint64_t Hash(std::string_view s) {
    std::uint64_t h=14695981039346656037ull;for(char c:s){h^=static_cast<unsigned char>(c);h*=1099511628211ull;}return h;
}
inline constexpr auto Hashes=[] {
    std::array<std::uint64_t,Names.size()> hashes{};
    for(std::size_t i=0;i<Names.size();++i)hashes[i]=Hash(Names[i]);
    return hashes;
}();
inline int Slot(const char* stage) {
    if(!Enabled())return -1;
    const auto hash=Hash(stage);
    for(std::size_t i=0;i<Names.size();++i)if(hash==Hashes[i])return static_cast<int>(i);
    return -1;
}
struct Sample {
    std::uint64_t calls=0,a=0,b=0;
    double begin=0,end=0,total=0,covered=0,maximum=0,outer_begin=0;
    unsigned depth=0;
};
struct State {
    std::uint64_t frame=0;
    double begin=0;
    bool active=false;
    std::array<Sample,Names.size()> stages{};
};
inline thread_local State* current=nullptr;
class Bind {
    State* previous=current;
public:
    explicit Bind(State& state){if(Enabled())current=&state;}
    ~Bind(){current=previous;}
};
inline void Write(std::uint64_t frame,const char* stage,const Sample& sample) {
    if(!Enabled())return;
    char row[512];
    std::snprintf(row,sizeof(row),"%llu,%s,%.9f,%.9f,%llu,%.6f,%.6f,%.6f,%llu,%llu\n",
        static_cast<unsigned long long>(frame),stage,sample.begin,sample.end,
        static_cast<unsigned long long>(sample.calls),sample.total*1000,sample.covered*1000,sample.maximum*1000,
        static_cast<unsigned long long>(sample.a),static_cast<unsigned long long>(sample.b));
    Output().Push(row);
}
inline void Span(std::uint64_t frame,const char* stage,double begin,double end,
                 std::uint64_t a=0,std::uint64_t b=0) {
    if(begin && end>=begin)Write(frame,stage,Sample{1,a,b,begin,end,end-begin,end-begin,end-begin});
}
inline void Enter(int slot,std::uint64_t frame,double begin) {
    if(slot<0 || !current || !current->active)return;
    if(frame && current->frame!=frame)return;
    auto& sample=current->stages[slot];
    if(!sample.depth++)sample.outer_begin=begin;
}
inline void Add(int slot,std::uint64_t frame,std::uint64_t a,std::uint64_t b,double begin,double end) {
    if(slot<0 || !current)return;
    auto& state=*current;
    if(!state.active || (frame && state.frame!=frame)) {
        if(std::string_view(Names[slot])=="frame_handoff")Span(frame,Names[slot],begin,end);
        return;
    }
    auto& sample=state.stages[slot];
    if(sample.depth && !--sample.depth)sample.covered+=end-sample.outer_begin;
    if(!sample.calls)sample.begin=begin;
    sample.begin=std::min(sample.begin,begin);
    ++sample.calls;sample.end=std::max(sample.end,end);sample.total+=end-begin;
    if(end-begin>sample.maximum){sample.maximum=end-begin;sample.a=a;sample.b=b;}
}
inline void Begin(State& state,std::uint64_t frame,double now) {
    if(!Enabled())return;
    state=State{};state.frame=frame;state.begin=now;state.active=true;
}
inline void End(State& state,std::uint64_t frame,double now) {
    if(!Enabled() || !state.active || state.frame!=frame)return;
    Span(frame,"cpu_frame_wall",state.begin,now);
    for(std::size_t i=0;i<Names.size();++i)if(state.stages[i].calls)Write(frame,Names[i],state.stages[i]);
    state.active=false;
}
}
