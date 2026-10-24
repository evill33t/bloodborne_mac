#include "gpu/liverpool/command_processor.h"

#include <atomic>
#include <array>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>

#include <pthread.h>
#include "gpu/metal/guest_clock.h"

#include "common/log.h"
#include "core/memory.h"
#include "gpu/renderer.h"
#include "gpu/metal/command_validation.h"
#include "gpu/metal/guest_label.h"
#include "gpu/metal/stutter_trace.h"
#include "gpu/metal/render_thread_qos.h"
#include "gpu/metal/bbhost/fog_draw_capture.h"

namespace Gpu::Liverpool {

namespace {

// Standalone CP spans also cover waits outside the renderer's frame lock.
class CpTiming {
    const char* stage;
    u64 ring, detail;
    double start;
public:
    CpTiming(const char* stage,u64 ring,u64 detail=0):stage(stage),ring(ring),detail(detail),
        start(Metal::FrameWork::Enabled()?Metal::StutterTrace::Now():0) {}
    ~CpTiming() {
        if(start)Metal::FrameWork::Span(0,stage,start,Metal::StutterTrace::Now(),ring,detail);
    }
};

// EVENT_WRITE_EOP / RELEASE_MEM data and interrupt selects.
enum DataSel : u32 { kDataNone = 0, kData32 = 1, kData64 = 2, kGpuClock = 3, kPerfCounter = 4 };
// NOP payload markers the Gnm driver uses for flip requests.
constexpr u32 kNopPrepareFlipLabel = 0x68750778;
constexpr u32 kNopPrepareFlipInterrupt = 0x68750780;
constexpr u32 kNopPrepareFlipInterruptLabel = 0x68750781;

enum IntSel : u32 { kIntNone = 0, kIntOnly = 1, kIntWriteConfirm = 2, kIntUndocumented = 3 };

u64 GpuClock() {
    // The GPU clock runs at 100 MHz on PS4; derive it from the host clock.
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 10;
}

// True if [addr, addr + size) is guest memory (one mapping).
bool Mapped(const void* p, u64 size) {
    const u64 addr = reinterpret_cast<u64>(p);
    Core::Memory::Vma vma;
    return addr && Core::Memory::Query(addr, false, &vma) && vma.type != Core::Memory::VmaType::Free &&
           addr>=vma.start && addr-vma.start<=vma.size && size<=vma.size-(addr-vma.start);
}

template <typename T>
T* Ptr(u32 lo, u32 hi) {
    return reinterpret_cast<T*>(u64(lo) | (u64(hi) << 32));
}

bool Compare(u32 func, u32 value, u32 ref) {
    switch (func) {
    case 0: return true;
    case 1: return value < ref;
    case 2: return value <= ref;
    case 3: return value == ref;
    case 4: return value != ref;
    case 5: return value >= ref;
    case 6: return value > ref;
    default: return true;
    }
}

} // namespace

thread_local CommandProcessor::Ring* CommandProcessor::t_ring = nullptr;
thread_local RegFile* CommandProcessor::t_regs = nullptr;
thread_local CommandProcessor::AuditJob* CommandProcessor::t_audit_job = nullptr;
thread_local u32 CommandProcessor::t_audit_buffer = 0;

struct CommandProcessor::AuditJob {
    struct Stream {
        u64 address=0,dwords=0,submitted_ns=0,consumed_ns=0;
        bool submit_mapped=false,consume_mapped=false;
        std::vector<u32> submitted,consumed;
    };
    u64 id=0;
    std::vector<std::array<Stream,2>> buffers;
    std::vector<std::string> events;
    u64 packet_address=0,packet_offset=0;
    u32 packet_header=0;
    int packet_depth=0;
};
struct CommandProcessor::AuditState {
    // 64MiB of binary snapshots plus <=32MiB of event text and bounded
    // manifests remain below 128MiB total output for one armed run.
    static constexpr u64 ByteLimit=64ull<<20,StreamLimit=8ull<<20;
    static constexpr u64 JobLimit=200,EventLimit=65536;
    std::mutex lock;
    std::filesystem::path root;
    std::string trigger,token;
    std::chrono::steady_clock::time_point poll{};
    bool armed=false;
    u64 jobs=0,reserved_bytes=0;
    std::atomic<u64> events{0};
    std::ofstream manifest,packets;
};

namespace {
u64 AuditNow() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
u64 AuditHash(const std::vector<u32>& words) {
    u64 value=14695981039346656037ull;
    for(const u8 byte:std::span(reinterpret_cast<const u8*>(words.data()),words.size()*4))
        value=(value^byte)*1099511628211ull;
    return value;
}
std::string AuditToken(const std::string& path) {
    std::ifstream file(path,std::ios::binary);char bytes[128]{};
    file.read(bytes,sizeof(bytes));return {bytes,static_cast<std::size_t>(file.gcount())};
}
// Reject a symlink in any existing output component, and any destination in
// either original-game root. Diagnostic files never belong in a game dump.
bool AuditRoot(const std::filesystem::path& path) {
    if(!path.is_absolute()) return false;
    std::error_code error;std::filesystem::path prefix;
    for(const auto& component:path.lexically_normal()) {
        prefix/=component;
        const auto status=std::filesystem::symlink_status(prefix,error);
        if(error && error!=std::errc::no_such_file_or_directory) return false;
        error.clear();if(std::filesystem::is_symlink(status)) return false;
    }
    const auto root=std::filesystem::weakly_canonical(path,error);if(error) return false;
    for(const char* name:{"BB_SOURCE_GAME","BB_GAME_DIR"}) if(const char* value=std::getenv(name);value && *value) {
        const auto game=std::filesystem::weakly_canonical(value,error);if(error) return false;
        const auto relative=root.lexically_relative(game);
        if(root==game || (!relative.empty() && *relative.begin()!="..")) return false;
    }
    return true;
}
}

void CommandProcessor::InitAudit() {
    const char* directory=std::getenv("BB_METAL_CP_AUDIT_DIR");
    const char* trigger=std::getenv("DC_TRACE_FILE");
    if(!directory || !*directory || !trigger || !*trigger || !AuditRoot(directory)) return;
    try {
        auto state=std::make_unique<AuditState>();
        state->root=std::filesystem::path(directory).lexically_normal()/std::to_string(AuditNow());
        std::filesystem::create_directories(state->root);
        if(!AuditRoot(state->root)) return;
        state->trigger=trigger;state->token=AuditToken(trigger);
        state->manifest.open(state->root/"buffers.jsonl");state->packets.open(state->root/"packets.csv");
        if(!state->manifest || !state->packets) return;
        state->manifest<<"{\"kind\":\"cp_audit_config\",\"version\":1,\"byte_limit\":"<<AuditState::ByteLimit
            <<",\"job_limit\":"<<AuditState::JobLimit<<",\"event_limit\":"<<AuditState::EventLimit
            <<",\"hash\":\"fnv1a64\",\"execution\":\"original guest spans; diagnostic copies are never executed\"}\n";
        state->packets<<"steady_ns,job,ring,buffer,kind,constant_engine,depth,address,offset_dwords,opcode,header,predicate,count,indexed,instances,index_offset,first_vertex,vs,ps,cb0,db_count_control,db_render_control,db_depth_control,cb_target_mask,depth_base,raw_words\n";
        DC_INFO("gpu: CP audit waiting for DC_TRACE_FILE token change at %s",state->root.c_str());
        audit=std::move(state);
    } catch(const std::exception& e) {DC_WARN("gpu: CP audit setup failed: %s",e.what());}
}

std::shared_ptr<CommandProcessor::AuditJob> CommandProcessor::AuditSubmission(const std::vector<CmdBuffers>& buffers) {
    if(!audit) return {};
    std::lock_guard lock(audit->lock);
    try {
        if(!audit->armed) {
            const auto now=std::chrono::steady_clock::now();if(now<audit->poll) return {};
            audit->poll=now+std::chrono::milliseconds(100);
            const auto token=AuditToken(audit->trigger);
            if(token.empty() || token==audit->token) return {};
            audit->armed=true;
            audit->manifest<<"{\"kind\":\"armed\",\"steady_ns\":"<<AuditNow()<<"}\n";
        }
        if(audit->jobs>=AuditState::JobLimit || buffers.size()>64) return {};
        u64 reserve=0;
        for(const auto& buffer:buffers) for(const auto stream:{buffer.dcb,buffer.ccb}) {
            if(stream.size_bytes()<=AuditState::StreamLimit) reserve+=stream.size_bytes()*2;
        }
        if(reserve>AuditState::ByteLimit-audit->reserved_bytes) return {};
        auto job=std::make_shared<AuditJob>();job->id=++audit->jobs;job->buffers.resize(buffers.size());
        audit->reserved_bytes+=reserve;
        for(std::size_t n=0;n<buffers.size();++n) for(unsigned k=0;k<2;++k) {
            const auto source=k ? buffers[n].ccb : buffers[n].dcb;auto& saved=job->buffers[n][k];
            saved.address=reinterpret_cast<u64>(source.data());saved.dwords=source.size();saved.submitted_ns=AuditNow();
            if(!source.empty() && source.size_bytes()<=AuditState::StreamLimit && Mapped(source.data(),source.size_bytes())) {
                saved.submitted.assign(source.begin(),source.end());saved.submit_mapped=true;
            }
        }
        return job;
    } catch(const std::exception& e) {DC_WARN("gpu: CP submission audit stopped: %s",e.what());return {};}
}

void CommandProcessor::AuditConsume(AuditJob& job,const CmdBuffers& buffer,u32 index) {
    try {
        for(unsigned k=0;k<2;++k) {
            const auto source=k ? buffer.ccb : buffer.dcb;auto& saved=job.buffers[index][k];saved.consumed_ns=AuditNow();
            if(!source.empty() && source.size_bytes()<=AuditState::StreamLimit && Mapped(source.data(),source.size_bytes())) {
                saved.consumed.assign(source.begin(),source.end());saved.consume_mapped=true;
            }
        }
    } catch(const std::exception& e) {DC_WARN("gpu: CP consumption audit incomplete: %s",e.what());}
}

void CommandProcessor::AuditFinish(AuditJob& job) {
    std::lock_guard lock(audit->lock);
    try {
        for(std::size_t n=0;n<job.buffers.size();++n) for(unsigned k=0;k<2;++k) {
            const auto& stream=job.buffers[n][k];
            const auto stem="job_"+std::to_string(job.id)+"_buffer_"+std::to_string(n)+(k?"_ccb":"_dcb");
            bool saved=true;
            for(unsigned phase=0;phase<2;++phase) {
                const auto& words=phase ? stream.consumed : stream.submitted;if(words.empty()) continue;
                std::ofstream file(audit->root/(stem+(phase?"_consume.bin":"_submit.bin")),std::ios::binary);
                file.write(reinterpret_cast<const char*>(words.data()),words.size()*4);file.close();saved&=file.good();
            }
            u64 different=0;
            for(std::size_t i=0;i<std::min(stream.submitted.size(),stream.consumed.size());++i)
                different+=stream.submitted[i]!=stream.consumed[i];
            audit->manifest<<"{\"kind\":\"command_stream\",\"job\":"<<job.id<<",\"buffer\":"<<n
                <<",\"constant_engine\":"<<(k?"true":"false")<<",\"address\":"<<stream.address<<",\"dwords\":"<<stream.dwords
                <<",\"submit_ns\":"<<stream.submitted_ns<<",\"consume_ns\":"<<stream.consumed_ns
                <<",\"submit_captured\":"<<(stream.submit_mapped?"true":"false")<<",\"consume_captured\":"<<(stream.consume_mapped?"true":"false")
                <<",\"submit_hash\":"<<AuditHash(stream.submitted)<<",\"consume_hash\":"<<AuditHash(stream.consumed)
                <<",\"different_dwords\":"<<different<<",\"files_saved\":"<<(saved?"true":"false")<<",\"stem\":\""<<stem<<"\"}\n";
        }
        for(const auto& event:job.events) audit->packets<<event<<'\n';
        audit->manifest.flush();audit->packets.flush();
    } catch(const std::exception& e) {DC_WARN("gpu: CP audit write failed: %s",e.what());}
}

void CommandProcessor::AuditPacket(std::span<const u32> packet,u64 offset,int depth,bool constant) {
    if(!t_audit_job || packet.empty()) return;
    try {
    auto& job=*t_audit_job;const u32 header=packet[0],op=HeaderOpcode(header);
    if(!constant) {job.packet_address=reinterpret_cast<u64>(packet.data());job.packet_offset=offset;job.packet_header=header;job.packet_depth=depth;}
    const bool selected=op==0x1f || op==0x20 || op==0x22 || op==Op::EventWrite ||
        op==Op::EventWriteEop || op==Op::EventWriteEos || op==Op::ReleaseMem ||
        op==Op::DrawIndirect || op==Op::DrawIndexIndirect || op==Op::DrawIndex2 ||
        op==Op::DrawIndexOffset2 || op==Op::DrawIndexAuto || op==Op::IndirectBuffer || op==Op::IndirectBufferConst;
    if(!selected || audit->events.fetch_add(1)>=AuditState::EventLimit) return;
    const auto& registers=CurRegs();
    std::ostringstream row;row<<AuditNow()<<','<<job.id<<','<<(t_ring?t_ring->index:0)<<','<<t_audit_buffer
        <<",packet,"<<constant<<','<<depth<<','<<reinterpret_cast<u64>(packet.data())<<','<<offset<<','<<op<<','<<header<<','<<(header&1)
        <<",,,,,,,,,"<<registers[0xa001]<<','<<registers[Reg::DB_RENDER_CONTROL]<<','<<registers[Reg::DB_DEPTH_CONTROL]
        <<','<<registers[Reg::CB_TARGET_MASK]<<','<<(u64(registers[Reg::DB_Z_READ_BASE])<<8)<<",\"";
    for(std::size_t i=0;i<std::min<std::size_t>(packet.size(),16);++i) row<<(i?" ":"")<<std::hex<<packet[i];
    row<<'"';auto line=row.str();if(line.size()<=512) job.events.push_back(std::move(line));
    } catch(const std::exception&) { /* Diagnostic allocation must not fail execution. */ }
}

void CommandProcessor::AuditDraw(bool indexed,u32 count,u32 offset,u32 first,u32 instances) {
    if(!t_audit_job || audit->events.fetch_add(1)>=AuditState::EventLimit) return;
    try {
    const auto& job=*t_audit_job;const auto& registers=CurRegs();
    std::ostringstream row;row<<AuditNow()<<','<<job.id<<','<<(t_ring?t_ring->index:0)<<','<<t_audit_buffer
        <<",draw,0,"<<job.packet_depth<<','<<job.packet_address<<','<<job.packet_offset<<','<<HeaderOpcode(job.packet_header)
        <<','<<job.packet_header<<','<<(job.packet_header&1)<<','<<count<<','<<indexed<<','<<(instances?instances:num_instances)
        <<','<<offset<<','<<first<<','<<registers.ProgramAddr(Reg::SPI_SHADER_PGM_LO_VS)<<','<<registers.ProgramAddr(Reg::SPI_SHADER_PGM_LO_PS)
        <<','<<(u64(registers[Reg::CB_COLOR0_BASE])<<8)<<','<<registers[0xa001]<<','<<registers[Reg::DB_RENDER_CONTROL]
        <<','<<registers[Reg::DB_DEPTH_CONTROL]<<','<<registers[Reg::CB_TARGET_MASK]<<','<<(u64(registers[Reg::DB_Z_READ_BASE])<<8)<<",\"\"";
    auto line=row.str();if(line.size()<=512) t_audit_job->events.push_back(std::move(line));
    } catch(const std::exception&) { /* Diagnostic allocation must not fail execution. */ }
}

CommandProcessor::CommandProcessor()
    :fence_map_cache(Metal::CpFenceMapCache::ConfiguredEnabled(
        std::getenv("BB_METAL_CP_FENCE_MAP_CACHE")),Core::Memory::kDirectSize,
        Metal::CpFenceMapCache::ConfiguredEnabled(std::getenv("BB_METAL_CP_FENCE_DEST_INDEX"))) {
    trace = std::getenv("DC_CP_TRACE") != nullptr;
    const char* validation=std::getenv("BB_METAL_CP_VALIDATE");
    validate_registers=validation && !std::strcmp(validation,"1");
    InitAudit();
    ClearState();
}

// CLEAR_STATE: context registers back to their power-on values. Most are 0;
// these are the ones whose default matters (from AMD's CIK clear-state table).
void CommandProcessor::ClearState() {
    std::memset(regs.Data() + kContextRegBase, 0, 0x400 * 4);
    regs[Reg::PA_SC_SCREEN_SCISSOR_BR] = 0x40004000;
    regs[Reg::PA_SC_WINDOW_SCISSOR_TL] = 0x80000000;
    regs[Reg::PA_SC_WINDOW_SCISSOR_BR] = 0x40004000;
    regs[0xA083] = 0xFFFF; // PA_SC_CLIPRECT_RULE
    for (u32 k = 0; k < 4; ++k) {
        regs[0xA085 + 2 * k] = 0x20002000; // PA_SC_CLIPRECT_n_BR
    }
    regs[0xA08C] = 0xAA99AAAA; // PA_SC_EDGERULE
    regs[Reg::PA_SC_GENERIC_SCISSOR_TL] = 0x80000000;
    regs[Reg::PA_SC_GENERIC_SCISSOR_BR] = 0x40004000;
    for (u32 k = 0; k < 16; ++k) {
        regs[Reg::PA_SC_VPORT_SCISSOR_0_TL + 2 * k] = 0x80000000;
        regs[Reg::PA_SC_VPORT_SCISSOR_0_TL + 2 * k + 1] = 0x40004000;
        regs[Reg::PA_SC_VPORT_ZMIN_0 + 2 * k + 1] = 0x3F800000; // ZMAX = 1.0
    }
}

CommandProcessor::~CommandProcessor() {
    stopping=true;execution->Notify();admission_changed.notify_all();
    for (auto& r : rings) {
        if (r) {
            r->thread.request_stop();
            r->cv.notify_all();
        }
    }
    for (auto& r:rings) if(r && r->thread.joinable()) r->thread.join();
    if(renderer) renderer->SetCpuReadGuard({});
    std::lock_guard guard(callback_gate->lock);callback_gate->alive=false;
}

void CommandProcessor::SetRenderer(Gpu::Renderer* r) {
    if(renderer && renderer!=r) renderer->SetCpuReadGuard({});
    renderer=r;
    if(r) {
        execution=r->Execution();
        r->SetCpuReadGuard([this](GuestAddr address,std::size_t bytes) {
            return !PendingFenceOverlaps(address,bytes);
        });
    }
}
void CommandProcessor::Fail(const std::string& reason) {
    if(execution->Fail(reason)) DC_ERROR("gpu: %s",reason.c_str());
    admission_changed.notify_all();
}
bool CommandProcessor::WaitLabel(u64 address,u64 reference,u32 bytes,u32 compare,u64 mask,
    std::chrono::steady_clock::time_point deadline,bool fatal_timeout) {
    CpTiming timing("cp_wait_label",t_ring?t_ring->index:0,address);
    Metal::GuestLabel label;
    if(!label.Capture(address,bytes)) {Fail("GPU wait refers to an invalid label at "+std::to_string(address));return false;}
    const auto matches=[&](u64 v) {
        v&=mask;
        switch(compare) {
        case 0:return true;case 1:return v<reference;case 2:return v<=reference;
        case 3:return v==reference;case 4:return v!=reference;case 5:return v>=reference;case 6:return v>reference;
        default:return false;
        }
    };
    bool parked=false;
    for(;;) {
        const auto epoch=execution->Epoch();
        if(execution->Failed() || stopping) return false;
        u64 value=0;
        if(!label.Load(value)) {Fail("GPU wait label was unmapped or reused at "+std::to_string(address));return false;}
        if(matches(value)) return true;
        const auto now=std::chrono::steady_clock::now();
        if(now>=deadline) {
            if(fatal_timeout) {++stats.label_timeouts;Fail("GPU fence wait timed out at "+std::to_string(address)+
                " value="+std::to_string(value)+" reference="+std::to_string(reference));}
            return false;
        }
        // External CPU stores have no GPU callback. Periodic rechecks retain
        // that protocol while GPU-completed labels wake immediately.
        if(!parked) {++stats.label_waits;parked=true;}
        execution->Wait(epoch,std::min(deadline,now+std::chrono::microseconds(250)));
        stats.label_parked_us+=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-now).count();
    }
}

