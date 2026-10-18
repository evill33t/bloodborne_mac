#pragma once

#include "guest_abi.h"

#include <cstddef>
#include <cstdint>
#include <string>

struct ElfImage;

void hle_register_all();
void hle_patch_guest(ElfImage* image);
void hle_set_guest_stack(void* base, std::size_t size);
void* hle_thread_enter_guest();
void hle_thread_leave_guest(void* tcb);
std::uint64_t hle_make_stub(const std::string& name, const std::string& nid, std::uint64_t got_va);
void* hle_wrap_fn(void* fn);

GUEST_ABI int hle_unknown();
