// FrpgSaveLoadMan layout and symbols (an ABI/layout inventory, no save policy).
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

inline constexpr Rva FRPG_SAVE_LOAD_MAN_SINGLETON_STORAGE{0x556d440};
inline constexpr Rva FRPG_SAVE_LOAD_MAN_SINGLETON{0x556d438};
inline constexpr Rva FRPG_SAVE_LOAD_MAN_INITIALIZE_SAVE_REQUEST_FN{0x1831270};
inline constexpr Rva FRPG_SAVE_LOAD_MAN_GET_SINGLETON_FN{0x18315e0};
inline constexpr Rva FRPG_SAVE_LOAD_MAN_CLEAR_PENDING_WRITE_FN{0x18314e0};
inline constexpr Rva FRPG_SAVE_LOAD_MAN_SUBMIT_SAVE_WRITE_FN{0x1831810};
inline constexpr Rva FRPG_SAVE_LOAD_MAN_QUEUE_COMPLETION_TASK_FN{0x1833050};

inline constexpr std::size_t FRPG_SAVE_LOAD_MAN_ACTIVE_REQUEST_OFFSET = 0x18;
inline constexpr std::size_t FRPG_SAVE_LOAD_MAN_COMPLETION_TASK_OFFSET = 0x28;
inline constexpr std::size_t FRPG_SAVE_LOAD_MAN_SAVE_FILE_HANDLE_OFFSET = 0x30;
inline constexpr std::size_t FRPG_SAVE_LOAD_MAN_SLOT_INDEX_OFFSET = 0x38;
inline constexpr std::size_t FRPG_SAVE_LOAD_MAN_LAST_SUBMIT_TIME_OFFSET = 0x40;
inline constexpr std::size_t FRPG_SAVE_LOAD_MAN_BIND_ENABLE_OFFSET = 0x48;

inline constexpr std::size_t FRPG_SAVE_LOAD_MAN_CHARACTER_PAYLOAD_BYTES = 0x140000;
inline constexpr std::size_t FRPG_SAVE_LOAD_MAN_SYSTEM_PAYLOAD_BYTES = 0x40000;
inline constexpr std::uint32_t FRPG_SAVE_LOAD_MAN_FIRST_PAYLOAD_LANE = 7;
inline constexpr std::uint32_t FRPG_SAVE_LOAD_MAN_LAST_CHARACTER_PAYLOAD_LANE = 0x10;
inline constexpr std::uint32_t FRPG_SAVE_LOAD_MAN_SYSTEM_PAYLOAD_LANE = 0x11;

struct FrpgSaveLoadMan {
    void* vtable;
    void* character_request_source;
    void* system_request_source;
    void* active_save_request;
    void* prior_save_request;
    void* completion_task;
    void* save_file_handle;
    std::uint32_t slot_index;
    std::uint32_t unknown_003c;
    std::uint64_t last_submit_time_us;
    std::uint8_t save_data_bind_enabled;
    Unknown<0x07> _unknown_0049;
    void* request_allocator;
};

namespace detail::frpg_save_load_man_layout {
BB_SIZE(FrpgSaveLoadMan, 0x58);
BB_OFFSET(FrpgSaveLoadMan, character_request_source, 0x08);
BB_OFFSET(FrpgSaveLoadMan, system_request_source, 0x10);
BB_OFFSET(FrpgSaveLoadMan, active_save_request, 0x18);
BB_OFFSET(FrpgSaveLoadMan, prior_save_request, 0x20);
BB_OFFSET(FrpgSaveLoadMan, completion_task, 0x28);
BB_OFFSET(FrpgSaveLoadMan, save_file_handle, 0x30);
BB_OFFSET(FrpgSaveLoadMan, slot_index, 0x38);
BB_OFFSET(FrpgSaveLoadMan, last_submit_time_us, 0x40);
BB_OFFSET(FrpgSaveLoadMan, save_data_bind_enabled, 0x48);
BB_OFFSET(FrpgSaveLoadMan, request_allocator, 0x50);
BB_OFFSET(FrpgSaveLoadMan, active_save_request, FRPG_SAVE_LOAD_MAN_ACTIVE_REQUEST_OFFSET);
BB_OFFSET(FrpgSaveLoadMan, completion_task, FRPG_SAVE_LOAD_MAN_COMPLETION_TASK_OFFSET);
BB_OFFSET(FrpgSaveLoadMan, save_file_handle, FRPG_SAVE_LOAD_MAN_SAVE_FILE_HANDLE_OFFSET);
BB_OFFSET(FrpgSaveLoadMan, slot_index, FRPG_SAVE_LOAD_MAN_SLOT_INDEX_OFFSET);
BB_OFFSET(FrpgSaveLoadMan, last_submit_time_us, FRPG_SAVE_LOAD_MAN_LAST_SUBMIT_TIME_OFFSET);
BB_OFFSET(FrpgSaveLoadMan, save_data_bind_enabled, FRPG_SAVE_LOAD_MAN_BIND_ENABLE_OFFSET);
}  // namespace detail::frpg_save_load_man_layout

}  // namespace bb
