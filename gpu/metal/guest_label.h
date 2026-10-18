// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "src/runtime_gpu_memory.h"
#include <cstdint>
#include <mach/mach.h>
#include <mach/mach_vm.h>

namespace Gpu::Metal {
// Runtime mappings use a lifetime token and a locked backing alias. Permanent
// loader/host label allocations use checked Mach copies, never raw dereferences.
class GuestLabel {
public:
    bool Capture(std::uint64_t address,std::uint32_t bytes) {
        lease={}; region_start=region_size=0;
        if (!address || (bytes!=4 && bytes!=8) || address%bytes || address>UINT64_MAX-bytes) return false;
        if (runtime_gpu_label_capture(address,bytes,&lease)) return true;
        lease.address=address;lease.bytes=bytes;
        return Region(false);
    }
    bool Load(std::uint64_t& value) const {
        if (lease.identity) return runtime_gpu_label_load(&lease,&value);
        if (!Region(false)) return false;
        value=0;mach_vm_size_t copied=0;
        return mach_vm_read_overwrite(mach_task_self(),lease.address,lease.bytes,
            reinterpret_cast<mach_vm_address_t>(&value),&copied)==KERN_SUCCESS && copied==lease.bytes;
    }
    bool Store(std::uint64_t value) const {
        if (lease.identity) return runtime_gpu_label_store(&lease,value);
        if (!Region(true)) return false;
        return mach_vm_write(mach_task_self(),lease.address,reinterpret_cast<vm_offset_t>(&value),lease.bytes)==KERN_SUCCESS;
    }
private:
    bool Region(bool write) const {
        if (!lease.address || !lease.bytes) return false;
        mach_vm_address_t address=lease.address;mach_vm_size_t size=0;
        vm_region_basic_info_data_64_t info{};
        mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;mach_port_t object=MACH_PORT_NULL;
        auto result=mach_vm_region(mach_task_self(),&address,&size,VM_REGION_BASIC_INFO_64,
            reinterpret_cast<vm_region_info_t>(&info),&count,&object);
        if (object) mach_port_deallocate(mach_task_self(),object);
        if (result!=KERN_SUCCESS || address>lease.address || lease.bytes>size || lease.address-address>size-lease.bytes ||
            !(info.protection & (write ? VM_PROT_WRITE : VM_PROT_READ))) return false;
        if (region_size && (region_start!=address || region_size!=size)) return false;
        region_start=address;region_size=size;return true;
    }
    BbGpuLabel lease{};
    mutable mach_vm_address_t region_start=0;
    mutable mach_vm_size_t region_size=0;
};
}
