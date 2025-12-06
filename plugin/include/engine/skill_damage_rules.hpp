// engine/skill_damage_rules.hpp
//
// Registration entry point for skill-based damage rules. Called
// from Engine::InitCoreModules() so the damage pipeline knows
// about any skill-driven modifiers.

#pragma once

namespace Fates {
namespace Engine {
namespace Skills {

// Register all skill-based damage rules with the damage engine.
// Intended to be called once at startup; should be idempotent.
void RegisterDamageRules();

} // namespace Skills
} // namespace Engine
} // namespace Fates
