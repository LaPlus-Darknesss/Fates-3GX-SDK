// engine/skill_damage_rules.cpp

#include "engine/damage.hpp"
#include "engine/skills.hpp"
#include "util/debug_log.hpp"
#include "engine/skill_rule_jakob_unique.hpp" // Jakob's personal (real rule, v2 reference)

namespace Fates {
namespace Engine {
namespace Skills {

namespace {
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

    // Ensure the damage engine's modifier is installed into the combat pipeline
    // before we start adding skill-driven rules.
    Damage::Init();

    // Register Jakob's personal damage rule as a clean, non-debug reference implementation.
    bool okJakob = RegisterJakobUniqueDamageRule();

    Logf("SkillDamageRules::RegisterDamageRules: damage pipeline initialised; "
         "jakob_rule=%s",
         okJakob ? "OK" : "FAILED");
}

} // namespace Skills
} // namespace Engine
} // namespace Fates
