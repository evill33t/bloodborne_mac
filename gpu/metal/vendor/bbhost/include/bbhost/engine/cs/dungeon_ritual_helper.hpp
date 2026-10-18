// Bloodborne 1.09 ritual-helper layout, commands, and generated-dungeon data.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

// Descriptive fixed offering row, initialized to (-1, 0, 0). Native record rows
// supply required/offered counts; a ready record can rebuild requirements from
// params and copy required into offered. Packed material IDs use the goods
// category (0x40000000). The currency row keeps the -1 item sentinel.
struct DungeonRitualOffering {
    std::uint32_t item_id_raw;
    std::uint32_t required_count;
    std::uint32_t offered_count;
};

// Descriptive 0x14-byte name-ID cache, rebuilt from HolygrailExParam and
// DungeonFeatureParam. The base name receives one priority-selected modifier;
// up to three short-name IDs are retained. IDs initialize to -1, count to 0.
struct DungeonRitualNameIds {
    std::int32_t name_id;
    std::int32_t short_name_ids[3];
    std::uint32_t short_name_count;
};

// Indices from registration, which is authoritative over stale Ghidra names.
// The zero-filled table row after Finish is not a named step.
enum class CSDungeonRitualStep : std::int32_t {
    Init = 0,
    WaitForRequest = 1,
    ExecForSuspend = 2,
    ExecForResume = 3,
    WaitForResume = 4,
    ExecUploadForResume = 5,
    WaitUploadForResume = 6,
    FinishForResume = 7,
    ExecForSetupHolygrail = 8,
    ExecForJoin = 9,
    ExecForRandomJoin = 10,
    WaitForRandomJoin = 11,
    FinishForRandomJoin = 12,
    ExecForQuickJoin = 13,
    ExecForCancel = 14,
    ExecForPresentOffering = 15,
    WaitForPresentOffering = 16,  // callback RVA 0x1aa8810
    ExecUploadForPresentOffering = 17,
    WaitUploadForPresentOffering = 18,
    ExecGetForPresentOffering = 19,
    WaitGetForPresentOffering = 20,
    FinishForPresentOffering = 21,
    ExecForChangeShareLevel = 22,
    WaitUploadForChangeShareLevel = 23,
    WaitForChangeShareLevel = 24,
    ExecGetForChangeShareLevel = 25,
    WaitGetForChangeShareLevel = 26,
    FinishForChangeShareLevel = 27,
    ExecForErrorRecovery = 28,
    StateSwitch = 29,
    Finish = 30,
};

// The Makeshift Altar / Short Ritual Root Chalice uses the transient QUICK
// altar bank. The six ordinary generated altars use banks 1..=6.
inline constexpr std::uint32_t SHORT_ROOT_ALTAR_SLOT = 0;
inline constexpr std::uint32_t DUNGEON_ALTAR_SLOT_COUNT = 7;
inline constexpr std::uint32_t DUNGEON_ALTAR_FLAG_STRIDE = 100000;
inline constexpr std::uint32_t DUNGEON_ALTAR_SELECTOR_FLAG = 0x233c;
inline constexpr std::uint32_t DUNGEON_ALTAR_SELECTOR_BIT_COUNT = 7;

// Gameplay altar interaction is TalkESD, not SprjDungeonSelectStep (that one
// is built by the debug boot menu's DUNGEON_SELECT choice). t210050 is Short
// Root; t210060..t210110 in steps of ten are banks 1..6. In each script, group
// 2147483645 evaluates action predicate 56 at state 2; predicate 14 resumes in
// this wait group after the altar conversation ends. Command 1:72 calls
// SelectDungeonAltar to select the bank and remap flags.
inline constexpr std::uint32_t DUNGEON_ALTAR_TALK_FIRST = 210050;
inline constexpr std::uint32_t DUNGEON_ALTAR_TALK_LAST = 210110;
inline constexpr std::uint32_t DUNGEON_ALTAR_TALK_STRIDE = 10;
inline constexpr Rva DUNGEON_ALTAR_SELECT_ADDRESS{0x13b89b0};

