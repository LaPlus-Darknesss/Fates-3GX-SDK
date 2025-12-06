// engine/skill_damage_rules.cpp
//
// Bridge between the skill engine and the damage engine.
//
// This file is the central registration point for any skill-based
// damage rules. It is called from Engine::InitCoreModules() or from
// the SEQ_MapStart hook so that Engine::Damage knows about all
// skill-driven modifiers.
//
// Current state (v2 cleanup):
//   * We only ensure that Engine::Damage is initialised.
//   * No skill-driven rules are registered yet; the pipeline acts as
//     a pure passthrough until real rules are added in later phases.

#include "engine/damage.hpp"
#include "engine/skills.hpp"
#include "util/debug_log.hpp"

namespace Fates {
namespace Engine {
namespace Skills {

namespace {
    // Guard so we never register the same rules twice.
    bool sDamageRulesRegistered = false;
}

void RegisterDamageRules()
{
    if (sDamageRulesRegistered)
    {
        Logf("SkillDamageRules::RegisterDamageRules: already registered, skipping");
        return;
    }
    sDamageRulesRegistered = true;

    // Make sure the damage engine's modifier is installed into
    // Engine::Combat before we start adding rules (now or in future).
    Damage::Init();

    // v2 cleanup: no debug / experimental rules are registered here.
    // Future phases will add real skill-driven rules on top of this.
    Logf("SkillDamageRules::RegisterDamageRules: damage pipeline initialised "
         "with no skill-based rules (passthrough)");
}

} // namespace Skills
} // namespace Engine
} // namespace Fates
