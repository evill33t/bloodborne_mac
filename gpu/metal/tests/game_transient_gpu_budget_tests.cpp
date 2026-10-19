// SPDX-License-Identifier: GPL-3.0-or-later
// CPU-only production-plan fixture: no game file, GPU or process hook required.
#include "gpu/metal/bbhost/transient_gpu_budget.h"
#include <cassert>
#include <cstdio>

namespace {
using namespace MacFeatures::TransientGpuBudget;
unsigned checks=0;
using Config=std::array<std::uint8_t,0x70>;
Config Original(std::uint32_t bytes=OriginalBytes) {
    Config c{};
    for(unsigned n=0;n<c.size();++n) c[n]=std::uint8_t(n*37+11);
    std::memcpy(c.data()+BudgetOffset,&bytes,sizeof(bytes));return c;
}
void Check(bool value) {assert(value);++checks;}
void Refused(std::string_view hash,std::span<const std::uint8_t> code,
             std::uint64_t address,std::uint64_t mb,Config config=Original(),std::size_t bytes=ConfigBytes) {
    const auto before=config;
    Check(Prepare(hash,code,std::span(config.data(),bytes),address,mb)==Decision::Refuse);
    Check(config==before);
}
}
int main() {
    using namespace MacFeatures::TransientGpuBudget;
    constexpr auto hash="cec1b276e7f9e4db978e57f524f41fbaac594530a3437b002e23f3fab14b4f86";
    for(const auto supported:{std::string_view(hash),std::string_view(kEboot109Sha256)}) {
        auto c=Original(),before=c;
        Check(Prepare(supported,Prologue,c,0x80001000,160)==Decision::Original);Check(c==before);
        Check(Prepare(supported,Prologue,c,0x80001000,320)==Decision::Expanded);
        std::uint32_t budget=0;std::memcpy(&budget,c.data()+BudgetOffset,sizeof(budget));Check(budget==ExpandedBytes);
        for(unsigned n=0;n<c.size();++n)
            if(n<BudgetOffset || n>=BudgetOffset+sizeof(budget)) Check(c[n]==before[n]);
        const auto expanded=c;
        Check(Prepare(supported,Prologue,c,0x80001000,320)==Decision::AlreadyExpanded);Check(c==expanded);
    }
    for(const auto unsupported:{std::string_view{},std::string_view("unsupported"),
             std::string_view("1ec1b276e7f9e4db978e57f524f41fbaac594530a3437b002e23f3fab14b4f86")})
        Refused(unsupported,Prologue,0x80001000,320);
    // Every single-bit code mismatch must refuse before touching capacity.
    for(unsigned byte=0;byte<Prologue.size();++byte) for(unsigned bit=0;bit<8;++bit) {
        auto code=Prologue;code[byte]^=std::uint8_t(1u<<bit);Refused(hash,code,0x80001000,320);
    }
    for(unsigned size=0;size<Prologue.size();++size)
        Refused(hash,std::span(Prologue.data(),size),0x80001000,320);
    for(unsigned size=0;size<ConfigBytes;++size) Refused(hash,Prologue,0x80001000,320,Original(),size);
    for(const auto mb:{0ull,159ull,161ull,256ull,321ull,4096ull,0xffffffffffffffffull})
        Refused(hash,Prologue,0x80001000,mb);
    for(const auto address:{0ull,1ull,0x80001001ull,0xfffffffffffffffcull,0xffffffffffffffa8ull})
        Refused(hash,Prologue,address,320);
    for(const auto bytes:{0u,128u*1024*1024,256u*1024*1024,UINT32_MAX})
        Refused(hash,Prologue,0x80001000,320,Original(bytes));
    Refused(hash,Prologue,0x80001000,160,Original(ExpandedBytes));
    std::printf("Transient GPU budget production-plan: %u CPU checks passed\n",checks);
}
