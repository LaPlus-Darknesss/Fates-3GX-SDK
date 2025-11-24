// handlers.hpp
//
// Declares the C callback functions that will be invoked by the CTRPF
// hook mechanism for each hook defined in hook_catalog_v6.  Each
// function is implemented in plugin/src/hooks_handlers.cpp and is
// responsible for bumping a hit counter and (eventually) calling back
// into the original game code via CTRPluginFramework::HookContext.
//
// All signatures here are derived from docs/hooks/hook_catalog_v6.txt.
// Arguments correspond to the register convention at the hook site
// (r0, r1, r2, r3).
// Almost all of these should have updated examples, but
// pay attention to args just to be sure.

#pragma once

#include <cstdint>

extern "C" {

// ---------------------------------------------------------------------
// Battle math hooks
// ---------------------------------------------------------------------

// r0 = battle_context_ptr, r1 = attacker_unit_ptr, r2 = defender_unit_ptr
int Hook_BTL_HitCalc_Main(int hitRate);

// r0 = unit_ptr, r1 = index_or_flag (exact meaning TBD)
int Hook_BTL_CritCalc_Main(void *unit,
                           int indexOrFlag);


// r0 = battle_context_ptr, r1 = attacker_unit_ptr, r2 = defender_unit_ptr
void Hook_BTL_FinalDamage_Pre(void *a0,
                              void *a1,
                              void *a2,
							  void *a3);

// r0 = battle_context_ptr, r1 = attacker_unit_ptr, r2 = defender_unit_ptr
void Hook_BTL_FinalDamage_Post(void *battleContext,
                               void *attacker,
                               void *defender);

// Thumb; r0 = battle_context_ptr, r1 = attacker_unit_ptr, r2 = defender_unit_ptr
void Hook_BTL_GuardGauge_Add(void *battleContext,
                             void *attacker,
                             void *defender);

// r0 = battle_context_ptr, r1 = attacker_unit_ptr, r2 = defender_unit_ptr
void Hook_BTL_GuardGauge_Spend(void *battleContext,
                               void *attacker,
                               void *defender);

// ---------------------------------------------------------------------
// HP and map damage hooks
// ---------------------------------------------------------------------

// r0 = unit_ptr, r1 = signed delta (HP change), r2 = context_or_flags
void Hook_SEQ_HpDamage(void *seq, 
						int mode);
					  
// r0 = unused_or_context, r1 = unitIndex, r2 = dmgAmount, r3 = maybe flags
int Hook_UNIT_HpDamage(void *a0,
                       void *a1,
                       void *a2,
                       void *a3);
					   
// r0 = source unit; clones and HP at +0x8C are synced inside.
void Hook_UNIT_UpdateCloneHP(void *unit);



// Thumb; r0 = unit_ptr, r1 = context_or_flags (HP_Apply_Generic caller)
void Hook_HP_KillCheck(void *calc,
                       void *contextOrFlags);

// SEQ_HpHeal – helper around map__SequenceHelper__HpDamage for healing.
// Observed call pattern:
//   r0 = SequenceHelper* / context
//   r1 = Unit* (heal target)
//   r2 = heal_amount (positive int)
//   r3 = flags / mode
int Hook_SEQ_HpDamage_Helper(void *a0,
                    void *a1,
                    void *a2,
                    void *a3);
					
// r0 = unit_ptr, r1 = item_id_or_slot, r2 = context_or_flags
int Hook_SEQ_ItemGain(void *a0,
                      void *a1,
                      void *a2,
                      void *a3);

// Map special sequence hooks (terrain heal / trick statue heal / skill cannon)
//
// These procedures operate on a sequence/context object, not a raw Unit* and
// not a direct "damage amount". Treat r0 as a meaningful pointer.
void Hook_MAP_ProcSkillDamage(void *seq);

void Hook_MAP_ProcTerrainDamage(void *seq);

void Hook_MAP_ProcTrickDamage(void *seq);


// ---------------------------------------------------------------------
// Event / action hooks
// ---------------------------------------------------------------------

// r0 = unit_ptr, r1 = action_result / flags
int Hook_EVENT_ActionEnd(void *eventInstance);

// ---------------------------------------------------------------------
// Battle support / Attack Stance
// ---------------------------------------------------------------------

// Experimental; r0 = battle_context_ptr, r1 = attacker_unit_ptr, r2 = defender_unit_ptr
int Hook_BTL_AttackStance_Check(void *situation,
                                int   index);

// r0 = battle_context_ptr, r1 = main_attacker_unit_ptr, r2 = support_unit_ptr
void Hook_BTL_AttackStance_ApplySupport(void *battleInfo);

// ---------------------------------------------------------------------
// HUD and skill hooks
// ---------------------------------------------------------------------

// r0 = hud_context_ptr, r1 = unit_ptr
void Hook_HUD_Battle_HPGaugeUpdate(void *hudContext,
                                   void *unit);

// r0 = battle_context_ptr, r1 = attacker_unit_ptr,
// r2 = defender_unit_ptr, r3 = skill_id_or_flags
int Hook_BTL_SkillEffect_Apply(void *battleContext,
                                void *attacker,
                                void *defender,
                                std::uint32_t skillIdOrFlags);
								
std::uint32_t Hook_SYS_Rng32(void *rngState,
                             std::uint32_t upperBound);
							 
void Hook_SEQ_TurnBegin();

int Hook_SEQ_TurnEnd(void *seq);

int Hook_SEQ_MapEnd(void *seq);

void Hook_SEQ_MapStart(void *seq);

// SEQ_ItemUse – Unit__ItemUse(unit, something, only procs on misc items, not healing consumables)
// This is not a reliable "item" hook, but is included for niche use cases.
void Hook_SEQ_ItemUse(void *seq);
// UNIT_LevelUp – Unit__LevelUp(unit)
void Hook_UNIT_LevelUp(void *unit);

// UNIT_SkillLearn – Unit__AddEquipSkill(unit, skillId)
int Hook_UNIT_SkillLearn(void *unit, std::uint32_t skillId);

void Hook_SEQ_UnitMove(void *seqInst);


} // extern "C"
