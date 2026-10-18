// Param helpers: the equipment params' shared fields and the SoloParamRepository holder table.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

// Typed row structs (EQUIP_PARAM_WEAPON_ST, ...) are generated from the game's
// paramdefs into bbhost/params.hpp; here they are only forward-declared, so
// this header works without it. They are expected in namespace bb.
struct EQUIP_PARAM_ACCESSORY_ST;
struct EQUIP_PARAM_GOODS_ST;
struct EQUIP_PARAM_PROTECTOR_ST;
struct EQUIP_PARAM_WEAPON_ST;

// ---- EquipParam: fields shared across the four equipment params ----------

// Which equipment row an EquipParamRef views.
enum class EquipParamKind : std::uint8_t {
    Accessory,
    Goods,
    Protector,
    Weapon,
};

// Byte offsets of the shared fields in one row type (from the paramdef
// layouts). is_deposit is one bit; icon_id is absent (npos) on protectors,
// which have separate male/female icons.
struct EquipParamFieldOffsets {
    static constexpr std::size_t npos = ~std::size_t{0};
    std::size_t weight;          // f32
    std::size_t sell_value;      // i32
    std::size_t sort_id;         // i32
    std::size_t is_deposit_byte;
    std::uint8_t is_deposit_bit;
    // EquipParamWithIcon (accessory, goods, weapon).
    std::size_t icon_id;         // u16
    std::size_t trophy_seq_id;   // i16
};

inline constexpr EquipParamFieldOffsets EQUIP_PARAM_FIELD_OFFSETS[] = {
    /* Accessory 0x40  */ {0x08, 0x14, 0x18, 0x3c, 0, 0x22, 0x28},
    /* Goods     0x70  */ {0x08, 0x10, 0x1c, 0x45, 4, 0x2c, 0x34},
    /* Protector 0x10c */ {0x20, 0x1c, 0x00, 0xd8, 0, EquipParamFieldOffsets::npos, 0x102},
    /* Weapon    0x13c */ {0x0c, 0x1c, 0x04, 0x103, 7, 0xba, 0xda},
};

// Row type -> kind; specialized only for the four equipment row types.
template <typename P>
struct EquipParamKindOf;
template <>
struct EquipParamKindOf<EQUIP_PARAM_ACCESSORY_ST> {
    static constexpr EquipParamKind value = EquipParamKind::Accessory;
};
template <>
struct EquipParamKindOf<EQUIP_PARAM_GOODS_ST> {
    static constexpr EquipParamKind value = EquipParamKind::Goods;
};
template <>
struct EquipParamKindOf<EQUIP_PARAM_PROTECTOR_ST> {
    static constexpr EquipParamKind value = EquipParamKind::Protector;
};
template <>
struct EquipParamKindOf<EQUIP_PARAM_WEAPON_ST> {
    static constexpr EquipParamKind value = EquipParamKind::Weapon;
};

// A view of any equipment row through its shared fields.
class EquipParamRef {
public:
    EquipParamRef(void* row, EquipParamKind kind) : row_(static_cast<std::uint8_t*>(row)), kind_(kind) {}
    template <typename P>
    explicit EquipParamRef(P* row) : EquipParamRef(static_cast<void*>(row), EquipParamKindOf<P>::value) {}

    EquipParamKind kind() const { return kind_; }
    void* row() const { return row_; }

    float weight() const { return at<float>(row_, offsets().weight); }
    void set_weight(float v) const { at<float>(row_, offsets().weight) = v; }
    std::int32_t sell_value() const { return at<std::int32_t>(row_, offsets().sell_value); }
    void set_sell_value(std::int32_t v) const { at<std::int32_t>(row_, offsets().sell_value) = v; }
    std::int32_t sort_id() const { return at<std::int32_t>(row_, offsets().sort_id); }
    void set_sort_id(std::int32_t v) const { at<std::int32_t>(row_, offsets().sort_id) = v; }
    bool is_deposit() const { return (row_[offsets().is_deposit_byte] >> offsets().is_deposit_bit) & 1; }
    void set_is_deposit(bool v) const {
        std::uint8_t& b = row_[offsets().is_deposit_byte];
        std::uint8_t mask = static_cast<std::uint8_t>(1u << offsets().is_deposit_bit);
        b = static_cast<std::uint8_t>(v ? (b | mask) : (b & ~mask));
    }

    // EquipParamWithIcon: false for protectors (no single icon id).
    bool has_icon() const { return offsets().icon_id != EquipParamFieldOffsets::npos; }
    std::uint16_t icon_id() const { return has_icon() ? at<std::uint16_t>(row_, offsets().icon_id) : 0; }
    void set_icon_id(std::uint16_t v) const {
        if (has_icon()) at<std::uint16_t>(row_, offsets().icon_id) = v;
    }
    std::int16_t trophy_seq_id() const { return at<std::int16_t>(row_, offsets().trophy_seq_id); }
    void set_trophy_seq_id(std::int16_t v) const { at<std::int16_t>(row_, offsets().trophy_seq_id) = v; }