inline constexpr std::uint32_t DUNGEON_ALTAR_MAP_UID_FLAG = 0x59a5380;
inline constexpr std::uint32_t DUNGEON_ALTAR_DATABASE_ID_FLAG = 0x59a53a0;
inline constexpr std::uint32_t DUNGEON_ALTAR_MATCH_UID_FLAG = 0x59a53e0;
inline constexpr std::uint32_t DUNGEON_ALTAR_HOLYGRAIL_EX_PARAM_FLAG = 0x59a5400;
inline constexpr std::uint32_t DUNGEON_ALTAR_FEATURE_PAIRS_FLAG = 0x59a5420;
inline constexpr std::uint32_t DUNGEON_ALTAR_MATCH_METADATA_FLAG = 0x59a5660;
// Historical name: stores the payload creator label, not the server glyph.
inline constexpr std::uint32_t DUNGEON_ALTAR_GLYPH_FLAG = 0x59a56e0;
inline constexpr std::uint32_t DUNGEON_ALTAR_RITUAL_METADATA_FLAG = 0x59a5760;

inline constexpr std::uint32_t DUNGEON_MATCH_RESULT_LOCAL_SIZE = 0x7c;
inline constexpr std::uint32_t DUNGEON_MATCH_RESULT_SERVER_SIZE = 0x8c;
inline constexpr std::uint32_t DUNGEON_MATCH_RESULT_LOCAL_TYPE = 4;
inline constexpr std::uint32_t DUNGEON_MATCH_RESULT_SERVER_TYPE = 5;

// The four exact fields submitted by the Short Root random-search path to the
// application-layer ChannelRandomJoin request (operation 0x12). Co-op versus
// hostile session intent is not present in this payload.
struct DungeonRandomJoinCriteria {
    std::uint32_t holy_grail_type_id;
    std::uint32_t ritual_level;
    std::uint32_t fixed_or_general;
    std::uint32_t sub_feature_flag;
};

// Native channel database records are allocated as 0x130-byte objects and
// keyed by the 64-bit database/channel identifier at +0x10.
inline constexpr std::size_t DUNGEON_CHANNEL_RECORD_SIZE = 0x130;
inline constexpr std::size_t DUNGEON_CHANNEL_DATABASE_ID_OFFSET = 0x10;
inline constexpr std::size_t DUNGEON_CHANNEL_SHARE_LEVEL_OFFSET = 0x50;
inline constexpr std::size_t DUNGEON_CHANNEL_RECORD_STATE_OFFSET = 0x54;
inline constexpr std::size_t DUNGEON_CHANNEL_PAYLOAD_OFFSET = 0x58;
inline constexpr std::size_t DUNGEON_CHANNEL_PAYLOAD_SIZE_OFFSET = 0x60;
inline constexpr std::size_t DUNGEON_CHANNEL_STATUS_OFFSET = 0x110;
// Current Get operation: 1 pending, 2 failed, 3 succeeded. Cached readiness at
// +0x54 can remain 1 while a new Get is pending; it is not a completion fence.
// GetFinish/GetErrorFinish at RVA 0x1e6ae20/0x1e6ae90 write 3/2 here.
inline constexpr std::size_t DUNGEON_CHANNEL_GET_COMPLETION_OFFSET = 0x114;
inline constexpr std::size_t DUNGEON_CHANNEL_UPLOAD_SHARE_OFFSET = 0x11c;
// Share operation completion state, NOT the privacy value at +0x50.
inline constexpr std::size_t DUNGEON_CHANNEL_SHARE_OFFSET = 0x120;
inline constexpr std::size_t DUNGEON_CHANNEL_TRANSACTION_REQUEST_ID_OFFSET = 0xd8;
inline constexpr std::uint32_t CHANNEL_UPLOAD_OPERATION = 0x0c;
inline constexpr Rva SPRJ_CHANNEL_DB_REQUEST_STEP_UPLOAD_ADDRESS{0x1e6af20};
inline constexpr Rva SPRJ_CHANNEL_DB_REQUEST_SUBMIT_UPLOAD_ADDRESS{0x1e6b040};
inline constexpr Rva SPRJ_NETWORK_CLIENT_BUILD_CHANNEL_UPLOAD_REQUEST_ADDRESS{0x1e9af40};

// Scalar fields copied from a generated-dungeon channel record into the native
// ChannelUploadRequest. FormData, material, and unlock lists are owned
// containers and are intentionally not represented here.
struct DungeonChannelUploadMetadata {
    std::uint32_t ritual_level;
    std::uint32_t fixed_or_general;
    std::uint32_t share_level;
    std::uint32_t status;
    std::uint32_t sub_feature_flag;
    std::uint32_t holy_grail_type_id;
    std::uint32_t form_data_version;
};