CommandProcessor::Ring& CommandProcessor::GetRing(u32 index) {
    std::scoped_lock lk(rings_lock);
    if (rings.size() <= index) {
        rings.resize(index + 1);
    }
    if (!rings[index]) {
        auto ring = std::make_unique<Ring>();
        ring->index = index;
        Ring* r = ring.get();
        r->thread = std::jthread([this, r](std::stop_token st) { Run(*r, st); });
        rings[index] = std::move(ring);
    }
    return *rings[index];
}

void CommandProcessor::Push(u32 index, Job job) {
    std::unique_lock admission(admission_lock);
    while(checkpoint_paused && !execution->Failed() && !stopping)
        admission_changed.wait_for(admission,std::chrono::milliseconds(10));
    if(execution->Failed() || stopping) return;
    Ring& r = GetRing(index);
    {
        std::scoped_lock lk(r.lock);
        r.jobs.push_back(std::move(job));
    }
    r.cv.notify_one();
}

void CommandProcessor::SubmitGraphics(std::vector<CmdBuffers> bufs, std::function<void()> after) {
    ++stats.submits;
    auto evidence=AuditSubmission(bufs);
    Push(0, {std::move(bufs), std::move(after),std::move(evidence)});
}

void CommandProcessor::SubmitCompute(u32 queue, std::span<const u32> acb) {
    Push(queue, {{CmdBuffers{acb, {}}}, {}});
}

