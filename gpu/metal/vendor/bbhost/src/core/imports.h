#pragma once

#include <cstdint>
#include <string>
#include <string_view>

std::string lookup_nid_name(std::string_view nid);
std::uint64_t bind_import(const std::string& name, const std::string& nid, std::uint64_t got_va,
                         int plt_index = -1);
std::uint64_t bind_glob_dat(const std::string& name, const std::string& nid, std::uint64_t elf_offset,
                           unsigned st_info);
void register_hle();
void register_hle_fn(const char* name, void* fn);
// BBHOST_HLE_COUNT=1: logs the HLE calls since the last report by thread and function.
void hle_call_counts_report();
// Bound without the FS/stack thunk: fn runs with guest FS on the guest stack.
void register_hle_fn_raw(const char* name, void* fn);
// The registered name of an HLE function, or nullptr.
const char* hle_fn_name(void* fn);
std::uint64_t hle_make_stub(const std::string& name, const std::string& nid, std::uint64_t got_va);