// State numbers installed by the native SprjChannelDbRequestStep table at RVA
// 0x1e6e670. The table contains 52 populated entries (0..=51).
enum class SprjChannelDbRequestState : std::uint32_t {
    Init = 0,
    Wait = 1,
    GetInit = 2,
    Get = 3,
    GetWait = 4,
    GetFinish = 5,
    GetErrorFinish = 6,
    UploadInit = 7,
    Upload = 8,
    UploadWait = 9,
    UploadFinish = 10,
    UploadErrorFinish = 11,
    UploadShareInit = 12,
    UploadShareUpload = 13,
    UploadShareUploadWait = 14,
    UploadShare = 15,
    UploadShareWait = 16,
    UploadShareFinish = 17,
    UploadShareErrorFinish = 18,
    ShareInit = 19,
    Share = 20,
    ShareWait = 21,
    ShareFinish = 22,
    ShareErrorFinish = 23,
    AddMaterialInit = 24,
    AddMaterial = 25,
    AddMaterialWait = 26,
    AddMaterialCompleteNotify = 27,
    AddMaterialCompleteNotifyWait = 28,
    AddMaterialFinish = 29,
    AddMaterialErrorFinish = 30,
    RandomJoinInit = 31,
    RandomJoin = 32,
    RandomJoinWait = 33,
    RandomJoinFinish = 34,
    RandomJoinErrorFinish = 35,
    SearchInit = 36,
    Search = 37,
    SearchWait = 38,
    SearchFinish = 39,
    SearchErrorFinish = 40,
    UniqueChannelSearchInit = 41,
    UniqueChannelSearch = 42,
    UniqueChannelSearchWait = 43,
    UniqueChannelSearchFinish = 44,
    UniqueChannelSearchErrorFinish = 45,
    KeywordSearchInit = 46,
    KeywordSearch = 47,
    KeywordSearchWait = 48,
    KeywordSearchFinish = 49,
    KeywordSearchErrorFinish = 50,
    Finish = 51,
};

// One generated-dungeon feature key/value pair. Native match results carry
// nine of these pairs and copy them into the selected altar's flag bank.
struct DungeonFeaturePair {
    std::uint32_t key;
    std::uint32_t value;
};

// Common prefix accepted by the native generated-dungeon result parser at RVA
// 0x1a9c490. Type 5 carries creator-label text (not the server's
// discernment-word/glyph); type 4 obtains that text from local game data. The
// prefix ends at 0x68. Type 5 reads a terminated UTF-16 creator name there;
// bytes after its terminator through 0x8b are not parsed, and live resumed
// records can differ from server FormData in that tail, so comparing all 0x8c
// bytes is not a valid semantic identity check.
struct DungeonMatchResultPrefix {
    std::uint32_t result_type;
    std::uint32_t result_size;
    std::uint32_t generated_map_uid;
    std::uint32_t holygrail_ex_param_id;
    DungeonFeaturePair feature_pairs[9];
    std::uint8_t match_metadata[16];
};

// Inline 0xc0-byte selection beginning at CSDungeonRitualHelper +0xc8. Its
// record and creator-label pointers are retained native references.
struct DungeonRitualSelection {
    std::uint32_t altar_slot;
    std::uint32_t _pad_cc;
    void* result_object;
    // Stored bits; native signed comparisons distinguish local negative IDs
    // from published nonnegative IDs. INT64_MIN is the no-selection sentinel.
    std::uint64_t database_id;
    std::uint32_t generated_map_uid;
    std::uint32_t _pad_e4;
    std::uint32_t holygrail_ex_param_id;
    std::uint32_t _pad_ec;
    const void* holygrail_ex_param_row;
    DungeonFeaturePair feature_pairs[9];
    std::uint8_t match_metadata[16];
    // Historical name: live generated records contain the creator label, not
    // the server glyph. A retained native object, not a raw string.
    void* glyph_object;
    std::uint8_t ritual_metadata[0x30];
};