void CommandProcessor::WaitIdle() {
    std::vector<Ring*> all;
    {
        std::scoped_lock lk(rings_lock);
        for (auto& r : rings) {
            if (r) {
                all.push_back(r.get());
            }
        }
    }
    for (Ring* r : all) {
        std::unique_lock lk(r->lock);
        r->idle_cv.wait(lk, [&] { return r->jobs.empty() && !r->busy; });
    }
}
void CommandProcessor::ResumeAfterCheckpoint() {
    std::lock_guard lock(admission_lock);checkpoint_paused=false;admission_changed.notify_all();
}
bool CommandProcessor::PauseForCheckpoint(CheckpointState& out,std::chrono::steady_clock::time_point deadline,std::string& error) {
    if(execution->Failed()){error=execution->Error();return false;}
    {
        std::lock_guard lock(admission_lock);
        if(checkpoint_paused){error="GPU checkpoint is already paused.";return false;}
        checkpoint_paused=true;
    }
    const auto fail=[&](const char* why){ResumeAfterCheckpoint();error=why;return false;};
    try {
        std::vector<Ring*> all;
        {std::lock_guard lock(rings_lock);for(const auto& ring:rings)if(ring)all.push_back(ring.get());}
        if(all.size()>4096 || std::any_of(all.begin(),all.end(),[](const auto* r){return r->index>65535;}))
            return fail("Too many GPU rings for this checkpoint schema.");
        for(auto* ring:all) {
            std::unique_lock lock(ring->lock);
            if(!ring->idle_cv.wait_until(lock,deadline,[&]{return ring->jobs.empty() && !ring->busy;}))
                return fail("GPU ring did not finish before the checkpoint timeout.");
        }
        if(renderer && !renderer->WaitIdleUntil(deadline,error)){ResumeAfterCheckpoint();return false;}
        {std::lock_guard lock(fence_lock);if(!pending_fences.empty())return fail("GPU fences are still active after draining.");}
        if(!ce.stack.empty() || std::any_of(all.begin(),all.end(),[](const Ring* ring) {
            return ring->flip_label.addr || ring->flip_label.interrupt;
        }))return fail("GPU command continuation is still active.");
        CheckpointState state;state.index_base=index_base;state.index_size=index_size;state.num_instances=num_instances;
        state.ce_counter=ce.ce_counter;state.de_counter=ce.de_counter;
        state.registers.assign(regs.Data(),regs.Data()+RegFile::kSize);state.ce_ram=ce.ram;
        for(const auto* ring:all) {
            CheckpointRing saved;saved.index=ring->index;saved.indirect_base=ring->indirect_base;
            saved.registers.assign(ring->regs.Data(),ring->regs.Data()+RegFile::kSize);state.rings.push_back(std::move(saved));
        }
        out=std::move(state);error.clear();return true;
    } catch(const std::exception& e) {ResumeAfterCheckpoint();error=e.what();return false;}
}
bool CommandProcessor::RestoreCheckpoint(const CheckpointState& state,std::string& error) {
    const auto fail=[&](const char* why){error=why;return false;};
    if(state.version!=1 || (state.index_size!=2 && state.index_size!=4) || state.index_base>=1ull<<40 ||
       state.registers.size()!=RegFile::kSize || state.ce_ram.size()!=0xc000/4 || state.rings.size()>4096)
        return fail("Invalid GPU frontend checkpoint layout.");
    std::set<u32> indices;
    for(const auto& ring:state.rings)
        if(ring.index>65535 || ring.indirect_base>=1ull<<40 || ring.registers.size()!=RegFile::kSize || !indices.insert(ring.index).second)
            return fail("Invalid GPU checkpoint ring.");
    std::lock_guard admission(admission_lock);
    {std::lock_guard lock(rings_lock);if(checkpoint_paused || !rings.empty() || stats.submits.load())return fail("GPU frontend restore requires a fresh command processor.");}
    checkpoint_paused=true;
    // No caller may activate this fresh process if allocation/thread creation
    // fails. Admission remains closed on an incomplete restore.
    try {
        std::copy(state.registers.begin(),state.registers.end(),regs.Data());ce.ram=state.ce_ram;
        ce.ce_counter=state.ce_counter;ce.de_counter=state.de_counter;
        index_base=state.index_base;index_size=state.index_size;num_instances=state.num_instances;
        for(const auto& saved:state.rings) {
            auto& ring=GetRing(saved.index);ring.indirect_base=saved.indirect_base;
            std::copy(saved.registers.begin(),saved.registers.end(),ring.regs.Data());
        }
        error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}

void CommandProcessor::Run(Ring& ring, std::stop_token stop) {
    char name[32];
    std::snprintf(name, sizeof(name), ring.index == 0 ? "GPU gfx" : "GPU compute %u", ring.index);
    pthread_setname_np(name);
    if(ring.index==0)Metal::RenderThreadQos::Apply(name);
    t_ring = &ring;
    t_regs = ring.index == 0 ? &regs : &ring.regs;
    for (;;) {
        Job job;
        {
            std::unique_lock lk(ring.lock);
            const auto idle=Metal::FrameWork::Enabled()?Metal::StutterTrace::Now():0;
            ring.cv.wait(lk, [&] { return !ring.jobs.empty() || stop.stop_requested(); });
            if(idle) {
                const auto end=Metal::StutterTrace::Now();
                if(end-idle>.0001)Metal::FrameWork::Span(0,"cp_ring_idle",idle,end,ring.index);
            }
            if (stop.stop_requested()) {
                return;
            }
            job = std::move(ring.jobs.front());
            ring.jobs.pop_front();
            ring.busy = true;
        }
        const auto job_start=Metal::FrameWork::Enabled()?Metal::StutterTrace::Now():0;
        t_audit_job=job.audit.get();t_audit_buffer=0;
        for (const auto& buf : job.buffers) {
            if(job.audit) AuditConsume(*job.audit,buf,t_audit_buffer);
            if (ring.index == 0) {
                ce.stack.clear();
                if (!buf.ccb.empty()) {
                    ce.stack.push_back(buf.ccb);
                }
                ce.ce_counter = ce.de_counter = 0;
            }
            if(execution->Failed() || stopping) break;
            Execute(buf.dcb, ring.index != 0, 0);
            if (ring.index == 0) {
                RunConstantEngine([] { return false; }); // whatever the CE has left
            }
            ++t_audit_buffer;
        }
        if (job.after && !execution->Failed() && !stopping) {
            job.after();
        }
        if (ring.index==0 && !execution->Failed() && (ring.flip_label.addr || ring.flip_label.interrupt)) {
            const FlipLabel label = ring.flip_label;
            FenceWrite(label.addr ? kData32 : kDataNone,label.addr,label.value,0,
                label.interrupt ? kIrqGfxEop : 0);
            ring.flip_label = {};
        }
        if (renderer) {
            renderer->OnSubmit(); // hand everything, signals included, to the host GPU
        }
        if(job_start)Metal::FrameWork::Span(0,"cp_job",job_start,Metal::StutterTrace::Now(),ring.index);
        t_audit_job=nullptr;
        if(job.audit) AuditFinish(*job.audit);
        {
            std::scoped_lock lk(ring.lock);
            ring.busy = false;
        }
        ring.idle_cv.notify_all();
    }
}

RegFile& CommandProcessor::CurRegs() {
    return t_regs ? *t_regs : regs;
}

void CommandProcessor::SetRegs(u32 index, const u32* values, u32 count) {
    if (index + count > RegFile::kSize) {
        return;
    }
    std::memcpy(CurRegs().Data() + index, values, count * 4);
}

void CommandProcessor::AfterGpu(std::function<void()> fn,u64 write_address,std::size_t write_bytes,bool write_only) {
    auto guarded=[gate=callback_gate,state=execution,fn=std::move(fn)] {
        std::lock_guard guard(gate->lock);
        if(gate->alive && !state->Failed()) fn();
    };
    if (renderer) {
        if(write_bytes && write_only)renderer->SignalCpuWriteOnly(write_address,write_bytes,std::move(guarded));
        else if(write_bytes)renderer->SignalCpuWrite(write_address,write_bytes,std::move(guarded));
        else renderer->Signal(std::move(guarded));
    } else {
        guarded();
    }
}

void CommandProcessor::FenceWrite(u32 data_sel, u64 addr, u32 lo, u32 hi, u32 irq) {
    if(data_sel>kPerfCounter) {Fail("Unsupported GPU fence data selector");return;}
    Metal::GuestLabel destination;
    const u32 bytes=data_sel==kData32 ? 4 : data_sel==kDataNone ? 0 : 8;
    if(bytes && !destination.Capture(addr,bytes)) {Fail("Invalid GPU fence destination at "+std::to_string(addr));return;}
    u64 ticket=0;
    if (bytes) {
        std::scoped_lock lk(fence_lock);
        ticket=++next_fence_ticket;
        const u32 ring=t_ring ? t_ring->index : 0;
        const bool known=data_sel==kData32 || data_sel==kData64;
        // Advance before any potentially allocating insertion/overwrite. Both
        // qword halves form one transaction under this existing lock.
        pending_fence_epoch.Change();
        pending_fences[addr] = {lo,ticket,ring,known};
        if (bytes==8) {
            pending_fences[addr + 4] = {hi,ticket,ring,known};
        }
    }
    AfterGpu([=, this] {
        u64 value=u64(lo)|(u64(hi)<<32);
        if(data_sel==kGpuClock) value=GpuClock();
        if(data_sel==kPerfCounter) value=MacGuestClock::ReadTsc();
        if(bytes && !destination.Store(value)) {Fail("GPU fence destination was unmapped or reused at "+std::to_string(addr));return;}
        if (bytes) {
            std::scoped_lock lk(fence_lock);
            // A later write to the same label may still be on its way.
            bool changed=false;
            auto it = pending_fences.find(addr);
            if (it != pending_fences.end() && it->second.ticket == ticket) {
                pending_fence_epoch.Change();changed=true;
                pending_fences.erase(it);
            }
            if (bytes==8) {
                it = pending_fences.find(addr + 4);
                if (it != pending_fences.end() && it->second.ticket == ticket) {
                    if(!changed)pending_fence_epoch.Change();
                    pending_fences.erase(it);
                }
            }
        }
        execution->Notify();
        if (irq) {
            Interrupt(irq);
        }
    // Without an IRQ this closure's only guest-memory effect is the captured,
    // identity-checked label store above. An arbitrary interrupt handler may
    // write other memory, so irq-bearing callbacks retain the unknown effect.
    },addr,bytes,irq==0);
}

bool CommandProcessor::PendingFence(u64 addr, u32* value) {
    std::scoped_lock lk(fence_lock);
    auto it = pending_fences.find(addr);
    if (it == pending_fences.end() || !it->second.known || it->second.ring!=(t_ring ? t_ring->index : 0)) {
        return false;
    }
    *value = it->second.value;
    return true;
}

Metal::CpFenceMapCache::Counters CommandProcessor::FenceMapCacheStats() {
    std::scoped_lock lock(fence_lock);
    return fence_map_cache.Statistics();
}

bool CommandProcessor::PendingFenceOverlaps(u64 address,u64 bytes) {
    std::scoped_lock lock(fence_lock);
    if(fence_map_cache.Enabled())return fence_map_cache.Overlaps(address,bytes,pending_fences,
        [&] {return pending_fence_epoch.Current();},
        [] {return Core::Memory::DirectMapGeneration();},
        [] {return Core::Memory::DirectMappings();});
    if(!bytes) return false;
    if(address>UINT64_MAX-bytes) return true;
    if(pending_fences.empty()) return false;
    const auto mappings=Core::Memory::DirectMappings();
    const auto physical_address=[&](u64 at,u64 count,u64& physical) {
        for(const auto& mapping:mappings) if(at>=mapping.start && at-mapping.start<mapping.size &&
            count<=mapping.size-(at-mapping.start)) {
            if(mapping.phys>UINT64_MAX-(at-mapping.start)) return false;
            physical=mapping.phys+(at-mapping.start);return physical<=UINT64_MAX-count;
        }
        return false;
    };
    u64 physical=0;
    if(!physical_address(address,bytes,physical)) return true;
    for(const auto& [destination,write]:pending_fences) {
        (void)write;
        u64 at=0;if(!physical_address(destination,4,at)) return true;
        if(at<physical+bytes && physical<at+4) return true;
    }
    return false;
}

bool CommandProcessor::SyncRenderer(u32 reason) {
    CpTiming timing("cp_sync_renderer",t_ring?t_ring->index:0,reason);
    if (renderer) {
        std::string error;
        if(!renderer->WaitIdleUntil(std::chrono::steady_clock::now()+std::chrono::seconds(10),error)) {
            Fail("GPU dependency failed: "+error);return false;
        }
    }
    return !execution->Failed();
}

void CommandProcessor::RunConstantEngine(const std::function<bool()>& done) {
    while (!ce.stack.empty() && !done()) {
        std::span<const u32>& cur = ce.stack.back();
        if (cur.empty()) {
            ce.stack.pop_back();
            continue;
        }
        const u32 header = cur[0];
        const u32 type = HeaderType(header);
        if (header == 0 || type == 2) {
            cur = cur.subspan(1);
            continue;
        }
        if (type != 3) {
            DC_WARN("gpu: CE: unsupported packet type %u", type);
            ce.stack.clear();
            return;
        }
        const u32 n = HeaderBodyDwords(header);
        if (n + 1 > cur.size()) {
            ce.stack.pop_back();
            continue;
        }
        const u32* body = cur.data() + 1;
        if (n<Metal::MinimumPacketBody(HeaderOpcode(header))) {
            ++stats.malformed_packets; ce.stack.pop_back(); continue;
        }
        AuditPacket({body-1,std::size_t(n)+1},0,int(ce.stack.size())-1,true);
        switch (HeaderOpcode(header)) {
        case Op::WriteConstRam: {
            const u32 off = (body[0] & 0xFFFF) / 4;
            for (u32 k = 0; k + 1 < n && off + k < ce.ram.size(); ++k) {
                ce.ram[off + k] = body[1 + k];
            }
            break;
        }
        case Op::LoadConstRam: {
            const u32* src = Ptr<const u32>(body[0], body[1]);
            const u32 count = body[2] & 0x7FFF;
            const u32 off = (body[3] & 0xFFFF) / 4;
            if (src && off + count <= ce.ram.size() && Mapped(src,u64(count)*4)) {
                std::memcpy(ce.ram.data() + off, src, count * 4);
            }
            break;
        }
        case Op::DumpConstRam: {
            const u32 off = (body[0] & 0xFFFF) / 4;
            const u32 count = body[1] & 0x7FFF;
            u32* dst = Ptr<u32>(body[2], body[3]);
            if (!dst || off + count > ce.ram.size() || !Mapped(dst,u64(count)*4)) {
                break;
            }
            if(renderer) renderer->CpuWrite(reinterpret_cast<u64>(dst),ce.ram.data()+off,count*4,true);
            else std::memcpy(dst,ce.ram.data()+off,count*4);
            break;
        }
        case Op::IncrementCeCounter:
            ++ce.ce_counter;
            break;
        case Op::WaitOnDeCounterDiff:
            if (ce.ce_counter - ce.de_counter >= body[0]) {
                return; // blocked until the DE catches up; retried later
            }
            break;
        case Op::IndirectBufferConst: {
            const u32* ib = Ptr<const u32>(body[0], body[1] & 0xFFFF);
            const u32 size = body[2] & 0xFFFFF;
            cur = cur.subspan(n + 1);
            if (ib && size && ce.stack.size()<8 && Mapped(ib,u64(size)*4)) {
                ce.stack.push_back({ib, size});
            }
            continue;
        }
        default:
            break;
        }
        cur = cur.subspan(n + 1);
    }
}

void CommandProcessor::Draw(bool indexed, u32 count, u32 index_offset, u32 first_vertex, u32 instances,bool indirect) {
    AuditDraw(indexed,count,index_offset,first_vertex,instances);
    ++stats.draws;
    if (!renderer || count == 0 || (indirect && instances==0)) {
        return;
    }
    DrawCall dc;
    dc.indexed = indexed;
    dc.count = count;
    dc.instances = indirect ? instances : instances ? instances : num_instances;
    dc.index_base = index_base;
    dc.index_offset = index_offset;
    dc.index_size = index_size;
    dc.first_vertex = first_vertex;
    renderer->Draw(CurRegs(), dc);
}

// Indirect draw/dispatch arguments live at the SET_BASE address + offset.
const u32* CommandProcessor::IndirectArgs(u32 offset, u32 dwords) {
    const Ring* ring = t_ring;
    CpTiming timing("cp_indirect_args",ring?ring->index:0,dwords);
    const u64 addr = (ring ? ring->indirect_base : 0) + offset;
    if (!addr || addr<offset || !dwords || dwords>5 || addr>UINT64_MAX-u64(dwords)*4) {
        Fail("Invalid indirect argument address");return nullptr;
    }
    std::array<Metal::GuestLabel,5> labels;
    for(u32 k=0;k<dwords;++k) if(!labels[k].Capture(addr+k*4,4)) {
        Fail("Unmapped indirect argument buffer");return nullptr;
    }
    // Commit GPU producers before CPU reads. Retain the captured identities
    // across that wait so an unmap/reuse cannot silently change the arguments.
    if(!SyncRenderer(1)) return nullptr;
    static thread_local std::array<u32,5> snapshot;
    for(u32 k=0;k<dwords;++k) {
        u64 value=0;
        if(!labels[k].Load(value)) {Fail("Indirect argument mapping expired during GPU completion");return nullptr;}
        snapshot[k]=u32(value);
    }
    return snapshot.data();
}

void CommandProcessor::Interrupt(u32 irq) {
    if (on_irq) {
        on_irq(irq);
    }
}

void CommandProcessor::Execute(std::span<const u32> cmds, bool compute, int depth) {
    RegFile& regs = CurRegs();
    const u32* p = cmds.data();
    const u32* end = p + cmds.size();
    std::unique_ptr<Metal::RegisterValidation> validation;
    if (validate_registers) validation=std::make_unique<Metal::RegisterValidation>();

    const u32* recent[4] = {}; // last packets, for desync reports
    while (p < end) {
        if(execution->Failed() || stopping) return;
        const u32 header = *p;
        const u32 type = HeaderType(header);
        if (type == 3) {
            std::memmove(recent, recent + 1, sizeof(recent) - sizeof(recent[0]));
            recent[3] = p;
        }
        if (header == 0 || type == 2) { // filler / trailing padding
            ++p;
            continue;
        }
        if (type == 0) {
            const u32 base_reg = header & 0xFFFF;
            const u32 count = ((header >> 16) & 0x3FFF) + 1;
            if (size_t(end-p)<size_t(count)+1) {
                ++stats.malformed_packets;
                break;
            }
            for (u32 i = 0; i < count; ++i) {
                if (base_reg + i < RegFile::kSize) {
                    regs[base_reg + i] = p[1 + i];
                }
            }
            p += 1 + count;
            continue;
        }
        if (type != 3) {
            ++stats.malformed_packets;
            DC_WARN("gpu: unsupported PM4 packet type %u (header %#x) at +%#zx of %#zx dwords", type, header,
                    static_cast<size_t>(p - cmds.data()), cmds.size());
            for (const u32* q : recent) {
                if (q) {
                    DC_WARN("gpu:   after +%#zx header %08x", static_cast<size_t>(q - cmds.data()),q[0]);
                }
            }
            return;
        }
        const u32 op = HeaderOpcode(header);
        const u32 body_n = HeaderBodyDwords(header);
        const u32* body = p + 1;
        if (size_t(end-p)<size_t(body_n)+1 || body_n<Metal::MinimumPacketBody(op)) {
            ++stats.malformed_packets;
            DC_WARN("gpu: truncated PM4 opcode %#x at +%#zx",op,size_t(p-cmds.data()));
            return;
        }
        if (validation) validation->Prepare({p,size_t(body_n)+1});
        ++stats.packets;
        AuditPacket({p,std::size_t(body_n)+1},u64(p-cmds.data()),depth,false);

        switch (op) {
        case Op::Nop:
            // The Gnm driver hides flip requests in NOPs: "when this frame is
            // shown, write `value` to `label` and/or raise the EOP interrupt".
            // Compute queues run concurrently and ordinary NOP payloads must
            // neither replace nor retire a graphics job's flip marker.
            if (!compute && t_ring && t_ring->index==0) {
                if (body_n >= 4 && (body[0] == kNopPrepareFlipLabel || body[0] == kNopPrepareFlipInterruptLabel)) {
                    t_ring->flip_label = {u64(body[1] & ~3u) | (u64(body[2]) << 32), body[3], body[0] == kNopPrepareFlipInterruptLabel};
                } else if (body_n >= 1 && body[0] == kNopPrepareFlipInterrupt) {
                    t_ring->flip_label = {0, 0, true};
                }
            }
            break;
        case Op::ClearState:
            ClearState();
            break;

        case Op::SetContextReg:
            SetRegs(kContextRegBase + (body[0]&0xffff), body + 1, body_n - 1);
            if (renderer) {
                renderer->OnContextRegs(body, body_n, size_t(end-body));
            }
            break;
        case Op::SetShReg:
            SetRegs(kShRegBase + (body[0]&0xffff), body + 1, body_n - 1);
            break;
        case Op::SetUconfigReg:
            SetRegs(kUconfigRegBase + (body[0]&0xffff), body + 1, body_n - 1);
            break;
        case Op::SetConfigReg:
            SetRegs(kConfigRegBase + (body[0]&0xffff), body + 1, body_n - 1);
            break;

        case Op::IndirectBuffer: {
            const u32* ib = Ptr<const u32>(body[0], body[1] & 0xFFFF);
            const u32 size = body[2] & 0xFFFFF;
            if (depth < 4 && Mapped(ib,u64(size)*4)) {
                Execute({ib, size}, compute, depth + 1);
            }
            break;
        }

        case Op::EventWriteEop: {
            const u32 data_control = body[2];
            const u32 int_sel = (data_control >> 24) & 3;
            const bool irq = int_sel == kIntOnly || int_sel == kIntWriteConfirm;
            FenceWrite((data_control >> 29) & 7, u64(body[1]) | (u64(data_control & 0xFFFF) << 32), body[3], body[4],
                       irq ? kIrqGfxEop : 0);
            break;
        }
        case Op::ReleaseMem: {
            const u32 dw2 = body[1];
            const u32 int_sel = (dw2 >> 24) & 7;
            const bool irq = int_sel == kIntWriteConfirm || int_sel == kIntUndocumented;
            FenceWrite((dw2 >> 29) & 7, u64(body[2]) | (u64(body[3]) << 32), body[4], body[5],
                       irq && !compute ? kIrqGfxEop : 0);
            break;
        }
        case Op::EventWriteEos: {
            const u32 cmd = (body[2] >> 29) & 7;
            if (cmd == 2) { // signal fence
                FenceWrite(kData32, u64(body[1]) | (u64(body[2] & 0xFFFF) << 32), body[3], 0, 0);
            }
            break;
        }

        case Op::WriteData: {
            const u32 dst_sel = (body[0] >> 8) & 0xF;
            if (dst_sel == 1 || dst_sel == 2 || dst_sel == 5) {
                static const bool log = std::getenv("DC_LOG_WRITEDATA") != nullptr;
                if (log) {
                    DC_INFO("gpu: WRITE_DATA %#llx %u dwords ctl %#x val %#x",
                            (unsigned long long)(u64(body[1]) | (u64(body[2]) << 32)), body_n - 3, body[0],
                            body_n > 3 ? body[3] : 0);
                }
                u32* dst = Ptr<u32>(body[1], body[2]);
                if (!Mapped(dst,u64(body_n-3)*4)) { ++stats.malformed_packets; break; }
                // Like a CE dump: draws recorded before this one must still read the
                // old data when the host GPU runs them, later ones the new data.
                if(renderer) renderer->CpuWrite(reinterpret_cast<u64>(dst),body+3,(body_n-3)*4,body_n-3>2);
                else std::memcpy(dst,body+3,(body_n-3)*4);
            }
            break;
        }

        case Op::DmaData: {
            // CP DMA: copies/fills between memory, GDS and immediate data.
            const u32 ctl = body[0];
            const u32 cmd = body[5];
            const u32 bytes = cmd & 0x1FFFFF;
            const u32 src_sel = (ctl >> 29) & 3; // 0/3 memory, 1 GDS, 2 data
            const u32 dst_sel = (ctl >> 20) & 3; // 0/3 memory, 1 GDS
            if ((cmd >> 27) & 1) {
                break; // destination is register space: cache prefetch hints
            }
            if (renderer) {
                const u32 dsel = dst_sel == 1 ? 1 : 0;
                const u32 ssel = src_sel == 3 ? 0 : src_sel;
                const u64 dst_addr = dsel ? (body[3] & 0xFFFF) : u64(body[3]) | (u64(body[4]) << 32);
                const u64 src_addr = ssel == 2 ? body[1]
                                   : ssel == 1 ? (body[1] & 0xFFFF) : u64(body[1]) | (u64(body[2]) << 32);
                if (bytes && renderer->DmaData(dsel, dst_addr, ssel, src_addr, bytes)) {
                    break;
                }
            }
            u8* gds = renderer ? renderer->Gds() : nullptr;
            u8* dst = dst_sel == 1 ? (gds ? gds + (body[3] & 0xFFFF) : nullptr) : Ptr<u8>(body[3], body[4]);
            if (!dst || bytes == 0 || (dst_sel != 1 && !Mapped(dst, bytes)) ||
                (src_sel != 1 && src_sel != 2 && !Mapped(Ptr<const u8>(body[1], body[2]), bytes))) {
                if (bytes) {
                    DC_WARN("gpu: DMA_DATA to/from unmapped memory skipped (ctl %#x dst %p, %u bytes)", ctl, dst, bytes);
                }
                break;
            }
            if(!SyncRenderer(2)) return; // the GPU may still be producing what we copy
            if(renderer && dst_sel!=1) {
                // The packet encodes at most0x1fffff bytes. Stage the source
                // before CpuWrite so virtual and physical aliases preserve
                // memmove behavior, and journal the completed host write.
                std::vector<u8> staged(bytes);
                if(src_sel==2) {
                    std::memcpy(staged.data(),dst,bytes); // retain any trailing1..3 bytes
                    for(u32 k=0;k+4<=bytes;k+=4)std::memcpy(staged.data()+k,&body[1],4);
                } else {
                    const u8* src=src_sel==1 ? (gds ? gds+(body[1]&0xffff) : nullptr) : Ptr<const u8>(body[1],body[2]);
                    if(!src)break;
                    std::memcpy(staged.data(),src,bytes);
                }
                renderer->CpuWrite(reinterpret_cast<u64>(dst),staged.data(),bytes,false);
            } else if (src_sel == 2) {
                for (u32 k = 0; k + 4 <= bytes; k += 4) {
                    std::memcpy(dst + k, &body[1], 4);
                }
            } else {
                const u8* src = src_sel == 1 ? (gds ? gds + (body[1] & 0xFFFF) : nullptr) : Ptr<const u8>(body[1], body[2]);
                if (src) {
                    std::memmove(dst, src, bytes);
                }
            }
            break;
        }

        case Op::WaitRegMem: {
            const u32 func = body[0] & 7;
            const bool memory = (body[0] >> 4) & 1;
            const u32 ref = body[3];
            const u32 mask = body[4];
            if (memory) {
                const u64 address=u64(body[1]&~3u)|(u64(body[2])<<32);
                Metal::GuestLabel label;u64 value=0;
                if(!label.Capture(address,4) || !label.Load(value)) {Fail("Invalid WAIT_REG_MEM label");return;}
                if (Compare(func, u32(value) & mask, ref)) {
                    break;
                }
                u32 pending = 0;
                if (PendingFence(address, &pending) && Compare(func, pending & mask, ref)) {
                    break;
                }
                // Whatever produces the value may sit in recorded host work.
                if (renderer) {
                    renderer->OnSubmit();
                }
                if(!WaitLabel(address,ref,4,func,mask,std::chrono::steady_clock::now()+std::chrono::seconds(5),true)) return;
            }
            break;
        }

        case Op::WaitOnCeCounter:
            if (!compute) {
                RunConstantEngine([&] { return ce.ce_counter > ce.de_counter; });
            }
            break;
        case Op::IncrementDeCounter:
            ++ce.de_counter;
            break;

        case Op::IndexType:
            index_size = (body[0] & 3) == 1 ? 4 : 2;
            break;
        case Op::IndexBase:
            index_base = u64(body[0] & ~1u) | (u64(body[1] & 0xFFFF) << 32);
            break;
        case Op::NumInstances:
            num_instances = body[0] ? body[0] : 1;
            break;

        case Op::DrawIndex2:
            index_base = u64(body[1]) | (u64(body[2] & 0xFFFF) << 32);
            Draw(true, body[3], 0, 0, 0);
            break;
        case Op::DrawIndexOffset2:
            Draw(true, body[2], body[1], 0, 0);
            break;
        case Op::DrawIndexAuto:
            if(!compute) MacFeatures::ObserveFogDrawPacket(reinterpret_cast<u64>(p),body[0],
                regs.ProgramAddr(Reg::SPI_SHADER_PGM_LO_PS),regs.ProgramAddr(Reg::SPI_SHADER_PGM_LO_VS),
                u64(regs[Reg::CB_COLOR0_BASE])<<8);
            Draw(false, body[0], 0, 0, 0);
            break;
        case Op::SetBase:
            if (t_ring) {
                t_ring->indirect_base = u64(body[1] & ~7u) | (u64(body[2] & 0xFFFF) << 32);
                t_ring->indirect_selector = body[0];
            }
            break;
        case Op::DrawIndirect:
        case Op::DrawIndexIndirect: {
            // DrawIndirect args: vertex count, instances, first vertex, first instance.
            // DrawIndexIndirect: index count, instances, first index, base vertex, first instance.
            const bool indexed = op == Op::DrawIndexIndirect;
            const u32 base_vtx_loc = body[1] & 0xFFFF;
            const u32 start_inst_loc = body[2] & 0xFFFF;
            // The generic path cannot defer PM4 writes to arbitrary SH
            // registers. Only the no-destination graphics packet is native.
            // Indexed arguments require a separately proven index extent.
            if(!compute && !indexed && renderer && t_ring && t_ring->indirect_selector==1 &&
                body_n==4 && body[3]==2 && !body[1] && !body[2] &&
                t_ring->indirect_base<=UINT64_MAX-body[0]) {
                DrawCall dc;dc.index_base=index_base;dc.index_size=index_size;
                const u64 address=t_ring->indirect_base+body[0];
                if(renderer->DrawIndirect(regs,dc,address,[&](GuestAddr read_address,std::size_t read_bytes) {
                    return !PendingFenceOverlaps(read_address,read_bytes);
                })) {
                    ++stats.draws;++stats.native_indirect_draws;break;
                }
            }
            ++stats.indirect_fallbacks;
            const u32* a = IndirectArgs(body[0], indexed ? 5 : 4);
            if (!a) {
                break;
            }
            if (base_vtx_loc && kShRegBase+base_vtx_loc<RegFile::kSize) {
                regs[kShRegBase + base_vtx_loc] = indexed ? a[3] : a[2];
            }
            if (start_inst_loc && kShRegBase+start_inst_loc<RegFile::kSize) {
                regs[kShRegBase + start_inst_loc] = indexed ? a[4] : a[3];
            }
            if (indexed) {
                Draw(true, a[0], a[2], 0, a[1],true);
            } else {
                Draw(false, a[0], 0, a[2], a[1],true);
            }
            break;
        }

        case Op::DispatchDirect:
            ++stats.dispatches;
            if (renderer) {
                renderer->Dispatch(regs, body[0], body[1], body[2]);
            }
            break;
        case Op::DispatchIndirect: {
            ++stats.dispatches;
            // MEC's three-word absolute-address packet has a different ABI.
            // Only the observed graphics SET_BASE(1), offset, initiator=1
            // form enters this optimization; unknown forms keep old decoding.
            if(!compute && renderer && t_ring && t_ring->indirect_selector==1 &&
                body_n==2 && body[1]==1 && t_ring->indirect_base<=UINT64_MAX-body[0] &&
                renderer->DispatchIndirect(regs,t_ring->indirect_base+body[0],[&](GuestAddr read_address,std::size_t read_bytes) {
                    return !PendingFenceOverlaps(read_address,read_bytes);
                })) {
                ++stats.native_indirect_dispatches;break;
            }
            ++stats.indirect_fallbacks;
            const u32* a = IndirectArgs(body[0], 3);
            if (renderer && a) {
                renderer->Dispatch(regs, a[0], a[1], a[2]);
            }
            break;
        }

        default:
            if (trace) {
                static std::atomic<u64> seen[4]{};
                const u64 bit = 1ull << (op & 63);
                if (!(seen[op >> 6].fetch_or(bit) & bit)) {
                    DC_INFO("gpu: CP%s: unhandled PM4 opcode %#x (%u dwords)", compute ? " compute" : "", op, body_n);
                }
            }
            break;
        }
        if (validation && validation->Checked()) {
            ++stats.validated_register_packets;
            if (auto index=validation->Mismatch({regs.Data(),RegFile::kSize})) {
                ++stats.register_mismatches;
                DC_WARN("gpu: independent decoder mismatch at register %#x, opcode %#x",*index,op);
            }
        }
        if (trace && (op == Op::EventWriteEop || op == Op::ReleaseMem || op == Op::EventWriteEos ||
                      op == Op::WriteData || op == Op::DmaData || op == Op::WaitRegMem)) {
            DC_INFO("gpu: CP%s %#x: %08x %08x %08x %08x %08x %08x", compute ? " compute" : "", op, body[0],
                    body_n > 1 ? body[1] : 0, body_n > 2 ? body[2] : 0, body_n > 3 ? body[3] : 0,
                    body_n > 4 ? body[4] : 0, body_n > 5 ? body[5] : 0);
        }
        p += body_n + 1;
    }
}

CommandProcessor& Instance() {
    static CommandProcessor cp;
    return cp;
}

} // namespace Gpu::Liverpool
