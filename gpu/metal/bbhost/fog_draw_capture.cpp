// SPDX-License-Identifier: GPL-3.0-or-later
// Observe where small PM4 draws (counts2..16) originate before submission. No
// command/resource bytes or native return values are modified by this probe.
#include "fog_draw_capture.h"
#include "core/elf.h"
#include "core/host_clock.h"
#include "core/portable.h"
#include "engine/addr.h"
#include "engine/graphics_patch.h"
#include "guest_abi.h"
#include "log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

namespace MacFeatures {
namespace {
constexpr std::uint64_t kWriterRva=0x10741b0;
constexpr std::uint64_t kProducerRva=0x28ec760;
constexpr std::uint64_t kAllocationRva=0x26db1b0,kAllocationResultRva=0x26db2d4;
constexpr std::size_t kLimit=4096,kDepth=12;
constexpr std::uint64_t kStackWindow=1ull<<20;
constexpr auto kDuration=std::chrono::seconds(30);
// push rbp; mov rbp,rsp; push r14; push rbx; mov r14d,esi;
// mov rbx,rdi; mov rcx,[rbx+8]. Complete, no RIP-relative instructions.
constexpr std::uint8_t kPrologue[]{0x55,0x48,0x89,0xe5,0x41,0x56,0x53,
    0x41,0x89,0xf6,0x48,0x89,0xfb,0x48,0x8b,0x4b,0x08};
constexpr std::uint8_t kProducerPrologue[]{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
    0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x98,0x01,0,0};
constexpr std::uint8_t kAllocationPrologue[]{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
    0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x28,0x49,0x89,0xd7};
// mov rcx,[r13]; cmp rcx,[rbp-0x30]; jne26db2ed; add rsp,0x28.
// This is the common EAX result, before the original stack-canary check.
constexpr std::uint8_t kAllocationResult[]{0x49,0x8b,0x4d,0x00,0x48,0x3b,0x4d,0xd0,
    0x75,0x0f,0x48,0x83,0xc4,0x28};

struct Frame { std::uint64_t rbp=0,return_ip=0; };
struct Record {
    std::uint64_t monotonic_ns=0,steady_ns=0,entry_rsp=0,entry_rbp=0,return_ip=0;
    std::uint64_t writer=0,gx_context=0,buffer_start=0,buffer_end=0,cursor=0;
    std::uint64_t reserve_callback=0,callback_data=0,remaining_dwords=0;
    std::uint64_t producer_entry_sequence=0;
    std::array<Frame,kDepth> frames{};
    std::uint32_t thread_id=0,count=0,flags=0,frame_count=0;
    bool writer_readable=false,capacity_valid=false,return_readable=false,rbp_readable=false;
    // 0=depth limit,1=outside1MiB window,2=unreadable,3=nonincreasing frame,
    // 4=return outside loaded guest executable segment,5=null frame.
    unsigned stop=0;
};
struct Slot { Record value; std::atomic<bool> ready{false}; };
struct Entry {
    std::array<std::uint64_t,6> arguments{};
    std::uint64_t monotonic_ns=0,steady_ns=0,entry_rsp=0,return_ip=0;
    std::uint64_t stamp_object=0,data_header=0,appearance_parameters=0;
    std::uint32_t thread_id=0,live_count=0,context_stamp=0,expected_stamp=0;
    std::uint32_t appearance_selector=0,appearance_bits=0,read_mask=0,observed_gate=0;
};
struct EntrySlot { Entry value; std::atomic<bool> ready{false}; };
struct Packet {
    std::uint64_t steady_ns=0,address=0,ps=0,vs=0,cb0=0;
    std::uint32_t count=0;
};
struct PacketSlot { Packet value; std::atomic<bool> ready{false}; };
struct ArenaNode {
    std::uint64_t address=0,status_word=0,start=0,end=0,next=0;
    std::uint32_t status=0,read_mask=0;
};
struct ArenaSnapshot {
    std::uint64_t arena=0,begin=0,end=0,outstanding=0,free_bytes=0,free_head=0;
    std::array<std::uint64_t,4> pending_heads{};
    std::array<ArenaNode,32> free_nodes{},pending_nodes{};
    std::array<unsigned,4> pending_stops{};
    unsigned read_mask=0,free_count=0,pending_count=0,free_stop=0;
};
struct Allocation {
    std::uint64_t steady_ns=0,producer_sequence=0,entry_rsp=0,return_ip=0;
    std::uint64_t descriptor=0,output=0,gx_context=0,resource=0;
    std::uint64_t provider_manager=0,provider=0,provider_vtable=0,provider_allocate=0;
    std::uint32_t thread_id=0,requested_bytes=0,capacity=0,alignment=0,offset=0,last_offset=0,read_mask=0;
    std::array<std::uint8_t,80> resource_bytes{};
    ArenaSnapshot arena;
};
struct AllocationSlot { Allocation value;std::atomic<bool> ready{false}; };
struct AllocationResult {
    std::uint64_t steady_ns=0,allocation_sequence=0,producer_sequence=0,output_value=0;
    std::uint32_t thread_id=0,status=0;bool output_readable=false;
    bool resource_readable=false;
    std::array<std::uint8_t,80> resource_bytes{};
    ArenaSnapshot arena;
};
struct AllocationResultSlot { AllocationResult value;std::atomic<bool> ready{false}; };
struct EntryLink { std::uint64_t sequence=0,expected_rbp=0,return_ip=0; };
thread_local EntryLink latest_entry;
struct AllocationLink {std::uint64_t sequence=0,expected_rbp=0,return_ip=0,output=0,producer=0,provider=0,resource=0;};
thread_local AllocationLink latest_allocation;
struct State {
    std::array<Slot,kLimit> slots{};
    std::array<EntrySlot,kLimit> entries{};
    std::array<PacketSlot,kLimit> packets{};
    std::array<AllocationSlot,kLimit> allocations{};
    std::array<AllocationResultSlot,kLimit> allocation_results{};
    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> entry_next{0};
    std::atomic<std::size_t> packet_next{0};
    std::atomic<std::size_t> allocation_next{0},allocation_result_next{0};
    std::atomic<unsigned> failure_details{0};
    std::atomic<unsigned> inflight{0};
    std::atomic<bool> armed{false};
    bool producer_enabled=false;
    bool allocation_enabled=false;
    std::uint64_t packet_ps=0x80d537b100ull,packet_cb0=0x806b240000ull;
    std::uint64_t slide=0;
    std::array<std::pair<std::uint64_t,std::uint64_t>,8> executable{};
    std::size_t executable_count=0;
    std::string trigger,initial_token;
    std::FILE* output=nullptr;
};
// The installed trampoline lives for the process. So does its diagnostic
// state, including after the worker closes its bounded output file.
std::atomic<State*> state{nullptr};
struct CaptureScope {
    State& s;bool entered;
    explicit CaptureScope(State& value):s(value),entered(false) {
        s.inflight.fetch_add(1,std::memory_order_acq_rel);
        entered=s.armed.load(std::memory_order_acquire);
    }
    ~CaptureScope() {s.inflight.fetch_sub(1,std::memory_order_release);}
};

std::uint64_t SteadyNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
template<class T> bool Read(std::uint64_t address,T& value) {
    return address && sizeof(T)<=std::numeric_limits<std::uint64_t>::max()-address &&
        host_read_safe(reinterpret_cast<const void*>(address),&value,sizeof(value));
}
template<class T> bool Field(std::uint64_t base,std::uint64_t offset,T& value) {
    return base && offset<=UINT64_MAX-base && Read(base+offset,value);
}

// Snapshots of original early-gate inputs, not execution of those gates.
// Read-mask bits: livecount1, contextstamp2, stampobject4, expectedstamp8,
// appearanceselector16, dataheader32, appearanceparameters64, floatbits128,
// originalreturn256. Appearance chain follows exact loads at28ec7cc..7d8.
__attribute__((noinline)) GUEST_ABI std::int64_t ObserveProducer(std::uint64_t,const std::uint64_t* saved) noexcept {
    // Clear even when the capture limit is reached: a later invocation may
    // reuse exactly the same stack addresses after the previous one returns.
    latest_entry={};
    State* s=state.load(std::memory_order_acquire);
    if(!s || !s->producer_enabled || !s->armed.load(std::memory_order_acquire) || !saved) return 0;
    CaptureScope active(*s);if(!active.entered) return 0;
    if(s->entry_next.load(std::memory_order_relaxed)>=kLimit) return 0;
    const auto index=s->entry_next.fetch_add(1,std::memory_order_relaxed);if(index>=kLimit) return 0;
    Entry& e=s->entries[index].value;
    e.monotonic_ns=host_clock_monotonic_ns();e.steady_ns=SteadyNs();
    e.thread_id=std::uint32_t(pthread_mach_thread_np(pthread_self()));
    for(unsigned n=0;n<6;++n) e.arguments[n]=saved[5-n];
    e.entry_rsp=reinterpret_cast<std::uint64_t>(saved+6);
    if(Read(e.entry_rsp,e.return_ip)) e.read_mask|=256;
    if(Field(e.arguments[2],0xa0,e.live_count)) e.read_mask|=1;
    if(Field(e.arguments[1],0x18,e.context_stamp)) e.read_mask|=2;
    if(Field(e.arguments[1],0x8,e.stamp_object)) {
        e.read_mask|=4;
        if(Field(e.stamp_object,0x8,e.expected_stamp)) e.read_mask|=8;
    }
    if(Field(e.arguments[0],0x58,e.appearance_selector)) e.read_mask|=16;
    if((e.read_mask&16) && e.appearance_selector && Read(e.arguments[2],e.data_header)) {
        e.read_mask|=32;
        if(Field(e.data_header,0x58,e.appearance_parameters)) {
            e.read_mask|=64;
            if(Field(e.appearance_parameters,0x1c,e.appearance_bits)) e.read_mask|=128;
        }
    }
    // Gate classification uses integer bits only. VUCOMISS 0,value; JAE
    // rejects negative values and both zeros, but does not reject NaN.
    const auto magnitude=e.appearance_bits&0x7fffffffu;
    const bool nan=(magnitude&0x7f800000u)==0x7f800000u && (magnitude&0x7fffffu);
    const bool nonpositive=!nan && ((e.appearance_bits&0x80000000u) || !magnitude);
    if(!e.arguments[2]) e.observed_gate=1; // null data:28ec788
    else if(!(e.read_mask&1)) e.observed_gate=0; // unknown
    else if(!e.live_count) e.observed_gate=2; // no live particles:28ec791
    else if(!(e.read_mask&16) || (e.appearance_selector && !(e.read_mask&128))) e.observed_gate=0;
    else if(e.appearance_selector && nonpositive) e.observed_gate=3; //28ec7d8
    else if((e.read_mask&14)!=14) e.observed_gate=0;
    else if(e.context_stamp!=e.expected_stamp) e.observed_gate=4; //28ec7e8
    else e.observed_gate=5; // observed early gates pass; later gates unknown
    if((e.read_mask&256) && e.entry_rsp>=8) latest_entry={index+1,e.entry_rsp-8,e.return_ip};
    s->entries[index].ready.store(true,std::memory_order_release);
    return 0; // No original function calls, version changes, or skipped work.
}
bool Code(const State& s,std::uint64_t address) {
    for(std::size_t n=0;n<s.executable_count;++n)
        if(address>=s.executable[n].first && address<s.executable[n].second) return true;
    return false;
}
bool ProducerPC(const State& s,std::uint64_t ip) {
    // Exact28ec760 FDE. Matching stack addresses alone is insufficient:
    // another virtual producer can later reuse both RBP and caller return.
    return ip>=s.slide+kProducerRva && ip<s.slide+0x28ed327;
}
bool ActiveProducer(const State& s,std::uint64_t rbp,std::uint64_t rsp) {
    if(!latest_entry.sequence || rsp>UINT64_MAX-kStackWindow) return false;
    std::uint64_t last=rsp-1,preceding_return=0;
    for(unsigned n=0;n<kDepth;++n) {
        Frame f;
        if((rbp&7) || rbp<=last || rbp<rsp || rbp>rsp+kStackWindow-sizeof(f) || !Read(rbp,f)) return false;
        if(rbp==latest_entry.expected_rbp && f.return_ip==latest_entry.return_ip && ProducerPC(s,preceding_return)) return true;
        last=rbp;rbp=f.rbp;preceding_return=f.return_ip;
    }
    return false;
}
bool SnapshotNode(std::uint64_t address,ArenaNode& n) {
    n.address=address;std::array<std::uint64_t,6> words{};
    if(!Read(address,words)) return false;
    n.read_mask|=1;n.status_word=words[0];n.start=words[1];n.next=words[5];
    if(Field(words[3],8,n.end)) n.read_mask|=2;
    // Original26aa970..981: tagged status means a pointed-to DWORD,
    // otherwise the low DWORD of the node itself is its state enum.
    if(!(n.status_word&1)) {n.status=std::uint32_t(n.status_word);n.read_mask|=4;}
    else if(Read(n.status_word&~1ull,n.status)) n.read_mask|=4;
    return true;
}
void SnapshotArena(std::uint64_t provider,ArenaSnapshot& a,bool detailed=false) {
    if(!Field(provider,8,a.arena)) return;
    a.read_mask|=1;
    // One bounded read captures sentinel/free heads, pending heads and
    // range/counters from exact native26a9b50/26a9db0/26aa860 layouts.
    std::array<std::uint64_t,14> fields{};
    if(!Field(a.arena,0x68,fields)) return;
    a.read_mask|=2;a.free_head=fields[5];
    for(unsigned n=0;n<4;++n) a.pending_heads[n]=fields[6+n];
    a.begin=fields[10];a.end=fields[11];a.outstanding=fields[12];a.free_bytes=fields[13];
    if(!detailed) return;
    a.read_mask|=4;
    const auto sentinel=a.arena<=UINT64_MAX-0x68 ? a.arena+0x68 : 0;
    auto at=a.free_head;
    while(at!=sentinel) {
        if(!at) {a.free_stop=4;break;}
        if(a.free_count==a.free_nodes.size()) {a.free_stop=1;break;}
        bool seen=false;for(unsigned n=0;n<a.free_count;++n) seen|=a.free_nodes[n].address==at;
        if(seen) {a.free_stop=3;break;}
        auto& node=a.free_nodes[a.free_count++];
        if(!SnapshotNode(at,node)) {a.free_stop=2;break;}
        at=node.next;
    }
    // Collector has four singly linked pending lists, each advanced at+0x28.
    // The combined pending budget is32 nodes, separately from32 free nodes.
    for(unsigned list=0;list<4;++list) {
        at=a.pending_heads[list];
        while(at) {
            if(a.pending_count==a.pending_nodes.size()) {a.pending_stops[list]=1;break;}
            bool seen=false;for(unsigned n=0;n<a.pending_count;++n) seen|=a.pending_nodes[n].address==at;
            if(seen) {a.pending_stops[list]=3;break;}
            auto& node=a.pending_nodes[a.pending_count++];
            if(!SnapshotNode(at,node)) {a.pending_stops[list]=2;break;}
            at=node.next;
        }
    }
    // These are unsynchronized diagnostic snapshots, never allocation inputs.
}
__attribute__((noinline)) GUEST_ABI std::int64_t ObserveAllocation(std::uint64_t,const std::uint64_t* saved) noexcept {
    latest_allocation={};
    State* s=state.load(std::memory_order_acquire);
    if(!s || !s->allocation_enabled || !s->armed.load(std::memory_order_acquire) || !saved) return 0;
    CaptureScope active(*s);if(!active.entered) return 0;
    const auto rsp=reinterpret_cast<std::uint64_t>(saved+6);
    std::uint64_t ret=0,rbp=0;
    if(!Read(rsp,ret) || (ret!=s->slide+0x28f89f1 && ret!=s->slide+0x28f8a09) ||
       !Read(reinterpret_cast<std::uint64_t>(__builtin_frame_address(0)),rbp) || !ActiveProducer(*s,rbp,rsp)) return 0;
    if(s->allocation_next.load(std::memory_order_relaxed)>=kLimit) return 0;
    const auto index=s->allocation_next.fetch_add(1,std::memory_order_relaxed);if(index>=kLimit) return 0;
    Allocation& a=s->allocations[index].value;
    a.steady_ns=SteadyNs();a.thread_id=std::uint32_t(pthread_mach_thread_np(pthread_self()));
    a.producer_sequence=latest_entry.sequence;a.entry_rsp=rsp;a.return_ip=ret;
    a.descriptor=saved[5];a.output=saved[4];a.gx_context=saved[3];a.requested_bytes=std::uint32_t(saved[2]);
    std::array<std::uint64_t,3> descriptor{};
    if(Read(a.descriptor,descriptor)) {
        a.read_mask|=1;a.resource=descriptor[0];
        a.capacity=std::uint32_t(descriptor[1]);a.alignment=std::uint32_t(descriptor[1]>>32);
        a.offset=std::uint32_t(descriptor[2]);a.last_offset=std::uint32_t(descriptor[2]>>32);
        if(Read(a.resource,a.resource_bytes)) a.read_mask|=2;
    }
    // Exact native discard path:2169c30 passes GX+0x10 to26d0d30,
    // which loads [writer+0xb490]->+8 and calls that provider's vtable+0x18.
    if(Field(a.gx_context,0xb4a0,a.provider_manager) && Field(a.provider_manager,8,a.provider) &&
       Read(a.provider,a.provider_vtable) && Field(a.provider_vtable,0x18,a.provider_allocate)) a.read_mask|=4;
    SnapshotArena(a.provider,a.arena);
    latest_allocation={index+1,rsp-8,ret,a.output,a.producer_sequence,a.provider,a.resource};
    s->allocations[index].ready.store(true,std::memory_order_release);
    return 0;
}
// Custom result stub saves all integer registers and RFLAGS. Unlike an entry
// hook, this point has RSP%16==0 and a live EAX result. saved[8] is original
// RAX; __builtin_frame_address recovers the unchanged guest allocation RBP.
__attribute__((noinline)) GUEST_ABI void ObserveAllocationResult(const std::uint64_t* saved) noexcept {
    State* s=state.load(std::memory_order_acquire);
    if(!s || !s->allocation_enabled || !saved || !latest_allocation.sequence) return;
    CaptureScope active(*s);if(!active.entered) return;
    std::uint64_t rbp=0,ret=0;
    if(!Read(reinterpret_cast<std::uint64_t>(__builtin_frame_address(0)),rbp) ||
       rbp!=latest_allocation.expected_rbp || !Field(rbp,8,ret) || ret!=latest_allocation.return_ip) return;
    const auto link=latest_allocation;latest_allocation={};
    if(s->allocation_result_next.load(std::memory_order_relaxed)>=kLimit) return;
    const auto index=s->allocation_result_next.fetch_add(1,std::memory_order_relaxed);if(index>=kLimit) return;
    auto& r=s->allocation_results[index].value;
    r.steady_ns=SteadyNs();r.thread_id=std::uint32_t(pthread_mach_thread_np(pthread_self()));
    r.allocation_sequence=link.sequence;r.producer_sequence=link.producer;r.status=std::uint32_t(saved[8]);
    r.output_readable=Read(link.output,r.output_value);
    r.resource_readable=Read(link.resource,r.resource_bytes);
    // Keep ordinary observations cheap. Traversal happens only after a real
    // failure, for the first16 failures in this bounded run.
    const bool detailed=r.status!=0 && s->failure_details.fetch_add(1,std::memory_order_relaxed)<16;
    SnapshotArena(link.provider,r.arena,detailed);
    s->allocation_results[index].ready.store(true,std::memory_order_release);
}
bool InstallAllocationResult(ElfImage* image) {
    auto* code=static_cast<std::uint8_t*>(host_page_alloc(4096,true));if(!code) return false;
    auto* c=code;
    const auto emit=[&](const auto& bytes){std::memcpy(c,bytes,sizeof(bytes));c+=sizeof(bytes);};
    const auto quad=[&](std::uint64_t value){std::memcpy(c,&value,8);c+=8;};
    // Original RSP%16==0. Ten pushes preserve alignment before the host call.
    // The host ABI preserves RBX/RBP/R12..15; the stub preserves remaining
    // GPRs, original integer return and flags. This native helper returns int.
    constexpr std::uint8_t save[]{0x9c,0x50,0x57,0x56,0x52,0x51,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53};
    constexpr std::uint8_t arg[]{0x48,0x89,0xe7,0x48,0xb8}; // mov rdi,rsp; movabs rax,host
    constexpr std::uint8_t call[]{0xff,0xd0};
    constexpr std::uint8_t restore[]{0x41,0x5b,0x41,0x5a,0x41,0x59,0x41,0x58,0x59,0x5a,0x5e,0x5f,0x58,0x9d};
    constexpr std::uint8_t compare[]{0x49,0x8b,0x4d,0x00,0x48,0x3b,0x4d,0xd0,0x74,0x0e};
    constexpr std::uint8_t jump[]{0xff,0x25,0,0,0,0};
    constexpr std::uint8_t stackadd[]{0x48,0x83,0xc4,0x28};
    emit(save);emit(arg);quad(reinterpret_cast<std::uint64_t>(&ObserveAllocationResult));emit(call);emit(restore);
    // Replay the original canary branch, relocating its short displacement.
    // The success path then performs the original stack add and resumes pops.
    emit(compare);emit(jump);quad(image->mem.slide+0x26db2ed);
    emit(stackadd);emit(jump);quad(image->mem.slide+0x26db2e2);
    const auto at=image->mem.slide+kAllocationResultRva;
    if(!guest_protect_rwx(&image->mem,at,sizeof(kAllocationResult))) {host_page_free(code,4096);return false;}
    auto* target=static_cast<std::uint8_t*>(guest_ptr(image->mem,at));
    std::memcpy(target,jump,sizeof(jump));const auto address=reinterpret_cast<std::uint64_t>(code);
    std::memcpy(target+sizeof(jump),&address,8);
    // Installed code is process lifetime, including if the diagnostic disarms.
    return guest_protect_rx(&image->mem,at,sizeof(kAllocationResult));
}
bool Inside(const std::filesystem::path& path,const std::filesystem::path& parent) {
    const auto relative=path.lexically_relative(parent);
    return path==parent || (!relative.empty() && *relative.begin()!="..");
}
bool RootAllowed(const std::filesystem::path& path) {
    if(!path.is_absolute()) return false;
    std::error_code error;std::filesystem::path prefix;
    for(const auto& component:path.lexically_normal()) {
        prefix/=component;
        const auto status=std::filesystem::symlink_status(prefix,error);
        if(error && error!=std::errc::no_such_file_or_directory) return false;
        error.clear();if(std::filesystem::is_symlink(status)) return false;
    }
    const auto root=std::filesystem::weakly_canonical(path,error);if(error) return false;
    bool game_known=false;
    for(const char* name:{"BB_SOURCE_GAME","BB_GAME_DIR"})
        if(const char* value=std::getenv(name);value && *value) {
            const auto game=std::filesystem::weakly_canonical(value,error);
            if(error || Inside(root,game)) return false;
            game_known=true;
        }
    return game_known;
}
std::string Token(const std::string& path) {
    const int fd=::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0) return {};
    struct stat status{};char bytes[128]{};ssize_t size=0;
    if(!::fstat(fd,&status) && S_ISREG(status.st_mode)) size=::read(fd,bytes,sizeof(bytes));
    ::close(fd);
    return size>0 ? std::string(bytes,static_cast<std::size_t>(size)) : std::string{};
}
bool AddressOption(const char* name,std::uint64_t& address) {
    const char* value=std::getenv(name);if(!value || !*value) return true;
    if(value[0]=='0' && (value[1]=='x' || value[1]=='X')) value+=2;
    std::uint64_t parsed=0;unsigned digits=0;
    for(;*value;++value) {
        const unsigned digit=*value>='0' && *value<='9' ? unsigned(*value-'0') :
            *value>='a' && *value<='f' ? unsigned(*value-'a'+10) :
            *value>='A' && *value<='F' ? unsigned(*value-'A'+10) : 16;
        if(digit>=16 || ++digits>16 || parsed>(UINT64_MAX-digit)/16) return false;
        parsed=parsed*16+digit;
    }
    if(!digits || !parsed) return false;
    address=parsed;return true;
}

// Called directly by the current Mac prologue thunk, which preserves rbp.
// __builtin_frame_address(0) establishes a host frame; its saved rbp is the
// original guest caller frame because the stub never installs its own rbp.
// The source is also compiled with -fno-omit-frame-pointer. Never inline it.
__attribute__((noinline)) GUEST_ABI std::int64_t Observe(std::uint64_t,const std::uint64_t* saved) noexcept {
    State* s=state.load(std::memory_order_acquire);
    if(!s || !s->armed.load(std::memory_order_acquire) || !saved) return 0;
    // These six words are the trusted stub's own register-save area, not
    // memory addressed by a guest argument. saved[6] is read safely below.
    if(std::uint32_t(saved[4])<2 || std::uint32_t(saved[4])>16 ||
        s->next.load(std::memory_order_relaxed)>=kLimit) return 0;
    CaptureScope active(*s);if(!active.entered) return 0;
    const auto index=s->next.fetch_add(1,std::memory_order_relaxed);
    if(index>=kLimit) return 0;
    Record& r=s->slots[index].value;
    r.monotonic_ns=host_clock_monotonic_ns();r.steady_ns=SteadyNs();
    r.thread_id=std::uint32_t(pthread_mach_thread_np(pthread_self()));
    r.count=std::uint32_t(saved[4]);r.writer=saved[5];
    r.entry_rsp=reinterpret_cast<std::uint64_t>(saved+6);
    r.return_readable=Read(r.entry_rsp,r.return_ip);
    r.rbp_readable=Read(reinterpret_cast<std::uint64_t>(__builtin_frame_address(0)),r.entry_rbp);
    std::array<std::uint64_t,6> command{};
    r.writer_readable=Read(r.writer,command);
    if(r.writer_readable) {
        r.buffer_start=command[0];r.buffer_end=command[1];r.cursor=command[2];
        r.reserve_callback=command[3];r.callback_data=command[4];r.flags=std::uint32_t(command[5]);
        r.capacity_valid=r.cursor>=r.buffer_start && r.cursor<=r.buffer_end &&
            !(r.cursor&3) && !(r.buffer_end&3);
        if(r.capacity_valid) r.remaining_dwords=(r.buffer_end-r.cursor)/4;
    }
    if(r.return_readable && r.return_ip>=s->slide && r.writer>=0x10 &&
        (r.return_ip-s->slide==0x2169753 || r.return_ip-s->slide==0x21698d4))
        r.gx_context=r.writer-0x10;
    const auto high=r.entry_rsp<=UINT64_MAX-kStackWindow ? r.entry_rsp+kStackWindow : UINT64_MAX;
    std::uint64_t frame=r.entry_rbp;
    while(r.rbp_readable && r.frame_count<kDepth) {
        if(!frame) {r.stop=5;break;}
        if((frame&7) || frame<r.entry_rsp || frame>high-16) {r.stop=1;break;}
        std::array<std::uint64_t,2> words{};
        if(!Read(frame,words)) {r.stop=2;break;}
        r.frames[r.frame_count++]={frame,words[1]};
        if(!Code(*s,words[1])) {r.stop=4;break;}
        if(words[0]<=frame) {r.stop=3;break;}
        frame=words[0];
    }
    if(s->producer_enabled) {
        const auto entry=latest_entry;
        for(unsigned n=1;entry.sequence && n<r.frame_count;++n)
            if(r.frames[n].rbp==entry.expected_rbp && r.frames[n].return_ip==entry.return_ip &&
               ProducerPC(*s,r.frames[n-1].return_ip)) {
                r.producer_entry_sequence=entry.sequence;break;
            }
    }
    s->slots[index].ready.store(true,std::memory_order_release);
    return 0; // Always execute the exact displaced prologue and original code.
}

void Address(std::FILE* file,const char* name,std::uint64_t address) {
    std::fprintf(file,"\"%s\":\"0x%llx\",",name,static_cast<unsigned long long>(address));
}
void Return(std::FILE* file,const State& s,std::uint64_t address) {
    if(Code(s,address)) std::fprintf(file,"\"0x%llx\"",static_cast<unsigned long long>(address-s.slide));
    else std::fputs("null",file);
}
void Write(State& s,std::size_t index,const Record& r) {
    auto* f=s.output;
    // Keep the first capture's JSON kind for reader compatibility. The count
    // field is authoritative: particles may have8,9,14,15...live vertices.
    std::fprintf(f,"{\"kind\":\"native-count8-draw\",\"sequence\":%zu,\"monotonic_ns\":%llu,"
        "\"steady_ns\":%llu,\"thread_id\":%u,\"count\":%u,",index+1,
        static_cast<unsigned long long>(r.monotonic_ns),static_cast<unsigned long long>(r.steady_ns),r.thread_id,r.count);
    Address(f,"writer_object",r.writer);Address(f,"gx_context",r.gx_context);
    Address(f,"entry_rsp",r.entry_rsp);Address(f,"entry_rbp",r.entry_rbp);Address(f,"return_ip",r.return_ip);
    Address(f,"buffer_start",r.buffer_start);Address(f,"buffer_end",r.buffer_end);Address(f,"command_cursor",r.cursor);
    Address(f,"reserve_callback",r.reserve_callback);Address(f,"callback_data",r.callback_data);
    std::fprintf(f,"\"writer_readable\":%s,\"capacity_valid\":%s,\"remaining_dwords\":%llu,\"command_flags\":%u,"
        "\"reserve_required\":%s,\"return_readable\":%s,\"rbp_readable\":%s,\"return_rva\":",
        r.writer_readable?"true":"false",r.capacity_valid?"true":"false",
        static_cast<unsigned long long>(r.remaining_dwords),r.flags,
        r.capacity_valid && r.remaining_dwords<=4?"true":"false",
        r.return_readable?"true":"false",r.rbp_readable?"true":"false");
    Return(f,s,r.return_ip);std::fputs(",\"caller_frames\":[",f);
    for(unsigned n=0;n<r.frame_count;++n) {
        if(n) std::fputc(',',f);
        std::fputc('{',f);Address(f,"rbp",r.frames[n].rbp);Address(f,"return_ip",r.frames[n].return_ip);
        std::fputs("\"return_rva\":",f);Return(f,s,r.frames[n].return_ip);std::fputc('}',f);
    }
    std::fprintf(f,"],\"stack_stop\":%u,\"producer_entry_sequence\":%llu}\n",r.stop,
        static_cast<unsigned long long>(r.producer_entry_sequence));
}
void WriteEntry(State& s,std::size_t index,const Entry& e) {
    auto* f=s.output;
    std::fprintf(f,"{\"kind\":\"native-fog-producer-entry\",\"sequence\":%zu,\"monotonic_ns\":%llu,"
        "\"steady_ns\":%llu,\"thread_id\":%u,\"arguments\":[",index+1,
        static_cast<unsigned long long>(e.monotonic_ns),static_cast<unsigned long long>(e.steady_ns),e.thread_id);
    for(unsigned n=0;n<6;++n)
        std::fprintf(f,"%s\"0x%llx\"",n?",":"",static_cast<unsigned long long>(e.arguments[n]));
    std::fputs("],",f);Address(f,"entry_rsp",e.entry_rsp);Address(f,"return_ip",e.return_ip);
    Address(f,"stamp_object",e.stamp_object);Address(f,"data_header",e.data_header);
    Address(f,"appearance_parameters",e.appearance_parameters);
    std::fprintf(f,"\"live_count\":%u,\"context_stamp\":%u,\"expected_stamp\":%u,"
        "\"appearance_selector\":%u,\"appearance_bits\":%u,\"read_mask\":%u,\"observed_gate\":%u}\n",
        e.live_count,e.context_stamp,e.expected_stamp,e.appearance_selector,e.appearance_bits,e.read_mask,e.observed_gate);
}
void WritePacket(State& s,std::size_t index,const Packet& p) {
    auto* f=s.output;
    std::fprintf(f,"{\"kind\":\"fog-draw-packet\",\"sequence\":%zu,\"steady_ns\":%llu,\"count\":%u,",
        index+1,static_cast<unsigned long long>(p.steady_ns),p.count);
    Address(f,"packet_address",p.address);Address(f,"ps",p.ps);Address(f,"vs",p.vs);
    std::fprintf(f,"\"cb0\":\"0x%llx\"}\n",static_cast<unsigned long long>(p.cb0));
}
void WriteArena(std::FILE* f,const ArenaSnapshot& a) {
    std::fputs("\"arena_snapshot\":{",f);
    Address(f,"arena",a.arena);Address(f,"begin",a.begin);Address(f,"end",a.end);Address(f,"free_head",a.free_head);
    std::fprintf(f,"\"outstanding\":%llu,\"free_bytes\":%llu,\"read_mask\":%u,\"free_stop\":%u,"
        "\"pending_heads\":[",static_cast<unsigned long long>(a.outstanding),
        static_cast<unsigned long long>(a.free_bytes),a.read_mask,a.free_stop);
    for(unsigned n=0;n<4;++n) std::fprintf(f,"%s\"0x%llx\"",n?",":"",static_cast<unsigned long long>(a.pending_heads[n]));
    std::fprintf(f,"],\"pending_stops\":[%u,%u,%u,%u]",a.pending_stops[0],a.pending_stops[1],a.pending_stops[2],a.pending_stops[3]);
    const auto nodes=[&](const char* name,const auto& values,unsigned count) {
        std::fprintf(f,",\"%s\":[",name);
        for(unsigned n=0;n<count;++n) {
            const auto& v=values[n];
            std::fprintf(f,"%s[\"0x%llx\",\"0x%llx\",\"0x%llx\",\"0x%llx\",\"0x%llx\",%u,%u]",
                n?",":"",static_cast<unsigned long long>(v.address),static_cast<unsigned long long>(v.status_word),
                static_cast<unsigned long long>(v.start),static_cast<unsigned long long>(v.end),
                static_cast<unsigned long long>(v.next),v.status,v.read_mask);
        }
        std::fputc(']',f);
    };
    nodes("free_nodes",a.free_nodes,a.free_count);nodes("pending_nodes",a.pending_nodes,a.pending_count);
    std::fputc('}',f);
}
void WriteAllocation(State& s,std::size_t index,const Allocation& a) {
    auto* f=s.output;
    std::fprintf(f,"{\"kind\":\"fog-allocation-entry\",\"sequence\":%zu,\"steady_ns\":%llu,"
        "\"thread_id\":%u,\"producer_entry_sequence\":%llu,\"requested_bytes\":%u,",
        index+1,static_cast<unsigned long long>(a.steady_ns),a.thread_id,
        static_cast<unsigned long long>(a.producer_sequence),a.requested_bytes);
    Address(f,"entry_rsp",a.entry_rsp);Address(f,"return_ip",a.return_ip);
    Address(f,"descriptor",a.descriptor);Address(f,"output",a.output);
    Address(f,"gx_context",a.gx_context);Address(f,"resource",a.resource);
    Address(f,"provider_manager",a.provider_manager);Address(f,"provider",a.provider);
    Address(f,"provider_vtable",a.provider_vtable);Address(f,"provider_allocate",a.provider_allocate);
    std::fprintf(f,"\"capacity\":%u,\"alignment\":%u,\"offset\":%u,\"last_offset\":%u,"
        "\"read_mask\":%u,\"resource_bytes\":\"",a.capacity,a.alignment,a.offset,a.last_offset,a.read_mask);
    for(auto byte:a.resource_bytes) std::fprintf(f,"%02x",unsigned(byte));
    std::fputs("\",",f);WriteArena(f,a.arena);std::fputs("}\n",f);
}
void WriteAllocationResult(State& s,std::size_t index,const AllocationResult& r) {
    auto* f=s.output;
    std::fprintf(f,"{\"kind\":\"fog-allocation-result\",\"sequence\":%zu,\"steady_ns\":%llu,"
        "\"thread_id\":%u,\"allocation_sequence\":%llu,\"producer_entry_sequence\":%llu,"
        "\"status\":%u,\"output_readable\":%s,\"output_value\":\"0x%llx\",\"resource_readable\":%s,\"resource_bytes\":\"",
        index+1,static_cast<unsigned long long>(r.steady_ns),r.thread_id,
        static_cast<unsigned long long>(r.allocation_sequence),static_cast<unsigned long long>(r.producer_sequence),
        r.status,r.output_readable?"true":"false",static_cast<unsigned long long>(r.output_value),r.resource_readable?"true":"false");
    for(auto byte:r.resource_bytes) std::fprintf(f,"%02x",unsigned(byte));
    std::fputs("\",",f);WriteArena(f,r.arena);std::fputs("}\n",f);
}
bool OutputRoom(const State& s) {
    const auto offset=std::ftell(s.output);return offset>=0 && offset<64l*1024*1024;
}
void Worker(State* s) noexcept {
    std::size_t written=0,entry_written=0,packet_written=0,allocation_written=0,result_written=0;
    try {
        // No GPU calls or waits, no file operations on the native draw path.
        // Only a changed nonempty token arms this one run; startup is ignored.
        while(true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const auto token=Token(s->trigger);
            if(!token.empty() && token!=s->initial_token) break;
        }
        const auto deadline=std::chrono::steady_clock::now()+kDuration;
        std::fprintf(s->output,"{\"kind\":\"armed\",\"monotonic_ns\":%llu,\"steady_ns\":%llu}\n",
            static_cast<unsigned long long>(host_clock_monotonic_ns()),static_cast<unsigned long long>(SteadyNs()));
        s->armed.store(true,std::memory_order_release);
        bool stopped=false;auto grace=deadline;
        for(;;) {
            const auto now=std::chrono::steady_clock::now();
            const auto claimed=std::min(s->next.load(std::memory_order_acquire),kLimit);
            const auto entry_claimed=std::min(s->entry_next.load(std::memory_order_acquire),kLimit);
            const auto packet_claimed=std::min(s->packet_next.load(std::memory_order_acquire),kLimit);
            const auto allocation_claimed=std::min(s->allocation_next.load(std::memory_order_acquire),kLimit);
            const auto result_claimed=std::min(s->allocation_result_next.load(std::memory_order_acquire),kLimit);
            while(written<claimed && s->slots[written].ready.load(std::memory_order_acquire)) {
                Write(*s,written,s->slots[written].value);++written;
            }
            while(entry_written<entry_claimed && s->entries[entry_written].ready.load(std::memory_order_acquire)) {
                WriteEntry(*s,entry_written,s->entries[entry_written].value);++entry_written;
            }
            while(packet_written<packet_claimed && s->packets[packet_written].ready.load(std::memory_order_acquire)) {
                WritePacket(*s,packet_written,s->packets[packet_written].value);++packet_written;
            }
            while(allocation_written<allocation_claimed && s->allocations[allocation_written].ready.load(std::memory_order_acquire) && OutputRoom(*s)) {
                WriteAllocation(*s,allocation_written,s->allocations[allocation_written].value);++allocation_written;
            }
            while(result_written<result_claimed && s->allocation_results[result_written].ready.load(std::memory_order_acquire) && OutputRoom(*s)) {
                WriteAllocationResult(*s,result_written,s->allocation_results[result_written].value);++result_written;
            }
            if(!stopped && (claimed>=kLimit || entry_claimed>=kLimit || packet_claimed>=kLimit ||
                allocation_claimed>=kLimit || result_claimed>=kLimit ||
                now>=deadline || std::ferror(s->output) || !OutputRoom(*s))) {
                s->armed.store(false,std::memory_order_release);stopped=true;grace=now+std::chrono::seconds(1);
                continue; // Resnapshot both high-water marks after disarming.
            }
            if(stopped && ((written==claimed && entry_written==entry_claimed && packet_written==packet_claimed &&
                allocation_written==allocation_claimed && result_written==result_claimed &&
                s->inflight.load(std::memory_order_acquire)==0) || now>=grace)) break;
            std::fflush(s->output);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        std::fprintf(s->output,"{\"kind\":\"complete\",\"records\":%zu,\"claimed\":%zu,"
            "\"producer_entries\":%zu,\"producer_claimed\":%zu,\"file_error\":%s}\n",
            written,std::min(s->next.load(std::memory_order_relaxed),kLimit),entry_written,
            std::min(s->entry_next.load(std::memory_order_relaxed),kLimit),std::ferror(s->output)?"true":"false");
        std::fprintf(s->output,"{\"kind\":\"packet-complete\",\"packets\":%zu,\"claimed\":%zu}\n",
            packet_written,std::min(s->packet_next.load(std::memory_order_relaxed),kLimit));
        std::fprintf(s->output,"{\"kind\":\"allocation-complete\",\"entries\":%zu,\"results\":%zu,"
            "\"entry_claimed\":%zu,\"result_claimed\":%zu}\n",allocation_written,result_written,
            std::min(s->allocation_next.load(std::memory_order_relaxed),kLimit),
            std::min(s->allocation_result_next.load(std::memory_order_relaxed),kLimit));
        host_log("Native fog draw capture completed: %zu count2..16 observations, %zu producer entries",written,entry_written);
    } catch(...) {
        s->armed.store(false,std::memory_order_release);
        host_log("Native fog draw capture stopped: diagnostic worker failed");
    }
    std::fclose(s->output);s->output=nullptr;
}
}

void ObserveFogDrawPacket(std::uint64_t packet,std::uint32_t count,std::uint64_t ps,
                         std::uint64_t vs,std::uint64_t cb0) noexcept {
    State* s=state.load(std::memory_order_acquire);
    if(!s || !s->armed.load(std::memory_order_acquire)) return;
    // Addresses are diagnostic identities verified for this scene. Filtering
    // them only limits observations; no command execution depends on this.
    if(ps!=s->packet_ps || cb0!=s->packet_cb0 ||
        s->packet_next.load(std::memory_order_relaxed)>=kLimit) return;
    CaptureScope active(*s);if(!active.entered) return;
    const auto index=s->packet_next.fetch_add(1,std::memory_order_relaxed);if(index>=kLimit) return;
    s->packets[index].value={SteadyNs(),packet,ps,vs,cb0,count};
    s->packets[index].ready.store(true,std::memory_order_release);
}

void InstallFogDrawCapture(ElfImage* image) {
    const char* directory=std::getenv("BBHOST_FOG_DRAW_CAPTURE_DIR");
    const char* trigger=std::getenv("DC_TRACE_FILE");
    if(!directory || !*directory || !trigger || !*trigger || !image ||
        !engine_eboot_supported(image->sha256) || state.load(std::memory_order_acquire)) return;
    try {
        const char* entry_flag=std::getenv("BBHOST_FOG_ENTRY_CAPTURE");
        const bool producer_on=entry_flag && !std::strcmp(entry_flag,"1");
        const char* allocation_flag=std::getenv("BBHOST_FOG_ALLOCATION_CAPTURE");
        const bool allocation_on=allocation_flag && !std::strcmp(allocation_flag,"1");
        if(allocation_on && !producer_on) {
            host_log("Native fog allocation capture refused: producer capture is required");return;
        }
        std::uint64_t packet_ps=0x80d537b100ull,packet_cb0=0x806b240000ull;
        if(!AddressOption("BBHOST_FOG_CAPTURE_PS",packet_ps) || !AddressOption("BBHOST_FOG_CAPTURE_CB0",packet_cb0)) {
            host_log("Native fog capture refused: packet PS/CB0 overrides require nonzero hexadecimal addresses");return;
        }
        const std::filesystem::path root=std::filesystem::path(directory).lexically_normal();
        if(!RootAllowed(root) || !std::filesystem::path(trigger).is_absolute()) {
            host_log("Native fog draw capture refused: absolute nonsymlink output outside known game roots required");return;
        }
        const auto matches=[&](std::uint64_t rva,const auto& expected) {
            std::array<std::uint8_t,sizeof(expected)> bytes{};
            return rva<=image->mem.size && sizeof(expected)<=image->mem.size-rva &&
                image->mem.slide<=UINT64_MAX-rva && Read(image->mem.slide+rva,bytes) &&
                !std::memcmp(bytes.data(),expected,sizeof(expected));
        };
        // Validate both original prologues before changing either code site.
        if(!matches(kWriterRva,kPrologue) || (producer_on && !matches(kProducerRva,kProducerPrologue)) ||
           (allocation_on && (!matches(kAllocationRva,kAllocationPrologue) || !matches(kAllocationResultRva,kAllocationResult)))) {
            host_log("Native fog draw capture refused: exact writer/producer/allocation bytes differ");return;
        }
        const auto run=root/(std::to_string(host_clock_monotonic_ns())+"-"+std::to_string(::getpid()));
        std::filesystem::create_directories(root);
        if(!RootAllowed(root) || !std::filesystem::create_directory(run) || !RootAllowed(run)) return;
        const auto path=run/"native-fog-draws.jsonl";
        const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        if(fd<0) return;
        auto* output=::fdopen(fd,"w");if(!output) {::close(fd);return;}
        auto* s=new State;s->output=output;s->slide=image->mem.slide;s->producer_enabled=producer_on;
        s->allocation_enabled=allocation_on;
        s->packet_ps=packet_ps;s->packet_cb0=packet_cb0;
        s->trigger=trigger;s->initial_token=Token(s->trigger);
        for(const auto& segment:image->segments) if((segment.flags&1) && segment.memsz &&
            s->executable_count<s->executable.size() && segment.va<=UINT64_MAX-s->slide &&
            segment.memsz<=UINT64_MAX-(s->slide+segment.va)) {
            const auto start=s->slide+segment.va;
            s->executable[s->executable_count++]={start,start+segment.memsz};
        }
        const auto restore=[&](std::uint64_t rva,const auto& expected) {
            const auto at=image->mem.slide+rva;
            if(!guest_protect_rwx(&image->mem,at,sizeof(expected))) return false;
            std::memcpy(guest_ptr(image->mem,at),expected,sizeof(expected));
            return guest_protect_rx(&image->mem,at,sizeof(expected));
        };
        const auto rollback=[&] {
            bool ok=restore(kWriterRva,kPrologue);
            if(producer_on) ok=restore(kProducerRva,kProducerPrologue) && ok;
            if(allocation_on) {
                ok=restore(kAllocationRva,kAllocationPrologue) && ok;
                ok=restore(kAllocationResultRva,kAllocationResult) && ok;
            }
            if(!ok) host_log("Native fog draw capture: original-byte protection restore failed");
        };
        bool installed=false;
        if(s->executable_count) {
            installed=engine_prologue_hook(image,s->slide+kWriterRva,kPrologue,sizeof(kPrologue),
                reinterpret_cast<void*>(&Observe));
            if(installed && producer_on)
                installed=engine_prologue_hook(image,s->slide+kProducerRva,kProducerPrologue,sizeof(kProducerPrologue),
                    reinterpret_cast<void*>(&ObserveProducer));
            if(installed && allocation_on) {
                installed=engine_prologue_hook(image,s->slide+kAllocationRva,kAllocationPrologue,sizeof(kAllocationPrologue),
                    reinterpret_cast<void*>(&ObserveAllocation));
                if(installed) installed=InstallAllocationResult(image);
            }
            if(!installed) rollback();
        }
        if(!installed) {
            std::fclose(output);delete s;host_log("Native fog draw capture refused: checked hook unavailable");return;
        }
        std::fprintf(output,"{\"kind\":\"config\",\"version\":1,\"writer_rva\":\"0x10741b0\","
            "\"image_sha256\":\"%s\",\"slide\":\"0x%llx\",\"record_limit\":4096,\"stack_depth\":12,"
            "\"stack_window_bytes\":1048576,\"capture_seconds\":30,\"producer_enabled\":%s,"
            "\"producer_rva\":\"0x28ec760\",\"producer_limit\":4096,\"allocation_enabled\":%s,"
            "\"allocation_rva\":\"0x26db1b0\",\"allocation_result_rva\":\"0x26db2d4\",\"allocation_limit\":4096,"
            "\"file_limit_bytes\":67108864,\"arena_free_node_limit\":32,\"arena_pending_node_limit\":32,"
            "\"arena_detailed_failure_limit\":16,\"arena_scope\":\"cheap header before/after; detailed unsynchronized lists after first16 nonzero returns only\","
            "\"arena_node_columns\":[\"address\",\"status_word\",\"start\",\"end\",\"next\",\"resolved_status\",\"read_mask\"],"
            "\"arena_stop_codes\":[\"end\",\"limit\",\"unreadable\",\"cycle\",\"unexpected_null\"],"
            "\"packet_limit\":4096,\"packet_ps_filter\":\"0x%llx\",\"packet_cb0_filter\":\"0x%llx\","
            "\"scope\":\"count2..16 writers, optional producer entries and selected CP packet identities; shared horizon ends at any4096 limit or30s; original execution preserved\"}\n",
            image->sha256.c_str(),static_cast<unsigned long long>(s->slide),producer_on?"true":"false",allocation_on?"true":"false",
            static_cast<unsigned long long>(s->packet_ps),static_cast<unsigned long long>(s->packet_cb0));
        std::fflush(output);state.store(s,std::memory_order_release);
        try { std::thread(Worker,s).detach(); }
        catch(...) {state.store(nullptr,std::memory_order_release);rollback();std::fclose(output);s->output=nullptr;throw;}
        host_log("Native fog draw capture waiting for DC_TRACE_FILE change: %s (4096 records/30s, producer%s)",
            path.c_str(),producer_on?" on":" off");
    } catch(...) {host_log("Native fog draw capture unavailable: diagnostic setup failed");}
}
}