inline constexpr std::size_t CSDUNGEON_RITUAL_SELECTION_OFFSET = 0xc8;
inline constexpr std::size_t CSDUNGEON_RITUAL_STATUS_OFFSET = 0x25c;
inline constexpr std::size_t CSDUNGEON_RITUAL_PENDING_COMMAND_OFFSET = 0x260;
inline constexpr std::size_t CSDUNGEON_RITUAL_CURRENT_COMMAND_OFFSET = 0x264;
inline constexpr std::size_t CSDUNGEON_RITUAL_ERROR_OFFSET = 0x268;
inline constexpr std::size_t CSDUNGEON_RITUAL_RETRY_TIME_OFFSET = 0x26c;

inline constexpr Rva DUNGEON_RITUAL_REQUEST_SETUP_ADDRESS{0x1aa47a0};
// Suspend releases helper references without clearing altar flags. Resume
// rebuilds the helper from those flags, including locally generated records.
inline constexpr Rva DUNGEON_RITUAL_REQUEST_SUSPEND_ADDRESS{0x1aa4040};
inline constexpr Rva DUNGEON_RITUAL_REQUEST_RESUME_ADDRESS{0x1aa40e0};
inline constexpr Rva DUNGEON_RITUAL_REQUEST_JOIN_ADDRESS{0x1aa4a10};
inline constexpr Rva DUNGEON_RITUAL_REQUEST_QUICK_JOIN_ADDRESS{0x1aa5280};
inline constexpr Rva DUNGEON_RITUAL_REQUEST_CANCEL_ADDRESS{0x1aa54d0};
inline constexpr Rva DUNGEON_RITUAL_REQUEST_OFFER_ALL_ADDRESS{0x1aa5670};
inline constexpr Rva DUNGEON_RITUAL_REQUEST_SHARE_OPEN_ADDRESS{0x1aa6080};
inline constexpr Rva DUNGEON_RITUAL_UPDATE_ADDRESS{0x1aacfe0};
inline constexpr Rva DUNGEON_RITUAL_CONFIG_INITIALIZE_ADDRESS{0x1aaeec0};
inline constexpr Rva DUNGEON_CHANNEL_REQUEST_GET_ADDRESS{0x1e65290};
// Native Share request: (record reference, share level). Unlike the public
// ritual RequestShareOpen predicate, this does not require Unshared privacy.
inline constexpr Rva DUNGEON_CHANNEL_REQUEST_SHARE_ADDRESS{0x1e65960};

// Cancel resets categories 1/5/6/7 at area 40+bank, block 0, slots 0..9.
// Category 9 stores registration metadata at the same area. Archive all five
// categories to preserve both a personal registration and its progress.
inline constexpr std::uint32_t DUNGEON_ALTAR_STORAGE_CATEGORIES[5] = {1, 5, 6, 7, 9};
inline constexpr std::size_t DUNGEON_ALTAR_STORAGE_BLOCKS = 50;
inline constexpr std::size_t DUNGEON_ALTAR_STORAGE_BLOCK_BYTES = 125;

// SprjHolygrail owns an array of seven owner pointers at +0x08. Each owner
// embeds its helper at +0x108. Native bank 0 is QUICK; 1..=6 are
// GENED0..GENED5. These bank IDs are not physical/debug-menu altar numbers.
inline constexpr std::size_t SPRJ_HOLYGRAIL_OWNER_ARRAY_OFFSET = 0x08;
inline constexpr std::size_t DUNGEON_ALTAR_OWNER_HELPER_OFFSET = 0x108;
inline constexpr std::size_t DUNGEON_ALTAR_OWNER_DEBUG_CONFIG_OFFSET = 0x3a0;

// Derived by StateSwitch (RVA 0x1aaa3d0), independently of the running step.
enum class DungeonRitualStatus : std::uint32_t {
    // Initial constructor state and the result of Suspend; Resume admits this.
    Suspended = 0,
    Empty = 1,
    LocalIncomplete = 2,
    ServerIncomplete = 3,
    LocalReady = 4,
    ServerReady = 5,
    Error = 6,
};

// Native record +0x50. Successful setup/upload alone can leave this Unshared.
// Party registration requires Open after native Share and its refresh finish.
enum class DungeonShareLevel : std::uint32_t {
    Unshared = 0,
    Closed = 1,
    Open = 2,
};

// One fixed, optional or group feature choice in the native setup descriptor.
struct DungeonRitualFeatureChoice {
    std::uint8_t available;
    std::uint8_t selected;
    std::uint8_t choice_count;
    std::uint8_t selected_choice;
    std::int32_t sub_feature_lot_id;
};

