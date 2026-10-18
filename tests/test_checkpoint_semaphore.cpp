// SPDX-License-Identifier: GPL-2.0-or-later
#include "src/checkpoint/archive.h"
#include "src/checkpoint/semaphore.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <future>
#include <thread>

using namespace Bb::Checkpoint;
using namespace std::chrono_literals;
namespace {
void Enrolled(Semaphores& semaphores,std::size_t count) {
    const auto end=std::chrono::steady_clock::now()+2s;
    while(semaphores.Pending()!=count && std::chrono::steady_clock::now()<end)std::this_thread::yield();
    assert(semaphores.Pending()==count);
}
void CodecTests() {
    Semaphores::Image image;image.next_id=2;image.next_sequence=3;
    image.objects.push_back({1,0,0,0,3,false});
    image.waiters.push_back({11,1,1,3,0,false,true,5000000000ull});
    image.waiters.push_back({22,2,1,1,0,false,false,0});
    std::vector<std::uint8_t> bytes;std::string error;assert(Semaphores::Encode(image,bytes,error));
    Semaphores::Image decoded;assert(Semaphores::Decode(bytes,decoded,error) && decoded==image);
    const auto valid=bytes;
    for(std::size_t size=0;size<valid.size();++size) {
        bytes.assign(valid.begin(),valid.begin()+size);
        assert(!Semaphores::Decode(bytes,decoded,error) && decoded==image);
    }
    for(auto changed:{std::size_t(12),std::size_t(52),std::size_t(84),std::size_t(96)}) {
        bytes=valid;bytes[changed]=255;
        assert(!Semaphores::Decode(bytes,decoded,error) && decoded==image);
    }
    auto bad=image;bad.waiters[1].thread=11;assert(!Semaphores::Encode(bad,bytes,error));
    bad=image;bad.waiters[0].semaphore=3;assert(!Semaphores::Encode(bad,bytes,error));
    bad=image;bad.objects[0].count=4;assert(!Semaphores::Encode(bad,bytes,error));
    bad=image;bad.waiters[0].remaining_ns=UINT64_MAX;assert(!Semaphores::Encode(bad,bytes,error));
}
void DeadlineTest() {
    Semaphores semaphores;std::uint32_t id=0;assert(!semaphores.Create(&id,1,0,1));
    auto waiter=std::async(std::launch::async,[&]{return semaphores.Wait(41,id,1,100000);});
    Enrolled(semaphores,1);Semaphores::Image image;std::string error;
    assert(semaphores.Pause(image,error));
    std::this_thread::sleep_for(120ms);
    assert(waiter.wait_for(0ms)==std::future_status::timeout);
    semaphores.Thaw();assert(!semaphores.Signal(id,1));
    const auto result=waiter.get();assert(!result.value && result.remaining_us>0 && result.remaining_us<=100000);
    assert(!semaphores.Delete(id));
    // A restored completed wait retains its already-granted result even when
    // its semaphore was deleted before the waiting thread returned to guest.
    image={};image.next_id=2;image.next_sequence=2;
    image.objects.push_back({1,2,0,0,1,true});image.waiters.push_back({51,1,1,1,0,true,false,0});
    Semaphores restored;assert(restored.Restore(image,error));restored.Thaw();
    assert(!restored.Resume(51).value && restored.Pending()==0);
    assert(std::uint32_t(restored.Poll(1,1))==0x80020003u);
}
}
int main(int argc,char** argv) {
    assert(argc==3);std::string error;
    if(!std::strcmp(argv[1],"--save")) {
        CodecTests();DeadlineTest();
        Semaphores semaphores;std::uint32_t id=0;assert(!semaphores.Create(&id,1,0,3));
        auto big=std::async(std::launch::async,[&]{return semaphores.Wait(11,id,3,5000000);});Enrolled(semaphores,1);
        auto small=std::async(std::launch::async,[&]{return semaphores.Wait(22,id,1,5000000);});Enrolled(semaphores,2);
        Semaphores::Image image;assert(semaphores.Pause(image,error));
        Section section;section.kind=SectionKind::Synchronization;section.id=1;
        assert(Semaphores::Encode(image,section.bytes,error));
        Archive archive;archive.identity="checkpoint-semaphore-fixture-v1";archive.sections.push_back(std::move(section));
        assert(Save(argv[2],archive,error));semaphores.Thaw();
        assert(!semaphores.Cancel(id,0));
        assert(std::uint32_t(big.get().value)==0x80020055u && std::uint32_t(small.get().value)==0x80020055u);
        assert(!semaphores.Delete(id));
        puts("Checkpoint semaphores: pending waits serialized without native handles or wait-stack pointers");return 0;
    }
    assert(!std::strcmp(argv[1],"--restore"));
    Archive archive;assert(Load(argv[2],"checkpoint-semaphore-fixture-v1",archive,error));
    assert(archive.sections.size()==1 && archive.sections[0].kind==SectionKind::Synchronization &&
           archive.sections[0].id==1 && archive.sections[0].version==1);
    Semaphores::Image image;assert(Semaphores::Decode(archive.sections[0].bytes,image,error));
    Semaphores restored;assert(restored.Restore(image,error));
    auto big=std::async(std::launch::async,[&]{return restored.Resume(11);});
    auto small=std::async(std::launch::async,[&]{return restored.Resume(22);});
    // Resource reconstruction time must not count against restored deadlines.
    std::this_thread::sleep_for(5ms);restored.Thaw();
    assert(!restored.Signal(1,1));assert(!small.get().value);
    assert(big.wait_for(0ms)==std::future_status::timeout);
    assert(!restored.Signal(1,3));assert(!big.get().value && restored.Pending()==0);
    assert(std::uint32_t(restored.Poll(1,1))==0x80020010u);
    std::uint32_t next=0;assert(!restored.Create(&next,0,1,1) && next==image.next_id);
    // A non-fresh restore must preserve the current kernel's state.
    assert(!restored.Restore(image,error));assert(!restored.Poll(next,1));
    assert(!restored.Delete(1) && !restored.Delete(next));
    puts("Checkpoint semaphores: fresh-process wait rebinding, FIFO first-fit, tokens and IDs restored");
}
