// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the actual guest pthread/TCB/semaphore ABI, not a parallel model.
extern "C" {
#include "src/runtime.h"
int32_t runtime_thread_spawn(void**,void*(*)(void*),void*,uint64_t,const char*);
int32_t runtime_thread_join_spawned(void*,void**);
void runtime_restart() {__builtin_abort();}
}
#include "src/checkpoint/semaphore.h"
#include "src/checkpoint/archive.h"
#include "src/checkpoint/synchronization.h"
#include "src/checkpoint/rwlock.h"
#include "src/checkpoint/graphics.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <sys/mman.h>
#include <thread>
#include <vector>
#include <unistd.h>

extern "C" const unsigned char bb_ck_runtime_begin[];
extern "C" const std::uint64_t bb_ck_runtime_size;
__asm__(".text\n.p2align 4\n.globl _bb_ck_runtime_begin\n_bb_ck_runtime_begin:\n"
        "push %rbp\nmov %rsp,%rbp\npush %r12\nsub $8,%rsp\nmov %rdi,%r12\n"
        "call *24(%r12)\nmov %rax,56(%r12)\nmov %fs:0,%rax\nmov %rax,64(%r12)\n"
        "cmpq $0,88(%r12)\njne 2f\n"
        "1: mov %r12,%rdi\ncall *0(%r12)\nincq 80(%r12)\ncmpq $0,40(%r12)\njne 1b\n"
        "mov %r12,%rdi\ncall *0(%r12)\n"
        "mov $0x31,%eax\nadd $8,%rsp\npop %r12\npop %rbp\nret\n"
        "2: cmpq $2,88(%r12)\nje 3f\ncmpq $3,88(%r12)\nje 4f\ncmpq $4,88(%r12)\nje 8f\n"
        "mov 32(%r12),%edi\nmov $1,%esi\nxor %edx,%edx\ncall *8(%r12)\n"
        "mov %eax,80(%r12)\nmov $1,%eax\nmov %rax,48(%r12)\n"
        "mov $0x52,%edi\ncall *16(%r12)\nud2\n"
        "3: lea 96(%r12),%rdi\ncall *104(%r12)\nmov %rax,80(%r12)\n"
        "lea 96(%r12),%rdi\ncall *112(%r12)\nmovq $1,48(%r12)\nmov $0x62,%eax\njmp 6f\n"
        "4: lea 96(%r12),%rdi\ncall *104(%r12)\n"
        "5: cmpq $0,40(%r12)\nje 7f\nlea 128(%r12),%rdi\nlea 96(%r12),%rsi\ncall *120(%r12)\njmp 5b\n"
        "7: lea 96(%r12),%rdi\ncall *112(%r12)\nmovq $1,48(%r12)\nmov $0x73,%eax\n"
        "6: add $8,%rsp\npop %r12\npop %rbp\nret\n"
        "8: lea 144(%r12),%rdi\ncall *152(%r12)\nmov %rax,80(%r12)\n"
        "lea 144(%r12),%rdi\ncall *160(%r12)\nmovq $1,48(%r12)\nmov $0x84,%eax\njmp 6b\n"
        "Lbb_ck_runtime_end:\n.section __TEXT,__const\n.p2align 3\n"
        ".globl _bb_ck_runtime_size\n_bb_ck_runtime_size: .quad Lbb_ck_runtime_end-_bb_ck_runtime_begin\n.text\n");