// Initialized by RVA 0x1aaeec0 on a native game thread using the native RNG.
// The setup request generates its map UID, record, payload and metadata from
// this descriptor. Setup alone does not complete the offering/upload work.
struct DungeonRitualSetup {
    std::int32_t holygrail_ex_param_id;
    std::uint32_t _pad04;
    const void* holygrail_ex_param_row;
    std::uint32_t variation;
    DungeonRitualFeatureChoice choices[13];
};

// Encode a native bank in flag range 0x233c..0x2342 (MSB-first). False when
// bank >= 7. The offering completion/reset code reads this global selector,
// not just helper+0xc8: save/set/restore it around each remote helper update on
// the game thread; do not leave a different selection installed between frames.
inline constexpr bool dungeon_altar_selector(std::uint32_t bank, std::uint32_t& out) {
    if (bank >= DUNGEON_ALTAR_SLOT_COUNT) return false;
    out = 1u << (6 - bank);
    return true;
}

// Checked form of SprjHolygrail's selected-helper lookup (RVA 0x1ac3e70). For a
// nonzero seven-bit value, the highest set bit selects bank 0..6; multiple set
// bits follow the native priority. Zero (native would index bank seven) and
// values outside the seven-bit field return false.
inline constexpr bool dungeon_altar_bank_from_selector(std::uint32_t selector, std::uint32_t& out) {
    if (selector == 0 || selector >= (1u << DUNGEON_ALTAR_SELECTOR_BIT_COUNT)) return false;
    std::uint32_t bits = 0;
    while (selector >> bits) ++bits;
    out = DUNGEON_ALTAR_SLOT_COUNT - bits;
    return true;
}

// Zero means no command; unrecognized native values stay in the raw fields.
enum class DungeonRitualCommand : std::uint32_t {
    Suspend = 1,
    Resume = 2,
    SetupHolygrail = 3,
    Join = 4,
    RandomJoin = 5,
    QuickJoin = 6,
    Cancel = 7,
    PresentOffering = 8,
    ShareLevel = 9,
    RecoverFromError = 10,
};

// WaitForRequest's dispatch mapping; this does not issue a native request.
inline constexpr CSDungeonRitualStep entry_step(DungeonRitualCommand command) {
    switch (command) {
        case DungeonRitualCommand::Suspend: return CSDungeonRitualStep::ExecForSuspend;
        case DungeonRitualCommand::Resume: return CSDungeonRitualStep::ExecForResume;
        case DungeonRitualCommand::SetupHolygrail: return CSDungeonRitualStep::ExecForSetupHolygrail;
        case DungeonRitualCommand::Join: return CSDungeonRitualStep::ExecForJoin;
        case DungeonRitualCommand::RandomJoin: return CSDungeonRitualStep::ExecForRandomJoin;
        case DungeonRitualCommand::QuickJoin: return CSDungeonRitualStep::ExecForQuickJoin;
        case DungeonRitualCommand::Cancel: return CSDungeonRitualStep::ExecForCancel;
        case DungeonRitualCommand::PresentOffering: return CSDungeonRitualStep::ExecForPresentOffering;
        case DungeonRitualCommand::ShareLevel: return CSDungeonRitualStep::ExecForChangeShareLevel;
        case DungeonRitualCommand::RecoverFromError: return CSDungeonRitualStep::ExecForErrorRecovery;
    }
    return CSDungeonRitualStep::WaitForRequest;  // not a native command
}

