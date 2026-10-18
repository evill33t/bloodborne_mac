// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic guest only: a fresh-process CPU/stack/memory checkpoint test. No
// game files, native pthread objects or saved host stack frames are involved.
#include "src/checkpoint/archive.h"
#include "src/checkpoint/cpu_x86_64.h"
#include "src/checkpoint/import_proxy.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

namespace {
using namespace Bb::Checkpoint;
constexpr std::uintptr_t Code=0x8001000000ull, Imports=0x8001004000ull;
constexpr std::uintptr_t Data=0x8001008000ull, Stack=0x8001010000ull;
constexpr std::size_t Page=16384, StackSize=65536;
std::filesystem::path path;
std::string identity;
enum class Mode {Direct,AfterImport,BeforeImport};
Mode mode=Mode::Direct;
bool restoring=false;
BbCheckpointCpu expected_cpu{};
ImportProxy import;
extern "C" const unsigned char bb_ck_fixture_begin[],bb_ck_fixture_end[];
extern "C" void bb_ck_fixture_enter(void*,void*) __attribute__((noreturn));

#define SYM(n) "_" n
#define VLOAD(n,off) "vmovdqu " #off "(%r15),%ymm" #n "\n"
#define VSTORE(n,off) "vmovdqu %ymm" #n "," #off "(%r15)\n"
__asm__(".text\n.p2align 4\n.globl " SYM("bb_ck_fixture_enter") "\n"
        SYM("bb_ck_fixture_enter") ":\nmov %rdi,%rax\nmov %rsi,%rsp\njmp *%rax\n"
        ".p2align 4\n.globl " SYM("bb_ck_fixture_begin") "\n"
        SYM("bb_ck_fixture_begin") ":\n"
        "movabs $0x8001008000,%r15\nsub $256,%rsp\n"
        "movabs $0x1122334455667788,%rbx\nmov %rbx,0(%rsp)\nmov %rbx,248(%rsp)\n"
        "movabs $0x1212121212121212,%r12\nmovabs $0x1313131313131313,%r13\n"
        "movabs $0x1414141414141414,%r14\nmovabs $0xabababababababab,%rbp\n"
        "movabs $0x2222222222222222,%rcx\nmovabs $0x3333333333333333,%rdx\n"
        "movabs $0x4444444444444444,%rsi\nmovabs $0x8888888888888888,%r8\n"
        "movabs $0x9999999999999999,%r9\nmovabs $0xaaaaaaaaaaaaaaaa,%r10\n"
        "movabs $0x8001004000,%r11\nmov %r15,%rdi\nmov $777,%eax\n"
        "fld1\nfldpi\n"
        VLOAD(0,8192) VLOAD(1,8224) VLOAD(2,8256) VLOAD(3,8288)
        VLOAD(4,8320) VLOAD(5,8352) VLOAD(6,8384) VLOAD(7,8416)
        VLOAD(8,8448) VLOAD(9,8480) VLOAD(10,8512) VLOAD(11,8544)
        VLOAD(12,8576) VLOAD(13,8608) VLOAD(14,8640) VLOAD(15,8672)
        "call *0(%r11)\n"
        // Only MOV/PUSH/POP before recording flags: preserve restored RFLAGS.
        "mov %rax,1024(%r15)\nmov %rbx,1032(%r15)\nmov %rcx,1040(%r15)\n"
        "mov %rdx,1048(%r15)\nmov %rsi,1056(%r15)\nmov %rdi,1064(%r15)\n"
        "mov %rbp,1072(%r15)\nmov %rsp,1080(%r15)\nmov %r8,1088(%r15)\n"
        "mov %r9,1096(%r15)\nmov %r10,1104(%r15)\nmov %r11,1112(%r15)\n"
        "mov %r12,1120(%r15)\nmov %r13,1128(%r15)\nmov %r14,1136(%r15)\n"
        "mov %r15,1144(%r15)\npushfq\npopq 1152(%r15)\nfxsave64 2048(%r15)\n"
        VSTORE(0,4096) VSTORE(1,4128) VSTORE(2,4160) VSTORE(3,4192)
        VSTORE(4,4224) VSTORE(5,4256) VSTORE(6,4288) VSTORE(7,4320)
        VSTORE(8,4352) VSTORE(9,4384) VSTORE(10,4416) VSTORE(11,4448)
        VSTORE(12,4480) VSTORE(13,4512) VSTORE(14,4544) VSTORE(15,4576)
        "movabs $0x8001004000,%r11\ntest %eax,%eax\njne 1f\ncall *8(%r11)\nud2\n"
        "1: addq $1,10000(%r15)\nsubq $1,10008(%r15)\ncall *16(%r11)\nud2\n"
        ".globl " SYM("bb_ck_fixture_end") "\n" SYM("bb_ck_fixture_end") ":\n");

void Check(bool ok,const char* message) {
    if(!ok){std::fprintf(stderr,"Checkpoint process test failed: %s\n",message);std::exit(1);}
}
std::uint64_t Hash(const unsigned char* bytes,std::size_t size) {
    std::uint64_t h=0xcbf29ce484222325ull;
    for(std::size_t k=0;k<size;++k)h=(h^bytes[k])*0x100000001b3ull;
    return h;
}
template<class T>T& At(std::uintptr_t address) {return *reinterpret_cast<T*>(address);}
std::size_t CodeSize() {return std::uintptr_t(bb_ck_fixture_end)-std::uintptr_t(bb_ck_fixture_begin);}
void* Map(std::uintptr_t address,std::size_t size) {
    void* memory=mmap(reinterpret_cast<void*>(address),size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    Check(memory==reinterpret_cast<void*>(address),"fixed guest mapping is unavailable");return memory;
}
Section Memory(std::uint64_t id,std::uintptr_t address,std::size_t size) {
    Section s;s.kind=SectionKind::MemoryBytes;s.id=id;s.bytes.resize(size);
    std::memcpy(s.bytes.data(),reinterpret_cast<void*>(address),size);return s;
}
[[noreturn]] void Saved() {
    auto& cpu=At<BbCheckpointCpu>(Data);
    Check(bb_checkpoint_cpu_valid(&cpu),"captured guest CPU context is structurally valid");
    Check((cpu.rip>=Code && cpu.rip<Code+CodeSize()) ||
          (mode==Mode::BeforeImport && cpu.rip==Imports+256),"saved PC belongs to guest code/import stub");
    Check(cpu.gpr[BB_CK_RSP]>=Stack+16 && cpu.gpr[BB_CK_RSP]<=Stack+StackSize,"saved SP belongs to guest stack");
    if(mode!=Mode::BeforeImport)cpu.gpr[BB_CK_RAX]=1;
    Archive a;a.identity=identity;
    Section context;context.kind=SectionKind::Cpu;context.bytes.resize(sizeof(cpu));
    std::memcpy(context.bytes.data(),&cpu,sizeof(cpu));a.sections.push_back(std::move(context));
    a.sections.push_back(Memory(1,Data,Page));
    // Host helper frames below the saved continuation are not guest state.
    auto stack=Memory(2,Stack,StackSize);
    std::fill(stack.bytes.begin(),stack.bytes.begin()+cpu.gpr[BB_CK_RSP]-Stack,0);
    a.sections.push_back(std::move(stack));
    std::string error;const bool saved=Save(path,a,error);Check(saved,error.c_str());
    // The live state diverges after saving. A restore must recover the file.
    At<std::uint64_t>(Data+10000)=999;At<std::uint64_t>(Data+10008)=0;
    std::memset(reinterpret_cast<void*>(Data+11000),0,64);
    std::puts("Fresh-process checkpoint: guest continuation saved; live memory then changed.");
    std::fflush(stdout);_exit(0);
}
[[noreturn]] void Restored() {
    auto& cpu=At<BbCheckpointCpu>(Data);
    const auto* registers=reinterpret_cast<const std::uint64_t*>(Data+1024);
    for(unsigned k=0;k<16;++k)Check(registers[k]==cpu.gpr[k],"GPR continuation mismatch");
    Check(At<std::uint64_t>(Data+1152)==cpu.rflags,"restored RFLAGS mismatch");
    Check(std::memcmp(reinterpret_cast<void*>(Data+2048),cpu.fx,416)==0,"x87/SSE/MXCSR state mismatch");
    Check(std::memcmp(reinterpret_cast<void*>(Data+4096),reinterpret_cast<void*>(Data+8192),512)==0,
          "AVX YMM0-15 state mismatch");
    Check(At<std::uint64_t>(cpu.gpr[BB_CK_RSP])==0x1122334455667788ull &&
          At<std::uint64_t>(cpu.gpr[BB_CK_RSP]+248)==0x1122334455667788ull,"guest stack locals mismatch");
    Check(At<std::uint64_t>(Data+10000)==42 && At<std::uint64_t>(Data+10008)==72,"guest state did not advance after restore");
    for(unsigned k=0;k<64;++k)Check(At<std::uint8_t>(Data+11000+k)==std::uint8_t(k*7),"guest material memory mismatch");
    std::puts("Fresh-process checkpoint: all GPRs, RFLAGS, x87/SSE/AVX, stack and guest memory restored; execution continued.");
    std::fflush(stdout);_exit(0);
}
int ReturnOne() {return 1;}
void ObserveImport(void*,BoundaryKind kind,const ImportProxy&,const BbCheckpointCpu& cpu) {
    if(!restoring && ((mode==Mode::AfterImport && kind==BoundaryKind::AfterImport) ||
                      (mode==Mode::BeforeImport && kind==BoundaryKind::BeforeImport))) {
        At<BbCheckpointCpu>(Data)=cpu;Saved();
    }
    if(restoring && mode==Mode::BeforeImport && kind==BoundaryKind::BeforeImport)
        Check(!std::memcmp(&cpu,&expected_cpu,sizeof(cpu)),"before-import CPU state differs after fresh-process restore");
    if(restoring && kind==BoundaryKind::AfterImport)At<BbCheckpointCpu>(Data)=cpu;
}
void Setup() {
    Map(Code,Page);Map(Imports,Page);Map(Data,Page);Map(Stack,StackSize);
    Check(CodeSize()<Page,"synthetic guest fits executable mapping");
    std::memcpy(reinterpret_cast<void*>(Code),bb_ck_fixture_begin,CodeSize());
    Check(!mprotect(reinterpret_cast<void*>(Code),Page,PROT_READ|PROT_EXEC),"guest execute protection");
    // These host addresses are freshly bound after every process restart.
    At<std::uintptr_t>(Imports)=reinterpret_cast<std::uintptr_t>(&bb_checkpoint_cpu_save);
    At<std::uintptr_t>(Imports+8)=reinterpret_cast<std::uintptr_t>(&Saved);
    At<std::uintptr_t>(Imports+16)=reinterpret_cast<std::uintptr_t>(&Restored);
    if(mode!=Mode::Direct) {
        import={mode==Mode::BeforeImport ? reinterpret_cast<void*>(&ReturnOne) : reinterpret_cast<void*>(&bb_checkpoint_cpu_save),
                77,nullptr,&ObserveImport,nullptr};
        Check(EmitImportProxy({reinterpret_cast<std::uint8_t*>(Imports+256),32},import),"emit rebound guest import stub");
        At<std::uintptr_t>(Imports)=Imports+256;
        Check(!mprotect(reinterpret_cast<void*>(Imports),Page,PROT_READ|PROT_EXEC),"import stub execute protection");
    }
}
}
int main(int argc,char** argv) {
    Check(argc==3,"usage: --save/--restore/--proxy-save/--proxy-restore/--before-save/--before-restore checkpoint-file");path=argv[2];
    const std::string operation=argv[1];
    if(operation=="--proxy-save" || operation=="--proxy-restore")mode=Mode::AfterImport;
    if(operation=="--before-save" || operation=="--before-restore")mode=Mode::BeforeImport;
    identity="synthetic-guest-v1-"+std::to_string(unsigned(mode))+"-"+std::to_string(Hash(bb_ck_fixture_begin,CodeSize()));
    if(operation=="--save" || operation=="--proxy-save" || operation=="--before-save") {
        Setup();
        for(unsigned k=0;k<512;++k)At<std::uint8_t>(Data+8192+k)=std::uint8_t(k*13+k/32);
        At<std::uint64_t>(Data+10000)=41;At<std::uint64_t>(Data+10008)=73;
        for(unsigned k=0;k<64;++k)At<std::uint8_t>(Data+11000+k)=std::uint8_t(k*7);
        bb_ck_fixture_enter(reinterpret_cast<void*>(Code),reinterpret_cast<void*>(Stack+StackSize-64));
    }
    Check(operation=="--restore" || operation=="--proxy-restore" || operation=="--before-restore","unknown operation");
    restoring=true;
    Archive a;std::string error;const bool loaded=Load(path,identity,a,error);Check(loaded,error.c_str());
    Check(a.sections.size()==3,"complete fixture section set");
    const auto& context=a.sections[0];const auto& data=a.sections[1];const auto& stack=a.sections[2];
    Check(context.kind==SectionKind::Cpu && context.version==1 && context.id==0 && context.bytes.size()==sizeof(BbCheckpointCpu),"CPU section schema");
    Check(data.kind==SectionKind::MemoryBytes && data.version==1 && data.id==1 && data.bytes.size()==Page,"data section schema");
    Check(stack.kind==SectionKind::MemoryBytes && stack.version==1 && stack.id==2 && stack.bytes.size()==StackSize,"stack section schema");
    BbCheckpointCpu cpu{};std::memcpy(&cpu,context.bytes.data(),sizeof(cpu));
    Check(bb_checkpoint_cpu_valid(&cpu),"loaded CPU record is valid");
    Check(((cpu.rip>=Code && cpu.rip<Code+CodeSize()) || (mode==Mode::BeforeImport && cpu.rip==Imports+256)) &&
          cpu.gpr[BB_CK_RSP]>=Stack+16 &&
          cpu.gpr[BB_CK_RSP]<=Stack+StackSize-256,"loaded CPU addresses belong to this guest");
    Check(std::memcmp(data.bytes.data(),&cpu,sizeof(cpu))==0,"CPU and data sections agree");
    expected_cpu=cpu;
    // No address or protection from the file is used to create host mappings.
    Setup();std::memcpy(reinterpret_cast<void*>(Data),data.bytes.data(),data.bytes.size());
    std::memcpy(reinterpret_cast<void*>(Stack),stack.bytes.data(),stack.bytes.size());
    bb_checkpoint_cpu_restore(&cpu);
}
