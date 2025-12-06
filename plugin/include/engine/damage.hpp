// engine/damage.hpp
//
// Core damage-rule pipeline that sits on top of Engine::Combat.
//
// This module does *not* hook the game directly. Instead it provides:
//
//   * A registration API for "rules" (RuleFn) that can adjust damage
//     based on Combat::DamageContext.
//   * A small internal table to store those rules.
//   * Init() wiring that registers a single modifier with the low-level
//     Combat pipeline.
//
// The actual hook into BTL_FinalDamage_Pre lives in hooks_handlers.cpp
// and calls Combat::ApplyDamageModifiersEx. This module aggregates all
// higher-level rules (skills, terrain, auras, etc.) behind one
// DamageModifierFn so the Combat layer stays simple.

#pragma once

#include <cstdint>

#include "engine/combat.hpp"  // Combat::DamageContext, RegisterDamageModifier

namespace Fates {
namespace Engine {
namespace Damage {

// Function signature for a damage rule.
//
// Given a DamageContext and the current candidate damage value,
// return the (possibly) modified damage. Implementations are free
// to be a no-op and just return currentDamage unchanged.
using RuleFn = int (*)(const Combat::DamageContext& ctx,
                       int                          currentDamage);

// Register a new damage rule in the engine.
//
// Rules are applied in registration order. Returns true on success,
// false if the internal table is full or fn == nullptr.
bool RegisterRule(RuleFn fn, const char* debugName);

// Explicit initialiser (idempotent). Normally you don't need to call
// this yourself; damage.cpp arranges for automatic registration at
// plugin load via a small static bootstrap, but it's exposed for
// clarity and potential future tests.
void Init();

} // namespace Damage
} // namespace Engine
} // namespace Fates