// Reflected 0x270-byte local step embedded at SprjDungeonGateIns +0x108.
// Constructor RVA 0x1aa3110 and size getter 0x1aad5a0 agree on its extent; the
// owner constructs and destroys it inline, without a separate allocation.
//
// Selection holds retained record and creator-label references. Destruction
// unreferences both before template/debug cleanup; it does not cancel an altar
// registration or erase persisted flags. Suspend likewise differs from Cancel.
//
// Request methods place a command in pending_command_raw, dispatch virtual
// +0xd0 with an FD4Time of about 1/30, then clear the pending field.
// WaitForRequest consumes it into current_command_raw and selects the
// command's first step. Asynchronous waits require later native updates.
// ExecUploadForResume additionally waits while SprjHolygrail +0x10 is zero.
//
// Status is independent of the running callback and does not encode privacy.
// The named Finish callback is a no-op, not a request for terminal index -1.
struct CSDungeonRitualHelper {
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_DUNGEON_RITUAL_HELPER_RUNTIME_CLASS;
    // STEP_TEMPLATE: 31 steps; registration clears 32 rows and the runtime class pointer follows them.
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = CS_DUNGEON_RITUAL_HELPER_TEMPLATE;
    static constexpr std::size_t SIZE = 0x270;
    static constexpr Rva VTABLE{0x5336390};
    static constexpr Rva CONSTRUCTOR_FN{0x1aa3110};
    static constexpr Rva DESTRUCTOR_FN{0x1aa3680};
    static constexpr Rva DELETING_DESTRUCTOR_FN{0x1aa3660};
    static constexpr Rva SIZE_GETTER_FN{0x1aad5a0};
    static constexpr Rva DISPATCH_FN = DUNGEON_RITUAL_UPDATE_ADDRESS;
    static constexpr Rva DISPATCH_BODY_FN{0x1aacca0};
    static constexpr Rva IS_FINISHED_FN{0x1aac1b0};
    static constexpr Rva ADVANCE_FN{0x1aadfc0};
    static constexpr Rva PARSE_RECORD_FN{0x1aa3b90};
    static constexpr Rva REFRESH_OFFERINGS_FN{0x1a9bf50};
    static constexpr Rva BUILD_OFFERINGS_FROM_PARAMS_FN{0x1ab0780};
    static constexpr Rva COPY_OFFERINGS_FROM_RECORD_FN{0x1ab0fd0};
    static constexpr Rva BUILD_NAME_IDS_FN{0x1aa1c10};
    static constexpr std::size_t CURRENCY_OFFERING_SLOT = 15;

    const void* vftable;
    const void* callback_table;
    Unknown<0x40> _condition_dispatcher10;
    std::int32_t current_step;
    std::int32_t requested_step;
    std::uint8_t continue_this_update;
    Unknown<7> _unk59;
    void* allocator;
    std::uint8_t debug_flags[2];
    Unknown<6> _unk6a;
    void* debug_menu;
    Unknown<0x38> _debug_string78;
    std::int32_t* execution_counts;
    // Conditionally refreshed by the debug dispatcher; may be stale.
    const std::uint16_t* execution_label;
    std::uint8_t debug_step_requested;
    Unknown<3> _unkc1;
    std::int32_t debug_step;
    DungeonRitualSelection selection;
    // Sixteen 0xc-byte rows. Slot 15 is the currency row, with item ID -1;
    // slots 0..14 are material rows. These counts do not deduct inventory.
    DungeonRitualOffering offerings[16];
    DungeonRitualNameIds name_ids;
    std::uint32_t status_raw;
    std::uint32_t pending_command_raw;
    std::uint32_t current_command_raw;
    // -1 means no error. StateSwitch maps any other value to Error status.
    std::int32_t error_raw;
    // Resume initializes 10.0 and subtracts FD4Time +8 during ExecForResume.
    // Not a general timeout for all asynchronous channel operations.
    float resume_wait_remaining;

    bool step_known() const { return current_step >= 0 && current_step <= 30; }
    CSDungeonRitualStep step() const { return static_cast<CSDungeonRitualStep>(current_step); }
    bool status_known() const { return status_raw <= 6; }
    DungeonRitualStatus status() const { return static_cast<DungeonRitualStatus>(status_raw); }
    DungeonRitualCommand pending_command() const { return static_cast<DungeonRitualCommand>(pending_command_raw); }
    DungeonRitualCommand current_command() const { return static_cast<DungeonRitualCommand>(current_command_raw); }
    // Native terminal predicate. The named Finish callback leaves it false.
    bool is_finished() const { return current_step == -1; }
    // Request-processing snapshot only; does not prove registration success,
    // server publication, Open privacy, or that an altar menu has closed.
    bool is_idle() const {
        return current_step == static_cast<std::int32_t>(CSDungeonRitualStep::WaitForRequest) &&
               pending_command_raw == 0 && current_command_raw == 0;
    }
};

