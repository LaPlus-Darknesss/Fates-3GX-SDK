// handlers.hpp
//
// Declares the C callback functions that will be invoked by the CTRPF
// hook mechanism for each hook defined in the hook catalog. Each
// function is implemented in plugin/src/hooks_handlers.cpp and is
// responsible for bumping a hit counter and (eventually) calling back
// into the original game code via CTRPluginFramework::HookContext.
//
// All signatures here are derived from docs/hooks/hook_catalog_*.txt.
// Arguments correspond to the register convention at the hook site
// (r0, r1, r2, r3). Pay close attention to args when adding new hooks.

#pragma once

#include <cstdint>

extern "C" {
//struct BattleResultRoot {
//  BattleResultSide* sideArray_00; // at +0
//    ... anything else about the root is either in other functions or unused
//};

//
// ---------------------------------------------------------------------
// Battle math hooks
// ---------------------------------------------------------------------

// BTL_HitCalc_Main / map__battle__detail__RandomCalculateHit
// Stage: HIT_RNG (RNG roll step, not stat-based hit formula)
// r0 = hitThreshold (pre-computed hit fraction/percent, 0..100-ish)
// returns: non-zero on hit, 0 on miss
int Hook_BTL_HitCalc_Main(int hitThreshold);

// r0 = unit_ptr, r1 = index_or_flag (exact meaning TBD)
int Hook_BTL_CritCalc_Main(void *unit,
                           int   indexOrFlag);


// BTL_FinalDamage_Pre
// Stage: FINAL_HP (late HP-apply / final damage observer)
//
// Observed behaviour (from RE + live tests):
//   * r0 points to a FinalDamageCalc-like object:
//        +0x00 : BattleRoot*  root
//        +0x04 : int          maxHp
//        +0x08 : int          hpAfter
//        +0x10 : int          hpBefore
//     hpBefore - hpAfter = damage for THIS pass.
//   * r1–r3 are additional opaque arguments used by the engine.
//   * This function is called multiple times per battle; early passes
//     can have smaller deltas, final pass matches the full damage.
//
// SDK usage (v2 hygiene):
//   * Treat this as an observation-only FINAL_HP hook.
//   * Allowed:
//       - Log final HP deltas and BattleRoot fields.
//       - Build DamageContext with origin = FinalHp for sanity checks.
//   * Must NOT:
//       - Be used as the primary damage math hook.
//       - Perform forecast-level writeback or try to “fix” the forecast.
//       - Recompute Attack/Hit/etc. (that belongs in PREVIEW_SIMPLE).
int Hook_BTL_FinalDamage_Pre(void *calc,
                             void *arg1,
                             void *arg2,
                             void *arg3);

// Deprecated / RE-only post-damage hook
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

// SEQ_Battle_UpdateHp
// r0 = seq, r1 = mode
void Hook_SEQ_HpDamage(void *seq,
                       int   mode);

// anonymous_namespace__UnitHpDamage
//
// Thin wrapper around map__SequenceHelper__HpDamage used by the battle
// engine to gate map-side HP commit. Ghidra shows it:
//
//   * Checks a small-range int argument (0 < x < 0xFB).
//   * Verifies some UnitPool / Force state.
//   * If checks pass, calls map__SequenceHelper__HpDamage(0).
//
// The exact register-level signature is still under investigation; for
// now we treat all four arguments as opaque r0..r3 and keep this hook
// observation-only (no behaviour changes).
int Hook_UNIT_HpDamage(void *a0,
                       void *a1,
                       void *a2,
                       void *a3);

// Unit__UpdateCloneHP
//
// Stage: HP_SYNC / CLONE_SYNC.
//   * r0 = Unit* (main / source unit).
//   * If unit->clone (at +0xAC) is non-null, copies:
//       clone->hpByte    @ +0xF3 = unit->hpByte    @ +0xF3
//       clone->hpField32 @ +0x8C = unit->hpField32 @ +0x8C
//
// Our hook treats this as the canonical "HP change finalized for this
// unit" point and only observes + forwards to Engine::OnUnitHpSync.
// Do NOT modify HP or clone state inside this hook.
void Hook_UNIT_UpdateCloneHP(void *unit);

// map__SequenceBattle__anonymous_namespace__ProcSequence__DeadEvent
//
// Stage: KILL_EVENT (post HP commit).
//   * This is the battle-sequence proc that fires after HP has already
//     been applied, once a loser exists and event::Die has been triggered.
//   * At this point, the sequence object stores:
//       +0x280 : killFlags bitfield (bit 0 set when there is a real kill)
//       +0x284 : dead slot 0 (Unit* or nullptr)
//       +0x288 : dead slot 1 (Unit* or nullptr)
//
// Calling convention (observed):
//   * r0 = seqBattle / ProcSequence* for the battle.
//   * r1 = unused noise from the call site; our stub passes it through
//         to the original but does not inspect it.
void Hook_HP_KillCheck(void *seqBattle,
                       void *contextOrFlags);

// map__SequenceHelper__HpDamage – map-side HP commit helper for damage.
//
// Stage: FINAL_HP_DRIVER (map damage).
//
// Observed calling convention (from decomp + callers):
//   r0 = SequenceHelper* / proc root (opaque context; passed to ProcInst__Create)
//   r1 = Unit*          (damage target; HP byte at +0xF3)
//   r2 = damageAmount   (int, subtracted from HP)
//   r3 = modeOrFlags    (int, typically 0/1)
//
// Behaviour:
//   * Skip / blackout fast-path:
//       - Reads current HP from unit->hp (+0xF3).
//       - Subtracts damageAmount, clamps result to >= 1.
//       - Stores back to unit->hp (+0xF3).
//       - Calls Unit__UpdateCloneHP() and returns immediately.
//   * Normal path:
//       - Allocates a ProcHpDamage instance and constructs it.
//       - Calls ProcInst__Create(r0) so the proc system applies HP and
//         calls Unit__UpdateCloneHP() later.
//
// SDK guidelines (v2 hygiene):
//   * Treat this as a FINAL_HP_DRIVER surface for non-battle map damage
//     (trap tiles, terrain gimmicks, scripted HP loss).
//   * Do NOT use this as the primary damage math hook; BattleInfo / BTL
//     hooks own the core formula + forecast.
//   * Do NOT reintroduce direct damage math or HP writeback here; the
//     canonical HP-change event is Engine::OnUnitHpSync via
//     UNIT_UpdateCloneHP.
int Hook_SEQ_HpDamage_Helper(void *a0,
                             void *a1,
                             void *a2,
                             void *a3);

// map__SequenceHelper__ItemGain (ARM)
// r0 = seqHelper, r1 = unit, r2 = itemArg, r3 = mode_or_ctx
int Hook_SEQ_ItemGain(void *a0,
                      void *a1,
                      void *a2,
                      void *a3);

// Map special sequence hooks (terrain heal / trick statue heal / skill cannon)
//
// These are high-level Proc entrypoints that eventually drive HP changes
// via map__SequenceHelper__HpDamage and Unit__UpdateCloneHP, but do not
// directly touch unit HP themselves.
//
//   * Hook_MAP_ProcSkillDamage
//       map__anonymous_namespace__ProcSequence__TerrainHeal
//       r0 = seq / proc root (terrain-based healing driver)
//
//   * Hook_MAP_ProcTerrainDamage
//       map__anonymous_namespace__ProcSequence__TrickStatueHeal
//       r0 = seq / proc root (trick statue AoE healing driver)
//
//   * Hook_MAP_ProcTrickDamage
//       map__anonymous_namespace__ProcSkillCannon__Effect
//       r0 = seq / proc root (Skill Cannon bind/effect driver)
//
// Stage classification:
//   * MAP_PROC_HEAL/TRAP front-ends.
//   * SDK usage: observation-only. Any real HP rules must be implemented
//     at the FINAL_HP_DRIVER and HP_SYNC layers
//     (SequenceHelper::HpDamage + Unit__UpdateCloneHP).
void Hook_MAP_ProcSkillDamage(void *seq);

void Hook_MAP_ProcTerrainDamage(void *seq);

void Hook_MAP_ProcTrickDamage(void *seq);

// ---------------------------------------------------------------------
// Forecast / damage core (PREVIEW_SIMPLE, EFFICACY, FINAL_HP_DRIVER)
// ---------------------------------------------------------------------

// map__BattleInfo__CalculateSimple
//
// Stage: PREVIEW_SIMPLE.
//   r0 = BattleInfo*.
//
// Role (vanilla):
//   * Iterates the four 0x200-byte BattleInfo::Side blocks and fills in the
//     "simple" forecast primitives for each side via the helpers:
//       - map__BattleInfo__Side__GetSimplePower  -> simplePower_2C
//       - map__BattleInfo__Side__GetSimpleHit    -> simpleHitRaw_30 and gate
//       - map__BattleInfo__Side__GetSimpleTimes  -> simpleTimes_3C
//   * Recomputes a gated 0..100 hit value into simpleHit_0to100_34 using
//     side hit stats plus opponent stats from the paired BattleAttackEntry.
//   * Computes simpleAuxRate_38 (typically 300 or 400, sometimes halved).
//   * Uses gBattleInfoGlobals (item/skill bitfields, cached equip-skill IDs)
//     which are initialised lazily via ItemSkill__Get / EquipSkill__Get.
//
// SDK usage:
//   * Canonical simple forecast aggregation point for the FORECAST origin.
//   * Our hook should call vanilla once, then (in future) let the engine
//     inspect/adjust per-side simple power / hit / times values.
//   * Do NOT change the side stride (0x200), side index mapping, or the
//     static gBattleInfoGlobals initialisation behaviour.
void Hook_MAP_BattleInfo_CalculateSimple(void *battleInfo);

// map__BattleInfo__Side__CalculateEfficacy
//
// Stage: PREVIEW_DETAIL / EFFICACY.
//   r0 = side / BattleAttackEntry* ("this"), r1..r3 = peer context.
//
// Role (vanilla):
//   * Initialises a static efficacy config (iRam00348cfc) containing:
//       - Several equip-skill IDs.
//       - ItemSkill-derived bitfields.
//       - JobCategory-based bitmasks.
//   * For a single side, sets bits in the flags at +0x28 (for example
//     0x200 and 0x400) when the weapon is effective against the target's
//     category, or when particular item/skill masks and item subkinds
//     match (physical/magic/beast/dragon style checks).
//
// SDK usage:
//   * Observation point for vanilla "effective against X" behaviour.
//   * In v2 hygiene this stays read-mostly: we may log and later augment
//     flags, but should not replace the base logic or repurpose the
//     flag layout at +0x28.
void Hook_MAP_BattleInfoSide_CalcEfficacy(void *side,
                                          void *a1,
                                          void *a2,
                                          void *a3);
										  
// map__BattleCalculator__CalculateAttack
//
// Stage: DAMAGE_MATH / FINAL_HP_DRIVER.
//   r0 = BattleResultRoot_t* (result root / "calculator"),
//   r1 = slotIndexOrKind, r2/r3 = unused noise at the call site.
//
// Role (vanilla):
//   * Initialises a static config block (iRam00362278) of equip-skill IDs
//     and item-skill bitfields using __cxa_guard_acquire.
//   * Derives the candidate result-side from the root and slotIndexOrKind
//     (base + slot * 0x200).
//   * Applies item/flag gates (unit__Item__ToData, flags & 0x100/0x4000).
//   * Uses CAND_MapBattleCalc_* helpers plus the config shorts at +4..+0x14
//     to choose a sideIndexB "rule" and optionally record an extra entry.
//   * Calls BTL_FinalDamage_Pre(resultRoot, slotIndexOrKind, sideIndexB),
//     sometimes in a loop (up to 5 passes) or as a chained two-pass case.
//
// SDK usage:
//   * High-risk, late-stage hook. In v2 hygiene we treat this as an
//     observation/validation surface for FINAL_HP-origin rules:
//       - Allowed: logging and building a DamageContext from the
//         BattleResultRoot + indices to sanity-check against forecast.
//       - Not allowed: changing the root pointer, the slot index, or
//         re-wiring candidate selection unless the full CAND_* behaviour
//         is understood and mirrored in the engine.
int Hook_MAP_BattleCalculator_CalculateAttack(void *calc,
                                              void *a1,
                                              void *a2,
                                              void *a3);



// ---------------------------------------------------------------------
// Event / action hooks
// ---------------------------------------------------------------------

// EVENT_ActionEnd
// r0 = eventInstance
int Hook_EVENT_ActionEnd(void *eventInstance);

// ---------------------------------------------------------------------
// Battle support / Attack Stance
// ---------------------------------------------------------------------

// map__Situation__CanDual(Situation* self, int indexOrOffset)
//
// Stage: ATTACK_STANCE_CHECK
//
// Calling convention (observed from RE):
//   * r0 = Situation* self.
//   * r1 = indexOrOffset – byte offset into the Situation struct used
//           as a small per-side / per-slot state byte.
//
// Vanilla behaviour (NA v1.1):
//   * Reads two gates:
//       - self+0x4, bit 0x80000.
//       - GameUserData(+0x28), bit 0x8.
//   * If either gate disables the per-slot logic, it returns 1
//     (Attack Stance effectively “forced on”).
//   * Otherwise it reads a single byte at (self + indexOrOffset) and
//     returns 1 iff that byte is exactly 1, else 0.
//
// SDK rules:
//   * Treat this as a pure eligibility gate; the SDK may observe and
//     (carefully) override the boolean result, but must not mutate the
//     Situation contents from this hook.
int Hook_BTL_AttackStance_Check(void *situation,
                                int   indexOrOffset);

// map__BattleInfo__CalculateDual – dual / guard forecast finalizer.
//
// Stage: PREVIEW_DETAIL / ATTACK_STANCE_APPLY_SUPPORT
//
// Calling convention:
//   * r0 = BattleInfo* root (BattleRoot/BattleInfo aggregate).
//
// Root layout (observed):
//   * root+0x804 : flags; bit 0x10 disables dual calc.
//   * root+0x28  : flags; bit 0x40 also gates dual/guard calc.
//
// Per-side layout (sideIndex in {0,1}, side = root + 0x200*sideIndex):
//   * side+0x90 : Unit* supportUnit (dual partner), or nullptr.
//   * side+0x94 : u8 dualEnabled flag (cleared, then possibly set).
//   * side+0x98 : u32 dualSynergy accumulator derived from the unit
//                 byte at +0x135 for lead + support.
//   * side+0x28 : sideFlags; bits 0x100 and 0x4000 are used as
//                 dual-disable gates.
//   * side+0x3C : times / strike count; if <1, side+0x43C is zeroed.
//   * side+0x43C: final dual/guard output slot; zeroed whenever dual
//                 should not apply.
//
// Vanilla behaviour (high-level):
//   * Early-out if root flags say dual/guard is disabled.
//   * For each side, clears dualEnabled/dualSynergy.
//   * If there is a supportUnit:
//       - If supportUnit == lead->pairPartner @ lead+0xA8, fills
//         dualSynergy as (lead->0x135 + support->0x135).
//       - Checks the lead for a specific equip skill (ID cached via
//         EquipSkill::Get); if present and (sideFlags & 0x100) == 0,
//         sets dualEnabled = 1.
//   * Applies final gating based on times, dualEnabled and sideFlags,
//     zeroing side+0x43C when dual should not apply.
//
// SDK rules:
//   * Treat this as a PREVIEW_DETAIL hook for logging or very light
//     augmentation of dual state; never touch HP or base Hit/Avo, and
//     never use this as a damage math hook.
void Hook_BTL_AttackStance_ApplySupport(void *battleInfo);

// ---------------------------------------------------------------------
// HUD and skill hooks
// ---------------------------------------------------------------------

// HUD_Battle_HPGaugeUpdate (currently Optional / RE-only)
// r0 = hud_context_ptr, r1 = unit_ptr
void Hook_HUD_Battle_HPGaugeUpdate(void *hudContext,
                                   void *unit);

// NEW: game__graphics__HpWindow__Draw
// r0 = hpWindow_ptr, r1..r3 = extra args
void Hook_HUD_HpWindow_Draw(void *hpWindow,
                            void *a1,
                            void *a2,
                            void *a3);

// r0 = battle_context_ptr, r1 = attacker_unit_ptr,
// r2 = defender_unit_ptr, r3 = skill_id_or_flags
int Hook_BTL_SkillEffect_Apply(void *battleContext,
                               void *attacker,
                               void *defender,
                               std::uint32_t skillIdOrFlags);
							   
// NEW: map__EquipSkillCalculator__Calculate
// Entry point for equip-skill aggregation on the map side.
// r0 = calc (EquipSkillCalculator* candidate), r1..r3 = extra args / context
int Hook_MAP_EquipSkillCalculator_Calculate(void *calc,
                                            void *a1,
                                            void *a2,
                                            void *a3);

// Global RNG wrapper
std::uint32_t Hook_SYS_Rng32(void *rngState,
                             std::uint32_t upperBound);


// ---------------------------------------------------------------------
// Map sequence / turn lifecycle hooks (v2 hygiene)
//
// Backed by the following vanilla functions (NA v1.1):
//
//   SEQ_MapStart  -> map__Sequence__anonymous_namespace__ProcSequence__Persistent @ 0x003A4898
//     * Stage: MAP_PERSISTENT_TICK.
//     * Role: Versus / link-match "persistent" tick inside the main
//       map sequence. It does not perform core map-start setup itself.
//       In the SDK we only treat the first time we see a *new* proc
//       pointer here as "map begin" for telemetry and Engine::OnMapBegin.
//       The function name SEQ_MapStart is therefore legacy.
//
//   SEQ_TurnBegin -> map__Sequence__anonymous_namespace__ProcSequence__TurnBegin @ 0x003A54D8
//     * Stage: TURN_BEGIN.
//     * Role: per-turn setup. Resets deploy state, walks the current
//       Force, runs unit__Enhance__TurnBegin and Unit__UpdateClone()
//       for eligible units, and updates various per-turn counters.
//
//   SEQ_TurnEnd   -> map__Sequence__anonymous_namespace__ProcSequence__TurnEnd @ 0x003A4F0C
//     * Stage: TURN_END.
//     * Role: per-turn teardown. Clears a block of unit flags, calls
//       unit__Enhance__TurnEnd() + Unit__UpdateClone() for non-ghost
//       units, refreshes the danger map, and asks map__Situation
//       whether the map should jump to GameOver/Complete.
//
//   SEQ_MapEnd    -> map__Sequence__anonymous_namespace__ProcSequence__Complete @ 0x003A4FFC
//     * Stage: MAP_END / POST_MAP_CLEANUP.
//     * Role: end-of-map processing. Chooses the "top support pair",
//       writes intermediate data, resets per-unit end-of-map state,
//       revives Casual/Phoenix deaths, clears panel/cursor flags,
//       refreshes danger/terrain images, and updates global viewer
//       data when not in Versus.
//
// SDK rules for these hooks:
//   * These are strictly map/turn lifecycle surfaces. They MUST NOT
//     perform HP or damage math, and MUST NOT touch BattleInfo or
//     BattleResult structures. HP is owned by the
//     SequenceHelper::HpDamage + Unit__UpdateCloneHP path.
//   * We always call the vanilla function (often first) and treat the
//     hook as a place to:
//         - Update gMapState / gMapStats.
//         - Maintain gCurrentTurnSide.
//         - Fire Engine-level events (OnMapBegin/End, OnTurnBegin/End).
//         - Do light logging.
//   * Any future map-logic features should be bolted onto the Engine
//     events, not spliced directly into these vanilla functions.
// ---------------------------------------------------------------------

// Hook_SEQ_TurnBegin
//   -> ProcSequence::TurnBegin (TURN_BEGIN)
//
void Hook_SEQ_TurnBegin();

// Hook_SEQ_TurnEnd
//   -> ProcSequence::TurnEnd (TURN_END)
int Hook_SEQ_TurnEnd(void *seq);

// Hook_SEQ_MapEnd
//   -> ProcSequence::Complete (MAP_END / POST_MAP_CLEANUP)
int Hook_SEQ_MapEnd(void *seq);

// Hook_SEQ_MapStart
//   -> ProcSequence::Persistent (MAP_PERSISTENT_TICK)
//   -> Used only to detect "new map" by watching for a new proc pointer.
void Hook_SEQ_MapStart(void *seq);

// SEQ_ItemUse – Unit__ItemUse(unit, something, only for misc items)
// r0 = seq
void Hook_SEQ_ItemUse(void *seq);

// UNIT_LevelUp – Unit__LevelUp(unit)
// r0 = unit
void Hook_UNIT_LevelUp(void *unit);

// UNIT_SkillLearn – Unit__AddEquipSkill(unit, skillId)
// r0 = unit, r1 = skillId
int Hook_UNIT_SkillLearn(void *unit,
                         std::uint32_t skillId);

// SEQ_UnitMove – ProcSequence__UnitMove(seq)
// r0 = seq
void Hook_SEQ_UnitMove(void *seqInst);

// UNIT_HasSkillById – Unit__HasSkillById(unit, skillId)
// r0 = unit, r1 = int16 skillId
int Hook_UNIT_HasSkillById(void *unit,
                           std::int16_t skillId);
						   

} // extern "C"
