// engine/skill_rule_jakob_unique.cpp
//
// Reference implementation of a skill-based damage rule for
// Jakob's personal skill.
//
// Behaviour (for now):
//   * If the attacker has Jakob's personal skill, add +1 damage
//     per hit to the TOTAL damage.
//
// This runs inside the Engine::Damage pipeline and is intended to
// be agnostic to origin (Forecast vs FinalHp). Once both stages
// are fully wired into Combat::ApplyDamageModifiersEx, this rule
// will apply in both places.

#include "engine/skill_rule_jakob_unique.hpp"
#include "engine/damage.hpp"
#include "engine/skills.hpp"
#include "engine/unit_state.hpp"   // UnitHandle::Raw / IsValid
#include "util/debug_log.hpp"

#include <cstdint>

namespace Fates {
namespace Engine {
namespace Skills {

namespace {

// From earlier BattleRoot flag observations (0x400000A3, ...),
// the low 16 bits correspond to Jakob's personal skill id.
constexpr std::uint16_t kSkillId_JakobPersonal = 0x00A3;

// JakobUniqueDamageRule
//
//   * currentDamage is TOTAL damage across all hits.
//   * To do "+X per hit", we compute:
//       delta = X * numHits
//
//   * ctx.attacker is a UnitHandle (not a raw Unit*). We use the
//     UnitHasSkill(UnitHandle, skillId) overload so we stay on the
//     engine-facing API instead of raw pointers.
int JakobUniqueDamageRule(const Combat::DamageContext& ctx,
                          int                          currentDamage)
{
    // No attacker -> nothing to do.
    if (!ctx.attacker.IsValid())
        return currentDamage;

    // Only trigger when the attacker actually has Jakob's personal.
    if (!UnitHasSkill(ctx.attacker, kSkillId_JakobPersonal))
        return currentDamage;

    // Derive the hit count. DamageContext treats currentDamage as
    // TOTAL, so "+1 per hit" becomes:
    //
    //   delta = 1 * numHits
    //
    int numHits = (ctx.numHits > 0) ? ctx.numHits : 1;

    int delta  = numHits;
    int result = currentDamage + delta;

    // Safety clamps (mirror DamageEngineModifier’s bounds).
    if (result < 0)
        result = 0;
    else if (result > 999)
        result = 999;

    // Light debug window so we can see the rule firing.
    static int sLogCount = 0;
    if (sLogCount < 32)
    {
        void *atkRaw = ctx.attacker.Raw();

        Logf("JakobUniqueDamageRule: base=%d cur=%d numHits=%d "
             "-> result=%d (atk=%p, n=%d)",
             ctx.baseDamage,
             currentDamage,
             numHits,
             result,
             atkRaw,
             sLogCount + 1);
        ++sLogCount;
    }

    return result;
}

} // anonymous namespace

bool RegisterJakobUniqueDamageRule()
{
    // Damage::Init() is handled by SkillDamageRules::RegisterDamageRules(),
    // so here we only need to register the rule itself.
    const bool ok =
        Damage::RegisterRule(&JakobUniqueDamageRule,
                             "JakobUnique_Personal");

    if (!ok)
    {
        Logf("JakobUniqueDamageRule: failed to register rule");
        return false;
    }

    Logf("JakobUniqueDamageRule: registered skill-based damage rule");
    return true;
}

} // namespace Skills
} // namespace Engine
} // namespace Fates