namespace detail::dungeon_ritual_helper_layout {
using T = CSDungeonRitualHelper;
BB_SIZE(T, 0x270);
BB_SIZE(T, T::SIZE);
static_assert(alignof(T) == 8, "alignof(CSDungeonRitualHelper)");
BB_OFFSET(T, callback_table, 0x08);
BB_OFFSET(T, _condition_dispatcher10, 0x10);
BB_OFFSET(T, current_step, 0x50);
BB_OFFSET(T, requested_step, 0x54);
BB_OFFSET(T, continue_this_update, 0x58);
BB_OFFSET(T, allocator, 0x60);
BB_OFFSET(T, debug_flags, 0x68);
BB_OFFSET(T, debug_menu, 0x70);
BB_OFFSET(T, _debug_string78, 0x78);
BB_OFFSET(T, execution_counts, 0xb0);
BB_OFFSET(T, execution_label, 0xb8);
BB_OFFSET(T, debug_step_requested, 0xc0);
BB_OFFSET(T, debug_step, 0xc4);
BB_OFFSET(T, selection, CSDUNGEON_RITUAL_SELECTION_OFFSET);
BB_OFFSET(T, offerings, 0x188);
BB_OFFSET(T, name_ids, 0x248);
BB_OFFSET(T, status_raw, CSDUNGEON_RITUAL_STATUS_OFFSET);
BB_OFFSET(T, pending_command_raw, CSDUNGEON_RITUAL_PENDING_COMMAND_OFFSET);
BB_OFFSET(T, current_command_raw, CSDUNGEON_RITUAL_CURRENT_COMMAND_OFFSET);
BB_OFFSET(T, error_raw, CSDUNGEON_RITUAL_ERROR_OFFSET);
BB_OFFSET(T, resume_wait_remaining, CSDUNGEON_RITUAL_RETRY_TIME_OFFSET);
static_assert(DUNGEON_ALTAR_OWNER_HELPER_OFFSET + sizeof(T) == 0x378, "owner helper extent");
BB_SIZE(DungeonRitualSelection, 0xc0);
BB_OFFSET(DungeonRitualSelection, altar_slot, 0xc8 - 0xc8);
BB_OFFSET(DungeonRitualSelection, result_object, 0xd0 - 0xc8);
BB_OFFSET(DungeonRitualSelection, database_id, 0xd8 - 0xc8);
BB_OFFSET(DungeonRitualSelection, generated_map_uid, 0xe0 - 0xc8);
BB_OFFSET(DungeonRitualSelection, holygrail_ex_param_id, 0xe8 - 0xc8);
BB_OFFSET(DungeonRitualSelection, holygrail_ex_param_row, 0xf0 - 0xc8);
BB_OFFSET(DungeonRitualSelection, feature_pairs, 0xf8 - 0xc8);
BB_OFFSET(DungeonRitualSelection, match_metadata, 0x140 - 0xc8);
BB_OFFSET(DungeonRitualSelection, glyph_object, 0x150 - 0xc8);
BB_OFFSET(DungeonRitualSelection, ritual_metadata, 0x158 - 0xc8);
BB_SIZE(DungeonRitualOffering, 0x0c);
static_assert(alignof(DungeonRitualOffering) == 4, "alignof(DungeonRitualOffering)");
BB_OFFSET(DungeonRitualOffering, required_count, 4);
BB_OFFSET(DungeonRitualOffering, offered_count, 8);
static_assert(offsetof(T, offerings) + T::CURRENCY_OFFERING_SLOT * sizeof(DungeonRitualOffering) +
                      offsetof(DungeonRitualOffering, required_count) ==
                  0x240,
              "currency offering required_count");
BB_SIZE(DungeonRitualNameIds, 0x14);
BB_OFFSET(DungeonRitualNameIds, short_name_ids, 4);
BB_OFFSET(DungeonRitualNameIds, short_name_count, 0x10);
BB_SIZE(DungeonRandomJoinCriteria, 16);
BB_SIZE(DungeonMatchResultPrefix, 0x68);  // "the prefix ends at 0x68"
BB_SIZE(DungeonRitualSetup, 0x80);
BB_OFFSET(DungeonRitualSetup, choices, 0x14);
BB_SIZE(DungeonRitualFeatureChoice, 8);
// Channel records are 0x130 bytes (DUNGEON_CHANNEL_RECORD_SIZE); no struct.
static_assert(static_cast<std::uint32_t>(SprjChannelDbRequestState::RandomJoinInit) == 31, "RandomJoinInit");
static_assert(static_cast<std::uint32_t>(SprjChannelDbRequestState::Finish) == 51, "Finish");
}  // namespace detail::dungeon_ritual_helper_layout

}  // namespace bb