    // The row as its concrete type, or null when it is another kind.
    EQUIP_PARAM_ACCESSORY_ST* as_accessory() const { return as<EQUIP_PARAM_ACCESSORY_ST>(); }
    EQUIP_PARAM_GOODS_ST* as_goods() const { return as<EQUIP_PARAM_GOODS_ST>(); }
    EQUIP_PARAM_PROTECTOR_ST* as_protector() const { return as<EQUIP_PARAM_PROTECTOR_ST>(); }
    EQUIP_PARAM_WEAPON_ST* as_weapon() const { return as<EQUIP_PARAM_WEAPON_ST>(); }
    template <typename P>
    P* as() const {
        return kind_ == EquipParamKindOf<P>::value ? reinterpret_cast<P*>(row_) : nullptr;
    }

private:
    const EquipParamFieldOffsets& offsets() const {
        return EQUIP_PARAM_FIELD_OFFSETS[static_cast<std::size_t>(kind_)];
    }
    std::uint8_t* row_;
    EquipParamKind kind_;
};

// ---- Solo params: SoloParamRepository's holder array ---------------------

// Metadata for a solo parameter holder.
struct SoloParamInfo {
    // Runtime parameter name used by ParamResCap.res_cap.name.
    const char* name;
    // Index in SoloParamRepository's holder array.
    std::size_t index;
    // Row-struct name used by the param file (the params.hpp type name).
    const char* struct_name;
};

// 1.09 solo parameters in holder-index order. X(name, row type, index).
#define BB_SOLO_PARAMS(X)                                                  \
    X(EquipParamWeapon, EQUIP_PARAM_WEAPON_ST, 0)                          \
    X(EquipParamProtector, EQUIP_PARAM_PROTECTOR_ST, 1)                    \
    X(EquipParamAccessory, EQUIP_PARAM_ACCESSORY_ST, 2)                    \
    X(EquipParamGoods, EQUIP_PARAM_GOODS_ST, 3)                            \
    X(ReinforceParamWeapon, REINFORCE_PARAM_WEAPON_ST, 4)                  \
    X(ReinforceParamProtector, REINFORCE_PARAM_PROTECTOR_ST, 5)            \
    X(NpcParam, NPC_PARAM_ST, 6)                                           \
    X(AtkParam_Npc, ATK_PARAM_ST, 7)                                       \
    X(AtkParam_Pc, ATK_PARAM_ST, 8)                                        \
    X(NpcThinkParam, NPC_THINK_PARAM_ST, 9)                                \
    X(ObjectParam, OBJECT_PARAM_ST, 10)                                    \
    X(Bullet, BULLET_PARAM_ST, 11)                                         \
    X(BehaviorParam, BEHAVIOR_PARAM_ST, 12)                                \
    X(BehaviorParam_PC, BEHAVIOR_PARAM_ST, 13)                             \
    X(Magic, MAGIC_PARAM_ST, 14)                                           \
    X(SpEffectParam, SP_EFFECT_PARAM_ST, 15)                               \
    X(SpEffectVfxParam, SP_EFFECT_VFX_PARAM_ST, 16)                        \
    X(TalkParam, TALK_PARAM_ST, 17)                                        \
    X(MenuColorTableParam, MENU_PARAM_COLOR_TABLE_ST, 18)                  \
    X(ItemLotParam, ITEMLOT_PARAM_ST, 19)                                  \
    X(MoveParam, MOVE_PARAM_ST, 20)                                        \
    X(CharaInitParam, CHARACTER_INIT_PARAM, 21)                            \
    X(EquipMtrlSetParam, EQUIP_MTRL_SET_PARAM_ST, 22)                      \
    X(FaceGenParam, FACE_GEN_PARAM_ST, 23)                                 \
    X(FaceParam, FACE_PARAM_ST, 24)                                        \
    X(FaceRangeParam, FACE_RANGE_PARAM_ST, 25)                             \
    X(RagdollParam, RAGDOLL_PARAM_ST, 26)                                  \
    X(ShopLineupParam, SHOP_LINEUP_PARAM, 27)                              \
    X(QwcChange, QWC_CHANGE_PARAM_ST, 28)                                  \
    X(QwcJudge, QWC_JUDGE_PARAM_ST, 29)                                    \
    X(GameAreaParam, GAME_AREA_PARAM_ST, 30)                               \
    X(SkeletonParam, SKELETON_PARAM_ST, 31)                                \
    X(CalcCorrectGraph, CACL_CORRECT_GRAPH_ST, 32)                         \
    X(LockCamParam, LOCK_CAM_PARAM_ST, 33)                                 \
    X(ObjActParam, OBJ_ACT_PARAM_ST, 34)                                   \
    X(HitMtrlParam, HIT_MTRL_PARAM_ST, 35)                                 \
    X(KnockBackParam, KNOCKBACK_PARAM_ST, 36)                              \
    X(Wind, WIND_PARAM_ST, 37)                                             \
    X(DecalParam, DECAL_PARAM_ST, 38)                                      \
    X(ActionButtonParam, ACTIONBUTTON_PARAM_ST, 39)                        \
    X(WeaponGenParam, WEAPON_GEN_PARAM_ST, 40)                             \
    X(ProtectorGenParam, PROTECTOR_GEN_PARAM_ST, 41)                       \
    X(GemGenParam, GEM_GEN_PARAM_ST, 42)                                   \
    X(GemeffectParam, GEMEFFECT_PARAM_ST, 43)                              \
    X(GemCategoryParam, GEM_CATEGORY_PARAM_ST, 44)                         \
    X(GemDropDopingParam, GEM_DROP_DOPING_PARAM_ST, 45)                    \
    X(GemDropModifyParam, GEM_DROP_MODIFY_PARAM_ST, 46)                    \
    X(ResidentFxParam, RESIDENT_FX_PARAM_ST, 47)                           \
    X(AiSoundParam, AI_SOUND_PARAM_ST, 48)                                 \
    X(GameMapParam, GAME_MAP_PARAM_ST, 49)                                 \
    X(ReturnPointParam, RETURN_POINT_PARAM_ST, 50)                         \
    X(ItemLotLvdepParam, ITEMLOT_LVDEP_PARAM_ST, 51)                       \
    X(HolygrailExParam, HOLYGRAIL_EX_PARAM_ST, 52)                         \
    X(DungeonFeatureParam, DUNGEON_FEATURE_PARAM_ST, 53)                   \
    X(DungeonSubFeatLotParam, DUNGEON_SUB_FEAT_LOT_PARAM, 54)              \
    X(RitualRequiredMatParam, RITUAL_REQUIRED_MAT_PARAM, 55)               \
    X(CharMakeMenuTopParam, CHARMAKEMENUTOP_PARAM_ST, 56)                  \
    X(CharMakeMenuListItemParam, CHARMAKEMENU_LISTITEM_PARAM_ST, 57)       \
    X(NewMenuColorTableParam, MENU_PARAM_COLOR_TABLE_ST, 58)               \
    X(MenuPropertySpecParam, MENUPROPERTY_SPEC, 59)                        \
    X(MenuPropertyLayoutParam, MENUPROPERTY_LAYOUT, 60)                    \
    X(MenuValueTableParam, MENU_VALUE_TABLE_SPEC, 61)

