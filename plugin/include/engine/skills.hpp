// engine/skills.hpp
//
// Thin front-door for the skill engine.
//
// Right now this module is responsible for:
//   * Maintaining a lightweight per-map skill table based on
//     UNIT_SkillLearn (fed from hooks_handlers.cpp).
//   * Registering a debug-only HP-change observer for RE / testing.
//
// Over time, additional helpers for hit/damage modifiers will be
// layered on top of this.

#pragma once

#include <cstdint>

#include "engine/events.hpp"  // TurnSide, HpChangeContext, etc.

namespace Fates {
namespace Engine {
namespace Skills {

// Initialise the skill engine (idempotent).
// Currently this:
//
//   * Resets internal skill tables.
//   * Registers a MapBegin handler to clear state per map.
//   * Registers a debug HP-change observer.
//
// You don't need to call this yourself; it is invoked automatically
// at plugin startup by a small static bootstrap in skills.cpp.
void InitDebugSkills();

// ---------------------------------------------------------------------
// Hooks-layer bridge
// ---------------------------------------------------------------------
//
// Called from Hook_UNIT_SkillLearn whenever the game successfully
// adds a skill to a unit. This is a low-level bridge; most callers
// should prefer to work with higher-level data (e.g. HP/kill events)
// instead of calling this directly.
//
void OnUnitSkillLearnRaw(void          *unitRaw,
                         std::uint16_t  skillId,
                         std::uint16_t  flags,
                         std::uint32_t  result,
                         TurnSide       side);

// ---------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------
//
// Lightweight query used by other engine modules / hooks to check
// whether a particular unit currently has a given skill ID recorded
// for this map.
//
bool UnitHasSkill(void          *unitRaw,
                  std::uint16_t  skillId);

} // namespace Skills
} // namespace Engine
} // namespace Fates
