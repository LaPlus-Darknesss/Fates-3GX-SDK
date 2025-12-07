// engine/skill_rule_jakob_unique.hpp
//
// Example skill-based damage rule for Jakob's personal skill.
//
// This is kept in its own TU so engine/skills.cpp stays lean and
// other skills can follow the same pattern.

#pragma once

namespace Fates {
namespace Engine {
namespace Skills {

// Register the Jakob personal damage rule with the damage engine.
// Safe to call multiple times; returns true on first success.
bool RegisterJakobUniqueDamageRule();

} // namespace Skills
} // namespace Engine
} // namespace Fates
