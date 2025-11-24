// engine/skills.hpp
//
// Thin front-door for the skill engine. For now this only exposes
// a debug initialiser that registers global HP-change hooks with
// the engine event bus. You normally don't need to call this
// manually; skills.cpp arranges for automatic registration at
// plugin load via a small static bootstrap.

#pragma once

#include "engine/events.hpp"  // HpChangeContext, etc.

namespace Fates {
namespace Engine {
namespace Skills {

// Initialise the debug skill set (idempotent).
// Currently this just wires a single global "extra damage on HP"
// effect for testing purposes. (disabled, left for reference)
//
// You don't need to call this yourself; it is invoked automatically
// at plugin startup. It's exposed mainly for clarity and potential
// future tests.
void InitDebugSkills();

} // namespace Skills
} // namespace Engine
} // namespace Fates
