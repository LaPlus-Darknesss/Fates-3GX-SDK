// hooks_table.cpp
//
// Defines the global kHooks array and kNumHooks count used by the
// hook manager.  This table is derived from DOCSTOADDLATER.md and
// provides the runtime addresses, guard patterns and other metadata
// for each supported hook.  Do not edit these values unless you are
// updating to a new version of code.bin or adding/removing hooks.
// ALL entries currently marked Optional are unstable and do not function.
#include "core/hooks.hpp"

namespace Fates {

// Static array containing one entry per hook.  The order must match
// the HookId enumeration in hooks.hpp.
const HookEntry kHooks[] = {
    {
        HookId_BTL_HitCalc_Main,
        "BTL_HitCalc_Main",
        0x003A3588u,
        0x002A3588u,
        { 0xE3A01064u, 0xE92D4070u, 0xE0050190u },
        false,
        HookStability::Core
    },
    {
        HookId_BTL_CritCalc_Main, // Will require deep logic, no clean area to hook, will revisit later.
        "BTL_CritCalc_Main",
        0x0052B988u,
        0x0042B988u,
        { 0xE3710001u, 0xE1A02000u, 0xE92D4010u },
        true,
        HookStability::Optional
    },
    {
        HookId_BTL_FinalDamage_Pre,
        "BTL_FinalDamage_Pre",
        0x00364FCC,
        0x00264FCC,
        { 0x00000000u, 0x00000000u, 0x00000000u },
        false,
        HookStability::Core
    },
// DEPRECATED / DO NOT USE:
//
// This was an early candidate "post-final-damage" hook. Later analysis
// showed it was mid-function and less clean than the current HP pipeline
// hooks (SEQ_Battle_UpdateHp + UNIT_HpDamage + UNIT_UpdateCloneHP).
//
// The entry remains only so HookId_BTL_FinalDamage_Post has a table row.
// It is marked Optional and should not be installed or used.
	{
		HookId_BTL_FinalDamage_Post, 
		"BTL_FinalDamage_Post", 
		0x0003B79Cu,
		0x0002B79Cu,
		{ 0x8590300Cu, 0x9A000012u, 0xE7935102u },
		false,
		HookStability::Optional
	},
    {
        HookId_BTL_GuardGauge_Add, // Wrong address, will revisit later.
        "BTL_GuardGauge_Add",
        0x00102DFEu,
        0x00002DFEu,
        { 0xB510430Bu, 0xD11C079Bu, 0xD31A2A04u },
        true,
        HookStability::Optional
    },
    
// ActionDualGuard__Tick @ 0x001D7AC4 – spends a guard pip, drives the cinematic.

// map__HpWindow__ShowDualGuardGauge @ 0x003A40EC – hpWindow->flag_0xAD = 1.

// game__graphics__DualGuardGauge__Draw @ 0x003E7898 – loops 10 × Icon__DualGuard__Draw.

// game__graphics__DualGuardGauge__DrawInfo @ 0x003E7920 – same loop but with richer color/range arguments.
	{
        HookId_BTL_GuardGauge_Spend, // Address completely wrong. Disabled. BTL_GuardGauge_Spend as "very likely ActionDualGuard__Tick"
        "BTL_GuardGauge_Spend",
        0x001490D4u,
        0x000490D4u,
        { 0xE672CF93u, 0xE666AFF2u, 0xE662BFF6u },
        false,
        HookStability::Optional
    },
    {
        // map__SequenceBattle__anonymous_namespace__ProcSequence__UpdateHp
        // Battle HP update+effects+UI; calls Unit__UpdateCloneHP and HpWindow moves
        HookId_SEQ_HpDamage,
        "SEQ_Battle_UpdateHp",          // 
        0x0035C7B8u,                    // code VA
        0x0025C7B8u,                    // ROM VA = code VA - 0x00100000
        { 0xE92D4070u, 0xE1A05000u, 0xE590025Cu },  // guard words
        false,                          // isThumb = false (ARM)
        HookStability::Core             // 
    },
	{
        // Generic unit HP damage wrapper:
        // anonymous_namespace__UnitHpDamage (ARM)
        HookId_UNIT_HpDamage,
        "UNIT_HpDamage",
        0x003A844Cu,      // 
        0x002A844Cu,      //
        { 0xE92D40F8u, 0xE2510000u, 0xE1A04001u },  
        false,            // isThumb = false (ARM)
        HookStability::Core
    },
	{
        // Unit__UpdateCloneHP
        // Copies flags and HP-ish word at +0x8C from a source unit to its clone
        HookId_UNIT_UpdateCloneHP,
        "UNIT_UpdateCloneHP",
        0x003D575Cu,           // code VA
        0x002D575Cu,           // 
        { 0xE59010ACu, 0xE3510000u, 0x0A000004u },  // 
        false,                 // isThumb = false (ARM)
        HookStability::Core    // 
    },
    {
        // map__SequenceBattle__anonymous_namespace__ProcSequence__DeadEvent
        // This runs after a unit has been confirmed dead and handles
        // record-death / record-kill flags and death productions.
        HookId_HP_KillCheck,
        "HP_KillCheck",
        0x0035CADCu,      // code VA (ProcSequence::DeadEvent)
        0x0025CADCu,      // ROM VA = 0x0035CADC - 0x00100000
        { 0xE92D4070u, 0xE1A05000u, 0xEB0724DDu },  // bl    0x00525e60 (map__BattleCalculator__GetDeadUnit-like)
        false,            // isThumb = false (ARM)
        HookStability::Core   // 
    },

    {
        // Generic HP heal as a map sequence:
        // map__SequenceHelper__HpHeal
        HookId_SEQ_HpDamage_Helper,
        "SEQ_HpDamage_Helper",
        0x00360F94u,      // code VA 
        0x00260F94u,      // 
        { 0xE92D41F0u, 0xE1A04000u, 0xE24DD010u },  
        false,            // isThumb = false (ARM)
        HookStability::Core
    },
	{
		// Generic "gain item" as a map sequence:
		// map__SequenceHelper__ItemGain (ARM)
		HookId_SEQ_ItemGain,
		"SEQ_ItemGain",
		0x00361124u,
		0x00261124u,
		{ 0xE92D43F8u, 0xE1A05001u, 0xE1A07000u },  
		false,
		HookStability::Core  
	},

    {
        HookId_MAP_ProcSkillDamage,
        "MAP_ProcSkillDamage",
        0x00386820u,
        0x00286820u,
        { 0xE92D4038u, 0xE1A05000u, 0xE3A0003Cu },
        false,
        HookStability::Core
    },
    {
        HookId_MAP_ProcTerrainDamage,
        "MAP_ProcTerrainDamage",
        0x00386948u,
        0x00286948u,
        { 0xE92D40F0u, 0xE24DD064u, 0xE1A07000u },
        false,
        HookStability::Core
    },
    {
        HookId_MAP_ProcTrickDamage,
        "MAP_ProcTrickDamage",
        0x00386D18u,
        0x00286D18u,
        { 0xE92D4070u, 0xE1A04000u, 0xE59F504Cu },
        false,
        HookStability::Core
    },
    {
        HookId_EVENT_ActionEnd, 
        "EVENT_ActionEnd", 
        0x0042262Cu,
        0x0032262Cu,
        { 0xE59F2018u, 0xE3A03000u, 0xE3A0101Eu },
        false,
        HookStability::Core
    },
    {
        HookId_BTL_AttackStance_Check,
        "BTL_AttackStance_Check",
        0x005281B8u,
        0x004281B8u,
        { 0xE92D4070u, 0xE1A04000u, 0xE5900004u },
        false,
        HookStability::Core
    },
    {
        HookId_BTL_AttackStance_ApplySupport,
        "BTL_AttackStance_ApplySupport",
        0x00347350u,
        0x00247350u,
        { 0xE92D47F0u, 0xE1A06000u, 0xE5900804u },
        false,
        HookStability::Core
    }, 
	
// NOTE: Known-bad address for HUD_Battle_HPGaugeUpdate – enabling this
// MITM causes UI glitches. Kept as a disabled candidate only.
// find a safer HUD hook later (Phase: QoL/UI), not during basic hook
// stabilization.
	{
        HookId_HUD_Battle_HPGaugeUpdate,
        "HUD_Battle_HPGaugeUpdate", // Seems to be unstable (Causing UI bugs), another hook location is needed.
        0x001D3148u,
        0x000D3148u,
        { 0xE92D4FFFu, 0xE1A04001u, 0xE1A07000u },
        false,
        HookStability::Optional
    },
    {
        HookId_BTL_SkillEffect_Apply, //Reduntent with current phasing
        "BTL_SkillEffect_Apply",
        0x0039F9E0u,
        0x0029F9E0u,
        { 0xE92D4FFFu, 0xE1A04001u, 0xE1A07000u },
        false,
        HookStability::Optional
    },
	//////////RNG
	{
		HookId_SYS_Rng32,
        "SYS_Rng32",
        0x0044ADF8u,      // code VA
        0x0034ADF8u,      // 
        { 0xE92D4010u, 0xE1A04001u, 0xEB000003u },  
        false,            // isThumb = false (ARM)
        HookStability::Core
    },
	{
		HookId_SEQ_TurnBegin,
        "SEQ_TurnBegin",
        0x003A54D8u,      // code VA
        0x002A54D8u,      // 
        { 0xE92D4070u, 0xE59F60DCu, 0xE5960008u },  
        false,            // isThumb = false (ARM)
        HookStability::Core
    },
	{
		HookId_SEQ_TurnEnd,
        "SEQ_TurnEnd",
        0x003A4F0Cu,      // code VA
        0x002A4F0Cu,      // 
        { 0xE92D41F0u, 0xE1A05000u, 0xE59F70D8u },  
        false,            // isThumb = false (ARM)
        HookStability::Core
    },
	{
		HookId_SEQ_MapEnd,
        "SEQ_MapEnd",
        0x003A4FFCu,      // code VA
        0x002A4FFCu,      // 
        { 0xE92D4FF8u, 0xE3A07000u, 0xE3A09003u },  
        false,            // isThumb = false (ARM)
        HookStability::Core
    },
	{
		HookId_SEQ_MapStart,
        "SEQ_MapStart",
        0x003A4898u,      // code VA
        0x002A4898u,      // 
        { 0xE59F0050u, 0xE92D4010u, 0xE5900000u },  
        false,            // isThumb = false (ARM)
        HookStability::Core
    },
	{
		HookId_SEQ_ItemUse,
        "Unit_ItemUse",
        0x0037D8F4u,      // code VA
        0x0027D8F4u,      // 
        { 0xE92D4010u, 0xE1A04000u, 0xE5900030u },  
        false,            // isThumb = false (ARM)
        HookStability::Core
    },
	{
		HookId_UNIT_LevelUp,
        "Unit_LevelUp",
        0x003D8154u,      // code VA
        0x002D8154u,      // 
        { 0xE92D4FF0u, 0xE24DD03Cu, 0xE1A07000u },  
        false,            // isThumb = false (ARM)
        HookStability::Core
    },
	{
		HookId_UNIT_SkillLearn,
        "Unit_AddEquipSkill",
        0x003D547Cu,      // code VA
        0x002D547Cu,      // 
        { 0xE3510000u, 0x0A000015u, 0xE1D02FBEu },  
        false,            // isThumb = false (ARM)
        HookStability::Core
    },
	{
		HookId_SEQ_UnitMove,
        "SEQ_UnitMove",
        0x00354524u,      // code VA
        0x00254524u,      //
        { 0xE92D4070u, 0xE1A05000u, 0xEB00D2B8u },  
        false,            // isThumb = false (ARM)
        HookStability::Core
    },
};

// Number of hooks defined above.
const std::size_t kNumHooks = sizeof(kHooks) / sizeof(kHooks[0]);

} // namespace Fates