namespace {
constexpr unsigned N=5;
std::uint64_t *graphics_counter=nullptr,graphics_at_pause=0;
bool graphics_fail=false,graphics_active=false;
unsigned graphics_releases=0;
void GpuCallback() {
    // GPU fence callbacks may touch HLE synchronization while draining, and
    // again when admission reopens. Either call deadlocks if HLE is paused.
    std::uint32_t temporary=0;assert(!bb_checkpoint_sema_create(&temporary,1,0,1));assert(!bb_checkpoint_sema_delete(temporary));
}
int GraphicsPause(std::uint32_t timeout,void** token,char* error,std::size_t capacity) {
    assert(timeout && !graphics_active);*token=nullptr;GpuCallback();
    if(graphics_fail){std::snprintf(error,capacity,"Fixture GPU drain timeout.");return 0;}
    graphics_at_pause=__atomic_load_n(graphics_counter,__ATOMIC_ACQUIRE);graphics_active=true;*token=&graphics_active;return 1;
}
void GraphicsRelease(void* token) {
    assert(token==&graphics_active && graphics_active && __atomic_load_n(graphics_counter,__ATOMIC_ACQUIRE)==graphics_at_pause);
    GpuCallback();graphics_active=false;++graphics_releases;
}
int GraphicsDescribe(void* token,BbCheckpointGraphicsChunk* chunks,std::size_t capacity,std::size_t* count) {
    assert(token==&graphics_active && graphics_active);*count=1;
    if(chunks){assert(capacity>=1);chunks[0]={7,4};}return 0;
}
int GraphicsRead(void* token,std::uint64_t id,std::uint64_t offset,void* out,std::size_t size,char*,std::size_t) {
    assert(token==&graphics_active && graphics_active && id==7 && offset<=4 && size<=4-offset);
    const unsigned char bytes[]{1,3,7,9};if(size)std::memcpy(out,bytes+offset,size);return 1;
}
void Tick(std::uint64_t* data) {
    using Op=int32_t(*)(void**);
    auto* local=runtime_thread_checkpoint_local();
    const std::uint64_t index[]{2,0};using Tls=void*(*)(const std::uint64_t*);
    auto* module=static_cast<unsigned char*>(reinterpret_cast<Tls>(runtime_resolve("vNe1w4diLCs#p#J",0))(index));
    if(!local->keys[254] && data[5]) {
        assert(module[0]==42 && module[1]==17 && !module[63]);module[0]=0xa7;
        assert(!reinterpret_cast<Op>(runtime_mutex_resolve("9UK1vLZQft4#p#J"))(reinterpret_cast<void**>(data+12)));
        assert(!reinterpret_cast<Op>(runtime_mutex_resolve("9UK1vLZQft4#p#J"))(reinterpret_cast<void**>(data+12)));
        assert(!reinterpret_cast<Op>(runtime_rwlock_resolve("mqdNorrB+gI#p#J"))(reinterpret_cast<void**>(data+18)));
        local->keys[254]=1;*runtime_errno()=77;data[17]=std::uintptr_t(runtime_errno());
    }
    assert(module[0]==0xa7 && module[1]==17 && !module[63]);
    assert(local->keys[254] && *runtime_errno()==77 && data[17]==std::uintptr_t(runtime_errno()));
    if(local->keys[254]==1 && !data[5]) {
        assert(!reinterpret_cast<Op>(runtime_mutex_resolve("tn3VlD0hG60#p#J"))(reinterpret_cast<void**>(data+12)));
        assert(!reinterpret_cast<Op>(runtime_mutex_resolve("tn3VlD0hG60#p#J"))(reinterpret_cast<void**>(data+12)));
        assert(!reinterpret_cast<Op>(runtime_rwlock_resolve("+L98PIbGttk#p#J"))(reinterpret_cast<void**>(data+18)));
        local->keys[254]=2;
    }
    std::this_thread::yield();
}
template<class F>void Until(F predicate) {
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!predicate()){assert(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
}
int main(int argc,char** argv) {
    assert(bb_checkpoint_runtime_enabled());
    runtime_start(1);
    const bool restore=argc==3 && !std::strcmp(argv[1],"--restore");
    const bool save=argc==3 && !std::strcmp(argv[1],"--save");
    assert(argc==1 || restore || save);
    auto* canary=reinterpret_cast<std::uint64_t*>(runtime_resolve("f7uOxY9mM1U#p#J",1));
    assert(canary && std::uintptr_t(canary)<1ull<<40);
    if(!restore) {
        // Level loading creates and destroys many temporary attributes. The
        // lifetime identity counter must not be capped at the live-object
        // snapshot limit or a later constructor unexpectedly receives ENOMEM.
        using AttrOp=int32_t(*)(void**);
        auto init=reinterpret_cast<AttrOp>(runtime_mutex_resolve("F8bUHwAG284#p#J"));
        auto destroy=reinterpret_cast<AttrOp>(runtime_mutex_resolve("smWEktiyyG0#p#J"));
        for(unsigned n=0;n<100000;++n) {
            void* attr=nullptr;
            if(auto error=init(&attr)) {
                std::fprintf(stderr,"Temporary attribute churn failed at %u: %#x\n",n,std::uint32_t(error));return 1;
            }
            assert(!destroy(&attr) && !attr);
        }
    }
    constexpr std::size_t page=16384;
    auto* code=static_cast<unsigned char*>(runtime_low_map(page,PROT_READ|PROT_WRITE));
    auto* imports=static_cast<unsigned char*>(runtime_low_map(page,PROT_READ|PROT_WRITE));
    auto* data=static_cast<std::uint64_t*>(runtime_low_map(page,PROT_READ|PROT_WRITE));
    assert(code && imports && data);
    const auto bytes=bb_ck_runtime_size;assert(bytes && bytes<page);
    std::memcpy(code,bb_ck_runtime_begin,bytes);assert(bb_checkpoint_runtime_code(std::uintptr_t(code),bytes));
    const unsigned char exit_code[]{0x48,0xc7,0x87,0xb0,0,0,0,0x34,0x12,0,0,0xc3};
    std::memcpy(code+4096,exit_code,sizeof(exit_code));assert(bb_checkpoint_runtime_code(std::uintptr_t(code+4096),sizeof(exit_code)));
    const char* nids[]{nullptr,"Zxa0VhQVTsk#p#J","3kg7rT0NQIs#p#J","aI+OeCz8xrQ#p#J","9UK1vLZQft4#p#J","tn3VlD0hG60#p#J","WKAXJ4XBPQ4#p#J","Ox9i0c7L5w0#p#J","+L98PIbGttk#p#J"};
    std::uint64_t functions[9];
    for(unsigned k=0;k<9;++k) {
        const auto target=k?runtime_thread_resolve(nids[k]):reinterpret_cast<std::uintptr_t>(&Tick);
        const auto resolved=k==1?runtime_sema_resolve(nids[k]):k>=7?runtime_rwlock_resolve(nids[k]):k>=4?runtime_mutex_resolve(nids[k]):target;
        functions[k]=bb_checkpoint_runtime_import(imports+k*32,resolved,k+1,k==1 || k==4 || k==6 || k==7);assert(functions[k]);
        if(k)bb_checkpoint_runtime_import_label(k+1,runtime_import_name(nids[k]));
    }
    assert(!mprotect(code,page,PROT_READ|PROT_EXEC) && !mprotect(imports,page,PROT_READ|PROT_EXEC));
    static const unsigned char initial[]{42,17};runtime_set_main_tls(initial,2,64,16);runtime_set_module_tls(2,initial,2,64);
    std::uint32_t semaphore=0;
    void* handles[N]{};
    if(restore) {
        using namespace Bb::Checkpoint;
        Archive archive;std::string error;
        assert(Load(argv[2],"production-runtime-guest-v2",archive,error));
        const auto section=[&](SectionKind kind,std::uint64_t id)->const Section& {
            for(const auto& s:archive.sections)if(s.kind==kind && s.id==id)return s;
            std::abort();
        };
        const auto& meta=section(SectionKind::Fixture,0);assert(meta.bytes.size()==40);
        std::uint64_t values[5];std::memcpy(values,meta.bytes.data(),sizeof(values));
        assert(values[0]==2 && values[1]==std::uintptr_t(code) && values[2]==std::uintptr_t(imports) && values[3]==std::uintptr_t(data));
        const auto& ts=section(SectionKind::Threads,0);assert(ts.bytes.size()==N*sizeof(BbCheckpointThreadRecord));
        BbCheckpointThreadRecord records[N];std::memcpy(records,ts.bytes.data(),sizeof(records));
        const auto& cs=section(SectionKind::Cpu,0);assert(cs.bytes.size()==N*sizeof(BbCheckpointRuntimeThread));
        BbCheckpointRuntimeThread contexts[N];std::memcpy(contexts,cs.bytes.data(),sizeof(contexts));
        const auto& ds=section(SectionKind::MemoryBytes,std::uintptr_t(data));assert(ds.bytes.size()==page);
        const auto& as=section(SectionKind::Threads,1);assert(as.bytes.size()==sizeof(BbCheckpointThreadAttrRecord));
        BbCheckpointThreadAttrRecord attr_record;std::memcpy(&attr_record,as.bytes.data(),sizeof(attr_record));
        const auto& process=section(SectionKind::Services,0);assert(process.bytes.size()==sizeof(BbCheckpointProcessRecord));
        BbCheckpointProcessRecord process_record;std::memcpy(&process_record,process.bytes.data(),sizeof(process_record));
        const auto& exits=section(SectionKind::Services,1);assert(exits.bytes.size()==sizeof(BbCheckpointExitRecord) && process_record.handlers==1);
        BbCheckpointExitRecord exit_record;std::memcpy(&exit_record,exits.bytes.data(),sizeof(exit_record));
        std::size_t expected_sections=10+N*2;
        for(const auto& r:records) {
            assert(section(SectionKind::MemoryBytes,r.tls).bytes.size()==r.tls_size);
            assert(section(SectionKind::MemoryBytes,r.stack).bytes.size()==r.stack_size);
            for(unsigned m=2;m<8;++m)if(r.module_tls[m]) {
                assert(section(SectionKind::MemoryBytes,r.module_tls[m]).bytes.size()==r.module_tls_size[m]);++expected_sections;
            }
        }
        assert(archive.sections.size()==expected_sections);
        // All file/schema checks precede the first restore-owned mapping.
        assert(bb_checkpoint_runtime_bind_return_stub(values[4]));
        assert(!runtime_thread_checkpoint_restore(records,N));
        assert(!runtime_thread_checkpoint_attr_restore(&attr_record,1));
        std::memcpy(data,ds.bytes.data(),page);
        for(const auto& r:records) {
            const auto& tls=section(SectionKind::MemoryBytes,r.tls);const auto& stack=section(SectionKind::MemoryBytes,r.stack);
            std::memcpy(reinterpret_cast<void*>(r.tls),tls.bytes.data(),tls.bytes.size());
            std::memcpy(reinterpret_cast<void*>(r.stack),stack.bytes.data(),stack.bytes.size());
            for(unsigned m=2;m<8;++m)if(r.module_tls[m]) {
                const auto& module=section(SectionKind::MemoryBytes,r.module_tls[m]);
                std::memcpy(reinterpret_cast<void*>(r.module_tls[m]),module.bytes.data(),module.bytes.size());
            }
        }
        assert(!runtime_checkpoint_process_restore(&process_record,&exit_record));
        const auto& sem=section(SectionKind::Synchronization,1);char why[512]{};
        assert(bb_checkpoint_runtime_restore_semaphores(sem.bytes.data(),sem.bytes.size(),why,sizeof(why)));
        const auto& sync=section(SectionKind::Synchronization,2);
        assert(bb_checkpoint_runtime_restore_synchronization(sync.bytes.data(),sync.bytes.size(),why,sizeof(why)));
        const auto& rw=section(SectionKind::Synchronization,3);
        assert(bb_checkpoint_runtime_restore_rwlocks(rw.bytes.data(),rw.bytes.size(),why,sizeof(why)));
        for(unsigned k=0;k<N;++k)handles[k]=reinterpret_cast<void*>(data[k*32+7]);
        semaphore=std::uint32_t(data[4]);const auto original=data[10];
        assert(!runtime_thread_checkpoint_start(contexts,N));
        // The new native workers must not touch guest state before all
        // providers have been rebuilt and the coordinator releases them.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));assert(data[10]==original && !data[38]);
        bb_checkpoint_runtime_activate_rwlocks();bb_checkpoint_runtime_activate_synchronization();bb_checkpoint_runtime_activate_semaphores();runtime_thread_checkpoint_activate();
        Until([&]{return __atomic_load_n(data+10,__ATOMIC_ACQUIRE)>original;});
        std::puts("Fresh process: production guest threads, TLS/errno, owned recursive mutex, condition and semaphore waiters resumed.");
    } else {
    using Create=int32_t(*)(uint32_t*,const char*,uint32_t,int32_t,int32_t,const void*);
    auto create=reinterpret_cast<Create>(runtime_sema_resolve("188x57JYp0g#p#J"));
    assert(create && !create(&semaphore,"runtime checkpoint",1,0,1,nullptr));
    using Init=int32_t(*)(void**,void**,const char*);
    using AttrInit=int32_t(*)(void**);using AttrType=int32_t(*)(void**,int);
    auto attr_init=reinterpret_cast<AttrInit>(runtime_mutex_resolve("F8bUHwAG284#p#J"));
    auto attr_type=reinterpret_cast<AttrType>(runtime_mutex_resolve("iMp8QpE+XO4#p#J"));
    void* attr=nullptr;void* owned=nullptr;void* mutex=nullptr;void* condition=nullptr;
    void* rwlock=nullptr;
    void* thread_attr=nullptr;
    auto thread_attr_init=reinterpret_cast<AttrInit>(runtime_thread_resolve("nsYoNRywwNg#p#J"));
    assert(!thread_attr_init(&thread_attr));
    assert(!attr_init(&attr) && !attr_type(&attr,2));
    auto mutex_init=reinterpret_cast<Init>(runtime_mutex_resolve("cmo1RIYva9o#p#J"));
    auto cond_init=reinterpret_cast<Init>(runtime_mutex_resolve("2Tb92quprl0#p#J"));
    assert(!mutex_init(&owned,&attr,"owned") && !mutex_init(&mutex,nullptr,"condition mutex") && !cond_init(&condition,nullptr,"condition"));
    auto rw_init=reinterpret_cast<Init>(runtime_rwlock_resolve("6ULAa0fq4jA#p#J"));assert(!rw_init(&rwlock,nullptr,"owned rwlock"));
    runtime_set_procparam(data+24);
    using Heap=void(*)(void**);reinterpret_cast<Heap>(runtime_resolve("p5EcQeEeJAE#p#J",0))(reinterpret_cast<void**>(data+25));
    using AtExit=int(*)(void(*)(void*),void*,void*);
    assert(!reinterpret_cast<AtExit>(runtime_resolve("tsvEmnenz48#q#q",0))(reinterpret_cast<void(*)(void*)>(code+4096),data,nullptr));
    data[23]=*canary;
    for(unsigned k=0;k<N;++k) {
        auto* p=data+k*32;std::memcpy(p,functions,4*sizeof(*functions));p[4]=semaphore;p[5]=1;p[11]=k;
        p[12]=std::uintptr_t(k==3?mutex:owned);p[13]=functions[4];p[14]=functions[5];p[15]=functions[6];p[16]=std::uintptr_t(condition);
        p[18]=std::uintptr_t(rwlock);p[19]=functions[7];p[20]=functions[8];
        p[21]=std::uintptr_t(thread_attr);
        assert(!runtime_thread_spawn(&handles[k],reinterpret_cast<void*(*)(void*)>(code),p,262144,"ck-runtime"));
        if(k==0)Until([&]{return __atomic_load_n(p+10,__ATOMIC_ACQUIRE)>0;});
    }
    }
    Until([&]{return __atomic_load_n(data+7,__ATOMIC_ACQUIRE) && __atomic_load_n(data+39,__ATOMIC_ACQUIRE) &&
        __atomic_load_n(data+71,__ATOMIC_ACQUIRE) && __atomic_load_n(data+103,__ATOMIC_ACQUIRE) && runtime_sema_waiters(semaphore)==1 &&
        __atomic_load_n(data+135,__ATOMIC_ACQUIRE) && Bb::Checkpoint::SyncWaiters(false)==1 &&
        Bb::Checkpoint::SyncWaiters(true)==1 && Bb::Checkpoint::RwWaiters()==1;});
    char error[512]{};void* token=nullptr;
    const BbCheckpointGraphicsHooks hooks{GraphicsPause,GraphicsRelease,GraphicsDescribe,GraphicsRead};
    graphics_counter=data+10;assert(bb_checkpoint_runtime_graphics_hooks(&hooks));
    assert(!bb_checkpoint_runtime_graphics_hooks(&hooks));
    graphics_fail=true;assert(!bb_checkpoint_runtime_freeze(2000,&token,error,sizeof(error)) && !token && !graphics_active);
    const auto after_failure=__atomic_load_n(data+10,__ATOMIC_ACQUIRE);
    Until([&]{return __atomic_load_n(data+10,__ATOMIC_ACQUIRE)>after_failure;});
    graphics_fail=false;
    assert(bb_checkpoint_runtime_freeze(2000,&token,error,sizeof(error)) && token);
    const BbCheckpointGraphicsChunk* gpu_chunks=nullptr;
    assert(bb_checkpoint_runtime_graphics_chunks(token,&gpu_chunks)==1 && gpu_chunks[0].id==7 && gpu_chunks[0].size==4);
    unsigned char gpu_bytes[4]{};assert(bb_checkpoint_runtime_graphics_read(token,7,0,gpu_bytes,4,error,sizeof(error)));
    assert(gpu_bytes[0]==1 && gpu_bytes[1]==3 && gpu_bytes[2]==7 && gpu_bytes[3]==9);
    assert(!bb_checkpoint_runtime_graphics_read(token,7,3,gpu_bytes,4,error,sizeof(error)) && gpu_bytes[0]==1);
    const BbCheckpointRuntimeThread* threads=nullptr;
    assert(bb_checkpoint_runtime_threads(token,&threads)==N);
    const BbCheckpointThreadRecord* frozen_records=nullptr;const BbCheckpointThreadAttrRecord* frozen_attributes=nullptr;
    assert(bb_checkpoint_runtime_thread_records(token,&frozen_records)==N);
    assert(bb_checkpoint_runtime_thread_attributes(token,&frozen_attributes)==1);
    for(unsigned k=0;k<N;++k) {
        assert(bb_checkpoint_cpu_valid(&threads[k].cpu));
        assert(threads[k].cpu.gpr[BB_CK_RSP]>=threads[k].stack && threads[k].cpu.gpr[BB_CK_RSP]<threads[k].stack+threads[k].stack_size);
        bool found=false;for(auto handle:handles)if(threads[k].id==std::uintptr_t(handle))found=true;assert(found);
    }
    const unsigned char* semabytes=nullptr;const auto semasize=bb_checkpoint_runtime_semaphores(token,&semabytes);
    Bb::Checkpoint::Semaphores::Image image;std::string why;
    assert(Bb::Checkpoint::Semaphores::Decode({semabytes,semasize},image,why));
    assert(image.waiters.size()==1 && image.waiters[0].thread==std::uintptr_t(handles[1]) && !image.waiters[0].done);
    if(save) {
        using namespace Bb::Checkpoint;
        Archive archive;archive.identity="production-runtime-guest-v2";
        const auto add=[&](SectionKind kind,std::uint64_t id,const void* bytes,std::size_t size) {
            Section s;s.kind=kind;s.id=id;s.bytes.resize(size);std::memcpy(s.bytes.data(),bytes,size);archive.sections.push_back(std::move(s));
        };
        const std::uint64_t values[]{2,std::uintptr_t(code),std::uintptr_t(imports),std::uintptr_t(data),bb_checkpoint_runtime_return_stub()};
        add(SectionKind::Fixture,0,values,sizeof(values));
        BbCheckpointThreadRecord records[N];std::memcpy(records,frozen_records,sizeof(records));
        add(SectionKind::Threads,0,records,sizeof(records));
        BbCheckpointThreadAttrRecord attributes[1];std::memcpy(attributes,frozen_attributes,sizeof(attributes));
        add(SectionKind::Threads,1,attributes,sizeof(attributes));
        const BbCheckpointProcessRecord* process=nullptr;const BbCheckpointExitRecord* exits=nullptr;
        assert(bb_checkpoint_runtime_process(token,&process,&exits) && process->handlers==1);
        add(SectionKind::Services,0,process,sizeof(*process));add(SectionKind::Services,1,exits,sizeof(*exits));
        add(SectionKind::Cpu,0,threads,N*sizeof(*threads));
        add(SectionKind::Synchronization,1,semabytes,semasize);
        const unsigned char* syncbytes=nullptr;const auto syncsize=bb_checkpoint_runtime_synchronization(token,&syncbytes);
        add(SectionKind::Synchronization,2,syncbytes,syncsize);
        const unsigned char* rwbytes=nullptr;const auto rwsize=bb_checkpoint_runtime_rwlocks(token,&rwbytes);
        add(SectionKind::Synchronization,3,rwbytes,rwsize);
        add(SectionKind::MemoryBytes,std::uintptr_t(data),data,page);
        for(const auto& r:records) {
            add(SectionKind::MemoryBytes,r.tls,reinterpret_cast<void*>(r.tls),r.tls_size);
            add(SectionKind::MemoryBytes,r.stack,reinterpret_cast<void*>(r.stack),r.stack_size);
            for(unsigned k=0;k<N;++k)if(threads[k].id==r.id) {
                auto& stack=archive.sections.back().bytes;
                std::fill(stack.begin(),stack.begin()+threads[k].cpu.gpr[BB_CK_RSP]-r.stack,0);
            }
            for(unsigned m=2;m<8;++m)if(r.module_tls[m])
                add(SectionKind::MemoryBytes,r.module_tls[m],reinterpret_cast<void*>(r.module_tls[m]),r.module_tls_size[m]);
        }
        assert(Save(argv[2],archive,why));
        // Diverge from the saved data before exiting this process. Repeated
        // restores must use the archive, never a surviving process snapshot.
        data[10]=0xeeee;data[38]=0xdddd;
        std::puts("Production runtime checkpoint fixture saved; live state deliberately changed.");
        std::fflush(stdout);_exit(0);
    }
    auto count=data[10];std::this_thread::sleep_for(std::chrono::milliseconds(15));assert(count==data[10]);
    bb_checkpoint_runtime_thaw(token);
    assert(!graphics_active && graphics_releases==1);
    using Signal=int32_t(*)(uint32_t,int32_t);
    auto signal=reinterpret_cast<Signal>(runtime_sema_resolve("4czppHBiriw#p#J"));assert(!signal(semaphore,1));
    __atomic_store_n(data+5,0,__ATOMIC_RELEASE);
    using HandleOp=int32_t(*)(void**);
    auto mutex_lock=reinterpret_cast<HandleOp>(runtime_mutex_resolve("9UK1vLZQft4#p#J"));
    auto mutex_unlock=reinterpret_cast<HandleOp>(runtime_mutex_resolve("tn3VlD0hG60#p#J"));
    auto cond_signal=reinterpret_cast<HandleOp>(runtime_mutex_resolve("kDh-NfxgMtE#p#J"));
    auto* condition_data=data+3*32;
    assert(!mutex_lock(reinterpret_cast<void**>(condition_data+12)));
    __atomic_store_n(condition_data+5,0,__ATOMIC_RELEASE);
    assert(!cond_signal(reinterpret_cast<void**>(condition_data+16)));
    assert(!mutex_unlock(reinterpret_cast<void**>(condition_data+12)));
    void* results[N]{};
    for(unsigned k=0;k<N;++k)assert(!runtime_thread_join_spawned(handles[k],&results[k]));
    assert(results[0]==reinterpret_cast<void*>(0x31) && results[1]==reinterpret_cast<void*>(0x52));
    assert(results[2]==reinterpret_cast<void*>(0x62) && results[3]==reinterpret_cast<void*>(0x73) && data[70]==1 && data[102]==1);
    assert(results[4]==reinterpret_cast<void*>(0x84) && data[134]==1 && !data[138]);
    assert(data[38]==1 && !data[42] && data[7]==std::uintptr_t(handles[0]) && data[39]==std::uintptr_t(handles[1]));
    assert(data[8] && data[40] && data[8]!=data[40]);
    assert(*canary==data[23] && runtime_application_heap_api()==reinterpret_cast<void**>(data+25));
    using Param=void*(*)();assert(reinterpret_cast<Param>(runtime_resolve("959qrazPIrg#p#J",0))()==data+24);
    assert(!data[22]);runtime_finalize(nullptr);assert(data[22]==0x1234);
    using GetStack=int32_t(*)(void**,std::uint64_t*);
    auto get_stack=reinterpret_cast<GetStack>(runtime_thread_resolve("-fA+7ZlGDQs#p#J"));
    std::uint64_t stack=0;assert(!get_stack(reinterpret_cast<void**>(data+21),&stack) && stack==1024*1024);
    std::puts("Production runtime: guest stacks/TCBs, TLS/errno, recursive mutex ownership, mutex/condition/semaphore replay, normal return and pthread_exit passed.");
}
