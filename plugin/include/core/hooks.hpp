// Hooks definitions for Fates-3GX-SDK.
//
// This header centralises the declaration of every hook known to the
// plugin. MUST be kept in sync whenever hook sites are added or removed. 
//  The enumerations defined here establish a stable ordering for hook IDs,
// allowing fixed‑size arrays for counters, handler lookup tables and
// metadata.  See docs/TODOLATER.md for details.

#pragma once

#include <cstdint>
#include <cstddef>

namespace Fates {

// Enumerates every hook supported by the plugin.  The order here is
// significant: it determines the indices used in gHookCount and other
// arrays.  Update HookId_Count whenever adding or removing entries.
// Refactored - 11/18/25
enum HookId : std::uint16_t {
    HookId_BTL_HitCalc_Main = 0,
    HookId_BTL_CritCalc_Main,
    HookId_BTL_FinalDamage_Pre,
    HookId_BTL_FinalDamage_Post,
    HookId_BTL_GuardGauge_Add,
    HookId_BTL_GuardGauge_Spend,
    HookId_SEQ_HpDamage,
	HookId_UNIT_HpDamage,
	HookId_UNIT_UpdateCloneHP,
    HookId_HP_KillCheck,
    HookId_SEQ_HpDamage_Helper,
	HookId_SEQ_ItemGain,        
    HookId_MAP_ProcSkillDamage,
    HookId_MAP_ProcTerrainDamage,
    HookId_MAP_ProcTrickDamage,
    HookId_EVENT_ActionEnd,
    HookId_BTL_AttackStance_Check,
    HookId_BTL_AttackStance_ApplySupport,
    HookId_HUD_Battle_HPGaugeUpdate,
    HookId_BTL_SkillEffect_Apply,
	HookId_SYS_Rng32,
    HookId_SEQ_TurnBegin,
    HookId_SEQ_TurnEnd,
    HookId_SEQ_MapEnd,
	HookId_SEQ_MapStart,
	HookId_SEQ_ItemUse,
    HookId_UNIT_LevelUp,
    HookId_UNIT_SkillLearn,
	HookId_SEQ_UnitMove,


    
	HookId_Count
};


// Labels the stability of hooks, Optional means hook is off, will expand upon in future revisions.
enum class HookStability : std::uint8_t {
    Core,
    Optional,
    Experimental
};

// Encapsulates all compile‑time metadata for a hook.  The plugin uses
// this structure to initialise CTRPF hooks and to emit debug
// information at runtime.
struct HookEntry {
    HookId        id;        // enumeration for indexing
    const char*   name;      // human‑readable name (same as id string)
    std::uint32_t targetVA;  // virtual address in code.bin (0x00100000 base)
    std::uint32_t fileOffset;// raw file offset into code.bin
    std::uint32_t guard[3];  // first three 32‑bit words of machine code
    bool          isThumb;   // true if the target executes in Thumb mode
    HookStability stability; // core/optional/experimental
};

// Extern declarations for the hook table and count.  These are
// defined in plugin/src/hooks_table.cpp.
extern const HookEntry kHooks[];
extern const std::size_t kNumHooks;

} // namespace Fates
