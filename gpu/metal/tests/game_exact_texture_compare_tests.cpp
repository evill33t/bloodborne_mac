// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/exact_texture_compare.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <new>
#include <thread>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
using namespace Gpu::Metal::ExactTextureCompare;
using Byte=unsigned char;
constexpr u64 Size=64ull<<10;
namespace {thread_local int fail_allocation_after=-1;}
// Fixture-local fault injection exercises Store's transactional allocations.
void* operator new(std::size_t bytes) {
    if(fail_allocation_after==0)throw std::bad_alloc();
    if(fail_allocation_after>0)--fail_allocation_after;
    if(auto* memory=std::malloc(bytes ? bytes : 1))return memory;
    throw std::bad_alloc();
}
void operator delete(void* pointer)noexcept{std::free(pointer);}
void* operator new[](std::size_t bytes){return ::operator new(bytes);}
void operator delete[](void* pointer)noexcept{std::free(pointer);}

Handle Make(u64 generation,Byte value=0x73,u64 allocation=Size) {
    auto data=std::make_shared<const std::vector<Byte>>(Size,value);
    return Snapshot::Create(data,data->data(),data->size(),allocation,generation);
}
Identity Id(u64 version,u64 map=2,u64 descriptor=99,u64 address=0x800000) {
    return {descriptor,address,Size,version,map};
}
void Configuration() {
    for(const auto* text:{"","0","-1","+384"," 384","384 ","384.0","2049","99999999999999999999","1x"})assert(ParseBudget(text)==0);
    assert(!ParseBudget(nullptr));assert(ParseBudget("384")==384ull<<20);
    assert(ParseBudget("000384")==384ull<<20);assert(ParseBudget("1")==1ull<<20);
    assert(ParseBudget("2048")==kMaximumBudgetBytes);
    assert(!Retainable(Size-1,Size*2));assert(Retainable(Size,Size));
    assert(!Retainable(Size,Size-1));assert(!Retainable(kMaximumSourceBytes+1,kMaximumBudgetBytes));
    Cache excessive(std::numeric_limits<u64>::max());assert(excessive.Budget()==kMaximumBudgetBytes);
    std::atomic<u64> counter{std::numeric_limits<u64>::max()-1};
    assert(NextGeneration(counter)==std::numeric_limits<u64>::max());assert(!NextGeneration(counter));
    assert(!NextGeneration(counter));assert(counter==std::numeric_limits<u64>::max());
    counter=0;std::array<std::array<u64,256>,4> values{};
    std::array<std::thread,4> workers;
    for(unsigned k=0;k<workers.size();++k)workers[k]=std::thread([&,k]{for(auto& value:values[k])value=NextGeneration(counter);});
    for(auto& worker:workers)worker.join();
    std::vector<u64> all;for(const auto& group:values)all.insert(all.end(),group.begin(),group.end());
    std::sort(all.begin(),all.end());for(u64 i=0;i<all.size();++i)assert(all[i]==i+1);
}
void Equality() {
    assert(EqualBytes(nullptr,nullptr,0));assert(!EqualBytes(nullptr,nullptr,1));
    for(std::size_t n=0;n<=513;++n)for(unsigned alignment=0;alignment<16;++alignment) {
        std::vector<Byte> a(n+alignment+1,0x8d),b(n+16-alignment,0x8d);
        auto* pa=a.data()+alignment;const auto* pb=b.data()+15-alignment;
        assert(EqualBytes(pa,pb,n));
        for(std::size_t byte=0;byte<n;++byte) {
            pa[byte]^=1;assert(!EqualBytes(pa,pb,n));pa[byte]^=1;
        }
    }
    for(const auto n:{Size-1,Size,Size+1,Size*2-1,Size*2+1}) {
        std::vector<Byte> a(n+3,0xa9),b(n+7,0xa9);
        assert(EqualBytes(a.data()+3,b.data()+7,n));
        for(const auto at:{u64(0),n/2,n-1,std::min(Size-1,n-1),std::min(Size,n-1)}) {
            a[at+3]^=1;assert(!EqualBytes(a.data()+3,b.data()+7,n));a[at+3]^=1;
        }
        a[3]^=1;a[3+16]^=1;assert(!EqualBytes(a.data()+3,b.data()+7,n));a[3]^=1;a[3+16]^=1;
    }
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));std::array<Byte*,2> maps{};
    for(auto& p:maps) {
        p=static_cast<Byte*>(mmap(nullptr,page*3,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0));
        assert(p!=MAP_FAILED);assert(!mprotect(p+page,page,PROT_READ|PROT_WRITE));std::memset(p+page,0x57,page);
    }
    for(const auto n:{std::size_t(0),std::size_t(1),std::size_t(15),std::size_t(16),std::size_t(17),std::size_t(63),
                      std::size_t(64),std::size_t(65),std::size_t(513),page-1,page})for(bool end:{false,true}) {
        auto* a=maps[0]+page+(end ? page-n : 0);auto* b=maps[1]+page+(end ? page-n : 0);
        assert(EqualBytes(a,b,n));
        if(n)for(const auto at:{std::size_t(0),n/2,n-1}) {
            a[at]^=1;assert(!EqualBytes(a,b,n));a[at]^=1;
        }
    }
    for(auto* p:maps)assert(!munmap(p,page*3));
}
void OwnershipAndCache() {
    auto data=std::make_shared<const std::vector<Byte>>(Size,0x73);
    assert(!Snapshot::Create({},data->data(),Size,Size,1));
    assert(!Snapshot::Create(data,nullptr,Size,Size,1));
    assert(!Snapshot::Create(data,data->data(),Size,Size-1,1));
    assert(!Snapshot::Create(data,data->data(),Size,Size,0));
    assert(!Snapshot::Create(data,reinterpret_cast<const void*>(std::numeric_limits<std::uintptr_t>::max()-3),Size,Size,1));
    auto snapshot=Snapshot::Create(data,data->data(),Size,Size*2,1);
    std::weak_ptr<const std::vector<Byte>> weak=data;data.reset();
    Cache cache(Size*3);assert(cache.Store(Id(1),snapshot));snapshot.reset();assert(!weak.expired());
    assert(cache.RetainedBytes()==Size*2);assert(cache.Entries()==1);
    std::vector<Byte> source(Size,0x73);
    assert(cache.Compare(Id(1),source.data())==Result::Equal);
    // Raw guest stores have no notification. All positions still participate.
    for(const auto changed:{u64(0),Size/2,Size-1}) {
        source[changed]^=1;
        assert(cache.Compare(Id(1),source.data())==Result::Different);
        assert(weak.expired());assert(cache.RetainedBytes()==0);
        assert(cache.Compare(Id(1),source.data())==Result::Missing);
        source[changed]^=1;auto next=Make(changed+2);
        assert(cache.Store(Id(1),next));next.reset();
    }
    cache.Clear();assert(!cache.Entries() && !cache.RetainedBytes());
    for(unsigned field=0;field<5;++field) {
        auto id=Id(10);assert(cache.Store(id,Make(10+field)));
        if(field==0)++id.descriptor_key;
        if(field==1)++id.address;
        if(field==2)--id.bytes;
        if(field==3)id.mapping_generation+=2;
        if(field==4)id.mapping_generation|=1;
        assert(cache.Compare(id,source.data())==Result::Missing);
        assert(cache.Compare(Id(10),source.data())==Result::Missing);
    }
    assert(cache.Store(Id(20),Make(30)));
    assert(cache.Compare(Id(21),source.data())==Result::Missing); // cannot borrow another upload's baseline
    assert(cache.Compare(Id(20),source.data())==Result::Equal);
    assert(cache.Compare(Id(20),source.data(),Size-1,2)==Result::Missing);
    assert(cache.Store(Id(20),Make(31)));
    assert(cache.Compare(Id(20),source.data(),0,0)==Result::Missing);
    assert(cache.Store(Id(20),Make(32)));
    source.back()^=1;assert(cache.Compare(Id(20),source.data(),0,Size/2)==Result::Equal);
    assert(cache.Compare(Id(20),source.data(),Size/2,Size/2)==Result::Different);source.back()^=1;
    Cache off;assert(!off.Store(Id(1),Make(40)));assert(off.Compare(Id(1),source.data())==Result::Missing);
    // An already-owned snapshot is read-only and remains valid after eviction.
    auto externally_owned=Make(50);Cache small(Size);assert(small.Store(Id(1),externally_owned));
    assert(small.Store(Id(2),Make(51)));assert(small.Counters().evictions==1);
    assert(small.Compare(Id(1),source.data())==Result::Missing);
    assert(EqualBytes(externally_owned->Data(),source.data(),Size));
    assert(small.RetainedBytes()==Size);
    assert(!small.Store(Id(3),Make(52,0x73,Size*2))); // payload fits, whole buffer does not
    assert(small.Compare(Id(2),source.data())==Result::Equal);
    Cache lru(Size*2);assert(lru.Store(Id(1),Make(61)));assert(lru.Store(Id(2),Make(62)));
    assert(lru.Compare(Id(1),source.data())==Result::Equal);assert(lru.Store(Id(3),Make(63)));
    assert(lru.Compare(Id(2),source.data())==Result::Missing);
    assert(lru.Compare(Id(1),source.data())==Result::Equal);assert(lru.Compare(Id(3),source.data())==Result::Equal);
    // Identical immutable allocation shared by two versions is charged once;
    // another wrapper claiming its generation is rejected.
    auto shared=Make(70,0x73,Size*2);Cache sharing(Size*2);
    assert(sharing.Store(Id(1),shared));assert(sharing.Store(Id(2),shared));
    assert(sharing.RetainedBytes()==Size*2);assert(sharing.Entries()==2);
    assert(!sharing.Store(Id(3),Make(70)));sharing.Erase(1);assert(sharing.RetainedBytes()==Size*2);
    sharing.Erase(2);assert(!sharing.RetainedBytes());
    assert(sharing.Store(Id(1),shared));assert(sharing.Store(Id(2),shared));
    assert(sharing.Store(Id(3),Make(71)));assert(sharing.RetainedBytes()==Size);
    assert(sharing.Compare(Id(1),source.data())==Result::Missing);
    assert(sharing.Compare(Id(2),source.data())==Result::Missing);
    Cache entries(Size*2);auto one_backing=Make(80,0x73,Size*2);
    for(std::size_t i=0;i<=kMaximumEntries;++i)assert(entries.Store(Id(100+i),one_backing));
    assert(entries.Entries()==kMaximumEntries && entries.RetainedBytes()==Size*2);
    assert(entries.Counters().evictions==1);assert(entries.Compare(Id(100),source.data())==Result::Missing);
    // Pressure may need to drop every reference before one whole backing is
    // released. It remains bounded by the fixed entry cap.
    assert(entries.Store(Id(10000),Make(81)));assert(entries.Entries()==1);
    assert(entries.RetainedBytes()==Size);
    for(int fail=0;fail<8;++fail) {
        Cache transactional(Size*2);assert(transactional.Store(Id(1),Make(100+fail)));
        const auto replacement=Make(200+fail,0x73,Size*2);
        fail_allocation_after=fail;
        const bool stored=transactional.Store(Id(2),replacement);
        fail_allocation_after=-1;
        assert(transactional.RetainedBytes()<=transactional.Budget());
        assert(transactional.Compare(Id(2),source.data())==(stored ? Result::Equal : Result::Missing));
        assert(transactional.Compare(Id(1),source.data())==Result::Missing);
        if(!stored)assert(transactional.Counters().allocation_failures==1);
    }
    auto saturated=std::numeric_limits<u64>::max()-1;Add(saturated,2);assert(saturated==std::numeric_limits<u64>::max());
    std::puts("exact texture comparison: configuration, generations, every-byte/bounds, immutable ownership, identity, whole-buffer LRU and CRC-miss contract PASS");
}
int main(){Configuration();Equality();OwnershipAndCache();}
