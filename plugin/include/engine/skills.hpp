// engine/skills.hpp
//
// Thin front-door for the skill engine.
//
// This module is responsible for:
//   * Maintaining a lightweight per-map skill table keyed by UnitState
//     indices (UnitHandle -> UnitStateIndex -> skill set).
//   * Resetting that table on each new map via a MapBegin handler.
//   * Providing helpers to query per-unit skills and decode the
//     "applied skill id" from a Combat::DamageContext's battle root.
//
// NOTE: The canonical truth for "does this unit have skill X?" is the
// game's own Unit__HasSkillById implementation. The per-map skill
// table is a derived snapshot used for debug and plugin-only skills,
// not the ultimate source of truth.
//
// There is intentionally no damage / HP modification logic here.
// Actual damage rules live in Engine::Damage and friends.

#pragma once

#include <cstdint>

#include "engine/events.hpp"  // TurnSide, MapContext, UnitHandle, etc.

namespace Fates {
namespace Engine {

// Forward declaration so we don't need to include combat.hpp here.
namespace Combat {
struct DamageContext;
}

namespace Skills {

// Initialise the skill engine (idempotent).
//
//   * Resets internal per-map skill tables.
//   * Registers a MapBegin handler to clear state each map.
//
// You don't need to call this yourself; it is invoked automatically
// at plugin startup by a small static bootstrap in skills.cpp.
void InitDebugSkills();

// Backwards-compatible alias: if any legacy code calls Skills::Init(),
// route it to InitDebugSkills().
inline void Init()
{
    InitDebugSkills();
}

// Hooks-layer bridge
// ---------------------------------------------------------------------
//
// Thin bridge for the vanilla Unit__HasSkillById function.
//
// The hook layer (hook_handlers.cpp) is responsible for calling
// SetVanillaUnitHasSkillThunk() once at startup with a pointer to the
// original implementation. When this thunk is unset, UnitHasSkill()
// falls back to the per-map skill table only.
using UnitHasSkillByIdThunk = int (*)(void *unitRaw, std::int16_t skillId);

// Install/replace the vanilla Unit__HasSkillById thunk used by
// UnitHasSkill(). Safe to call multiple times.
void SetVanillaUnitHasSkillThunk(UnitHasSkillByIdThunk fn);

// Called from Hook_UNIT_SkillLearn whenever the game successfully
// adds a skill to a unit.
void OnUnitSkillLearnRaw(void          *unitRaw,
                         std::uint16_t  skillId,
                         std::uint16_t  flags,
                         std::uint32_t  result,
                         TurnSide       side);

// Called whenever we have evidence that a unit currently has a given
// skill ID.
//
// Today this is fed from:
//
//   * Hook_UNIT_HasSkillById (vanilla function reports "has skill").
//   * Hook_BTL_SkillEffect_Apply, which receives a skill id/flags
//     dword and interprets the low 16 bits as the applied skill.
//   * Battle-root driver flags that look like 0x4000XXXX / 0x4001XXXX,
//     observed from hooks such as:
//         - Hook_BTL_FinalDamage_Pre
//         - Hook_MAP_BattleInfo_CalculateSimple
//         - Hook_MAP_EquipSkillCalculator_Calculate
//
// All of these are treated as "unit U has skill S at some point this
// map", and fed into the per-map UnitState-backed skill table.
void OnUnitHasSkillObserved(void          *unitRaw,
                            std::uint16_t  skillId);

// ---------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------
//
// Lightweight queries used by other engine modules / hooks to check
// whether a particular unit currently has a given skill ID.
//
// Contract:
//
//   * If the vanilla Unit__HasSkillById thunk has been installed via
//     SetVanillaUnitHasSkillThunk(), its answer is treated as the
//     canonical truth for base-game skills.
//   * The per-map skill table is a derived snapshot used for debug,
//     telemetry, and future plugin-only skills. It does not override
//     vanilla when there is a disagreement.
//
// Overloads exist for raw Unit* and UnitHandle.
//
bool UnitHasSkill(void          *unitRaw,
                  std::uint16_t  skillId);

// Convenience overload that takes a UnitHandle instead of a raw
// Unit* pointer.
bool UnitHasSkill(UnitHandle     unit,
                  std::uint16_t  skillId);

// Decode the "applied skill id" for a given damage context.
//
// For support/damage skills that drive a particular hit, the dword at
// root+0x10 looks like:
//
//   0x400000A3, 0x4001005B, 0x4001005C, ...
//
// The low 16 bits match the skill id driving this effect. If there is
// no root pointer, or no skill driver, returns 0.
std::uint16_t GetAppliedSkillId(const Combat::DamageContext& ctx);

// ---------------------------------------------------------------------
// Debug helpers
// ---------------------------------------------------------------------
//
// Dump the entire per-map skill table to the log. Safe to call from
// an OSD/debug hotkey; the output format is small and stable.
//
void DebugDumpAllSkillSets();

} // namespace Skills
} // namespace Engine
} // namespace Fates