inline constexpr SoloParamInfo SOLO_PARAMS[] = {
#define BB_SOLO_PARAM_INFO(Name, Row, Index) {#Name, Index, #Row},
    BB_SOLO_PARAMS(BB_SOLO_PARAM_INFO)
#undef BB_SOLO_PARAM_INFO
};
inline constexpr std::size_t SOLO_PARAM_COUNT = sizeof(SOLO_PARAMS) / sizeof(SOLO_PARAMS[0]);

// Holder index by name: SoloParamIndex::NpcParam == 6.
enum class SoloParamIndex : std::size_t {
#define BB_SOLO_PARAM_INDEX(Name, Row, Index) Name = Index,
    BB_SOLO_PARAMS(BB_SOLO_PARAM_INDEX)
#undef BB_SOLO_PARAM_INDEX
};

// The solo params as types:
// solo_param::NpcParam::NAME / INDEX / STRUCT_NAME, and StructType = the
// forward-declared row type, for templates like FD4ParamResCap::get<P>.
#define BB_SOLO_PARAM_ROW_DECL(Name, Row, Index) struct Row;
BB_SOLO_PARAMS(BB_SOLO_PARAM_ROW_DECL)
#undef BB_SOLO_PARAM_ROW_DECL

namespace solo_param {
#define BB_SOLO_PARAM_TYPE(Name, Row, Index)                   \
    struct Name {                                              \
        static constexpr const char* NAME = #Name;             \
        static constexpr std::size_t INDEX = Index;            \
        static constexpr const char* STRUCT_NAME = #Row;       \
        using StructType = ::bb::Row;                          \
    };
BB_SOLO_PARAMS(BB_SOLO_PARAM_TYPE)
#undef BB_SOLO_PARAM_TYPE
}  // namespace solo_param

// Holder index for a runtime param name, or -1.
inline std::ptrdiff_t find_solo_param(const char* name) {
    for (const SoloParamInfo& p : SOLO_PARAMS) {
        const char* a = p.name;
        const char* b = name;
        while (*a && *a == *b) ++a, ++b;
        if (*a == *b) return static_cast<std::ptrdiff_t>(p.index);
    }
    return -1;
}

namespace detail::param_layout {
static_assert(SOLO_PARAM_COUNT == 62, "SOLO_PARAMS size");
static_assert(SOLO_PARAMS[37].index == 37, "SOLO_PARAMS order");
static_assert(solo_param::MenuValueTableParam::INDEX == 61, "MenuValueTableParam::INDEX");
static_assert(static_cast<std::size_t>(SoloParamIndex::NpcParam) == 6, "SoloParamIndex::NpcParam");
}  // namespace detail::param_layout

}  // namespace bb
