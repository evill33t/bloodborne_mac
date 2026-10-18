// SPDX-License-Identifier: GPL-2.0-or-later
#include "src/checkpoint/import_proxy.h"
#include <cassert>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <immintrin.h>
#include <mutex>
#include <stdexcept>
#include <sys/mman.h>
#include <thread>
#include <vector>

namespace {
using namespace Bb::Checkpoint;
struct Event {BoundaryKind kind;std::uint64_t id,sp,pc,rax;};
struct Observations {std::mutex mutex;std::vector<Event> events;};
void Observe(void* context,BoundaryKind kind,const ImportProxy& import,const BbCheckpointCpu& cpu) {
    auto& state=*static_cast<Observations*>(context);
    assert(cpu.features==1 && !cpu.reserved[0] && !cpu.reserved[1] && !cpu.reserved[2]);
    if(kind==BoundaryKind::BeforeImport)assert(cpu.rip==std::uintptr_t(import.code));
    {std::lock_guard lock(state.mutex);state.events.push_back({kind,import.identity,cpu.gpr[BB_CK_RSP],cpu.rip,cpu.gpr[BB_CK_RAX]});}
    // Deliberately disturb the host floating environment and vector registers.
    // The target's input and the guest's returned state must remain intact.
    _mm_setcsr(0x1f80);volatile double noise=1.2345;noise=noise*4.5;(void)noise;
    assert(ImportDepth()==(kind==BoundaryKind::BeforeImport ? 0 : 1));
}
__attribute__((noinline)) std::uint64_t Integers(std::uint64_t a,std::uint64_t b,std::uint64_t c,std::uint64_t d,
    std::uint64_t e,std::uint64_t f,std::uint64_t g,std::uint64_t h,std::uint64_t i,std::uint64_t j,std::uint64_t k,std::uint64_t l) {
    return a+2*b+3*c+4*d+5*e+6*f+7*g+8*h+9*i+10*j+11*k+12*l;
}
__attribute__((noinline)) double Variadic(int count,...) {
    va_list args;va_start(args,count);double result=0;
    for(int k=0;k<count;++k)result+=(k+1)*va_arg(args,double);
    va_end(args);return result;
}
__attribute__((noinline)) __m256 Vector(__m256 value,float bias) {return _mm256_add_ps(value,_mm256_set1_ps(bias));}
struct Pair {std::uint64_t a,b;};
__attribute__((noinline)) Pair Aggregate(std::uint64_t a,std::uint64_t b) {return {a*17,b*23};}
__attribute__((noinline)) long double Extended(long double a,long double b) {return a*2+b*3;}
std::uint64_t (*inner)(std::uint64_t)=nullptr;
__attribute__((noinline)) std::uint64_t Inner(std::uint64_t value) {return value*7;}
__attribute__((noinline)) std::uint64_t Outer(std::uint64_t value) {return inner(value)+3;}
std::atomic<unsigned> attempts=0;
__attribute__((noinline)) std::uint64_t Retrying(std::uint64_t a,std::uint64_t b) {
    if(attempts.fetch_add(1)==0) {assert(ReplayCurrentImport());return 99999;}
    return a*19+b*23;
}
void Throw(void*,BoundaryKind,const ImportProxy&,const BbCheckpointCpu&) {throw std::runtime_error("fixture failure");}
}
int main() {
    using namespace Bb::Checkpoint;
    // Match the production executable: native return PCs are above the
    // guest's 40-bit address space, independent of the proxy mapping address.
    assert(reinterpret_cast<std::uintptr_t>(&Integers)>=(1ull<<40));
    constexpr std::size_t Bytes=16384;
    auto* code=static_cast<std::uint8_t*>(mmap(nullptr,Bytes,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0));
    assert(code!=MAP_FAILED);Observations state;
    const std::vector<void*> targets{reinterpret_cast<void*>(&Integers),reinterpret_cast<void*>(&Variadic),
        reinterpret_cast<void*>(&Vector),reinterpret_cast<void*>(&Aggregate),reinterpret_cast<void*>(&Extended),
        reinterpret_cast<void*>(&Outer),reinterpret_cast<void*>(&Inner),reinterpret_cast<void*>(&Inner),reinterpret_cast<void*>(&Retrying)};
    std::vector<ImportProxy> descriptors(targets.size());
    for(std::size_t k=0;k<targets.size();++k) {
        descriptors[k]={targets[k],k+1,nullptr,k==7 ? &Throw : &Observe,&state};
        assert(EmitImportProxy({code+k*32,32},descriptors[k]));
    }
    descriptors[8].replayable=true;
    assert(!EmitImportProxy({code+4096,16},descriptors[0]));
    assert(!mprotect(code,Bytes,PROT_READ|PROT_EXEC));
    const auto integers=reinterpret_cast<decltype(&Integers)>(code);
    assert(integers(1,2,3,4,5,6,7,8,9,10,11,12)==650);
    const auto variadic=reinterpret_cast<decltype(&Variadic)>(code+32);
    assert(variadic(9,1.0,2.0,3.0,4.0,5.0,6.0,7.0,8.0,9.0)==285.0);
    assert(state.events[2].id==2 && (state.events[2].rax&255)==8); // SysV AL vector count
    const auto vector=reinterpret_cast<decltype(&Vector)>(code+64);
    alignas(32) float values[8]={1,2,3,4,5,6,7,8},actual[8]{};
    _mm256_store_ps(actual,vector(_mm256_load_ps(values),0.5f));
    for(unsigned k=0;k<8;++k)assert(actual[k]==values[k]+0.5f);
    const auto aggregate=reinterpret_cast<decltype(&Aggregate)>(code+96);
    const auto pair=aggregate(19,29);assert(pair.a==323 && pair.b==667);
    const auto extended=reinterpret_cast<decltype(&Extended)>(code+128);
    assert(extended(1.25L,2.5L)==10.0L);
    inner=reinterpret_cast<decltype(inner)>(code+192);
    const auto outer=reinterpret_cast<decltype(&Outer)>(code+160);
    assert(outer(5)==38 && ImportDepth()==0);
    assert(state.events.size()==12 && state.events[10].id==6 && state.events[11].id==6);
    for(std::size_t k=0;k<state.events.size();k+=2) {
        const auto& before=state.events[k];const auto& after=state.events[k+1];
        assert(before.kind==BoundaryKind::BeforeImport && after.kind==BoundaryKind::AfterImport &&
               before.id==after.id && after.sp==before.sp+8 && after.pc!=before.pc);
    }
    std::thread first([&]{for(unsigned k=0;k<100;++k)assert(outer(k)==k*7+3);assert(!ImportDepth());});
    std::thread second([&]{for(unsigned k=0;k<100;++k)assert(outer(k)==k*7+3);assert(!ImportDepth());});
    first.join();second.join();assert(ImportProxiesHealthy());
    const auto callbacks_before=state.events.size();
    EnterNativeGuestCallback();EnterNativeGuestCallback();
    assert(outer(5)==38 && !ImportDepth());LeaveNativeGuestCallback();
    assert(outer(5)==38 && !ImportDepth());LeaveNativeGuestCallback();
    assert(state.events.size()==callbacks_before);
    assert(outer(5)==38 && state.events.size()==callbacks_before+2);
    const auto host_events=state.events.size();descriptors[5].guest_only=true;
    assert(outer(5)==38 && state.events.size()==host_events);descriptors[5].guest_only=false;
    std::atomic<bool> requested=false;
    for(unsigned k:{0u,1u,2u,4u}){descriptors[k].requested=&requested;descriptors[k].leaf=true;}
    const auto leaf_events=state.events.size();
    assert(integers(1,2,3,4,5,6,7,8,9,10,11,12)==650);
    assert(variadic(9,1.0,2.0,3.0,4.0,5.0,6.0,7.0,8.0,9.0)==285.0);
    _mm256_store_ps(actual,vector(_mm256_load_ps(values),0.5f));
    for(unsigned k=0;k<8;++k)assert(actual[k]==values[k]+0.5f);
    assert(extended(1.25L,2.5L)==10.0L && state.events.size()==leaf_events);
    requested.store(true);
    assert(integers(1,2,3,4,5,6,7,8,9,10,11,12)==650 && state.events.size()==leaf_events+2);
    requested.store(false);
    assert(!ReplayCurrentImport());
    const auto retrying=reinterpret_cast<decltype(&Retrying)>(code+256);
    const auto old_events=state.events.size();
    assert(retrying(13,17)==638 && attempts==2 && !ImportDepth());
    assert(state.events.size()==old_events+3 && state.events[old_events].kind==BoundaryKind::BeforeImport &&
           state.events[old_events+1].kind==BoundaryKind::BeforeImport && state.events.back().kind==BoundaryKind::AfterImport);
    // Callback failure rejects future checkpoints while preserving ordinary
    // call execution. No exception unwinds through the assembly/guest frame.
    const auto failing=reinterpret_cast<decltype(&Inner)>(code+224);
    assert(failing(9)==63 && !ImportProxiesHealthy() && !ImportDepth());
    assert(outer(7)==52 && !ImportDepth());
    assert(!munmap(code,Bytes));
    puts("Checkpoint import boundary: stack/register/variadic/AVX/x87 arguments, aggregate returns, nested callbacks, TLS isolation and failure passthrough passed");
}
