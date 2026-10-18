/* SPDX-License-Identifier: GPL-2.0-or-later
 * Compile the production VM implementation as a standalone provider fixture. */
#include "../src/runtime_memory.c"
#include <assert.h>

int32_t *runtime_errno(void) {static _Thread_local int32_t e;return &e;}
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name) {
    for(size_t k=0;k<count;++k)if(!strcmp(table[k].name,name))return (uintptr_t)table[k].function;
    return 0;
}
void bb_checkpoint_memory_fixture_setup(uint64_t locations[4]) {
    int64_t physical=-1;
    assert(!direct_allocate(0,POOL_SIZE,PAGE*3,0,3,&physical) && physical==0);
    void *a=NULL,*b=NULL,*flex=NULL,*reserved=NULL;
    assert(!direct_map(&a,PAGE*3,3,0,physical,0));
    assert(!direct_map(&b,PAGE,3,0,physical+PAGE,0));
    memset(a,0x11,PAGE);memset((char *)a+PAGE,0x22,PAGE);memset((char *)a+PAGE*2,0x33,PAGE);
    assert(!map_flexible(&flex,PAGE*3,3,0));
    memset(flex,0x51,PAGE);memset((char *)flex+PAGE,0x52,PAGE);memset((char *)flex+PAGE*2,0x53,PAGE);
    assert(!kernel_mprotect((char *)flex+PAGE,PAGE,1));
    assert(!reserve_range(&reserved,PAGE*2,0,0));
    locations[0]=(uintptr_t)a;locations[1]=(uintptr_t)b;locations[2]=(uintptr_t)flex;locations[3]=(uintptr_t)reserved;
}
void bb_checkpoint_memory_fixture_verify(const uint64_t locations[4]) {
    assert(runtime_memory_is_mapped(locations[0],PAGE*3));
    assert(runtime_memory_is_mapped(locations[1],PAGE));
    assert(runtime_memory_is_mapped(locations[2],PAGE*3));
    VirtualQueryInfo info;
    assert(!virtual_query((void *)locations[0],0,&info,sizeof(info)) && info.protection==3 && info.offset==0);
    assert(!virtual_query((void *)locations[1],0,&info,sizeof(info)) && info.protection==3 && info.offset==PAGE);
    assert(!virtual_query((void *)(locations[2]+PAGE),0,&info,sizeof(info)) && info.protection==1 && (info.flags&1));
    assert(!virtual_query((void *)locations[3],0,&info,sizeof(info)) && info.protection==0 && !info.flags);
    unsigned char *a=(void *)locations[0],*b=(void *)locations[1],*flex=(void *)locations[2];
    for(size_t k=0;k<PAGE;++k) {
        assert(a[k]==0x11 && a[PAGE+k]==0x22 && a[PAGE*2+k]==0x33 && b[k]==0x22);
        assert(flex[k]==0x51 && flex[PAGE+k]==0x52 && flex[PAGE*2+k]==0x53);
    }
    b[0]=0x78;assert(a[PAGE]==0x78);a[PAGE+1]=0x79;assert(b[1]==0x79);
    void *next=NULL;assert(!map_flexible(&next,PAGE,3,0));memset(next,0x87,PAGE);
    assert(flex[0]==0x51 && flex[PAGE]==0x52 && flex[PAGE*2]==0x53);
    assert(!release_flexible(next,PAGE));
}
