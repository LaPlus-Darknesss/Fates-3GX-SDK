// engine/damage.cpp
//
// Core damage-rule pipeline that sits on top of Engine::Combat.
//
// This module is deliberately tiny and conservative:
//
//   * It installs a single DamageModifierFn into the Combat pipeline.
//   * It maintains a small table of "rules" that can adjust damage.
//   * It has no built-in gameplay rules; everything interesting is
//     registered from other modules (skills, terrain, global effects,
//     etc.) via Damage::RegisterRule.
//
// This keeps all custom damage logic centralized and testable, instead
// of scattering tweaks across hooks_handlers.cpp.

#include <cstddef>

#include "engine/damage.hpp"
#include "util/debug_log.hpp"

namespace Fates {
namespace Engine {
namespace Damage {

namespace {

constexpr int kMaxRules = 16;

struct RuleEntry {
    RuleFn      fn;
    const char* name;
};

RuleEntry sRules[kMaxRules];
int       sNumRules    = 0;
bool      sInitialised = false;

// ---------------------------------------------------------------------
// Main modifier wired into Engine::Combat.
//
// It just walks the registered rules in order, letting each one
// adjust the damage. If there are no rules, this is a pure passthrough.
// ---------------------------------------------------------------------
int DamageEngineModifier(const Combat::DamageContext& ctx,
                         int                          currentDamage)
{
    // Debug: log the first few calls so we can see the pipeline being hit
    // from both forecast + HP paths, even before any rules exist.
    static int sDebugLogCount = 0;
    if (sDebugLogCount < 32)
    {
        const char *originStr = "Unknown";
        switch (ctx.origin)
        {
        case Combat::DamageOrigin::Forecast: originStr = "Forecast"; break;
        case Combat::DamageOrigin::FinalHp:  originStr = "FinalHp";  break;
        default:                             originStr = "Unknown";  break;
        }

        Logf("DamageEngineModifier: origin=%s base=%d cur=%d hits=%d "
             "dmgPerHit=%d hpBefore=%d hpAfter=%d rules=%d",
             originStr,
             ctx.baseDamage,
             currentDamage,
             ctx.numHits,
             ctx.dmgPerHit,
             ctx.hpBefore,
             ctx.hpAfter,
             sNumRules);

        ++sDebugLogCount;
    }

    int dmg = currentDamage;

    for (int i = 0; i < sNumRules; ++i)
    {
        RuleFn fn = sRules[i].fn;
        if (!fn)
            continue;

        dmg = fn(ctx, dmg);

        if (dmg < 0)
            dmg = 0;
        else if (dmg > 999)
            dmg = 999;
    }

    return dmg;
}

// One-time wiring of the damage engine into Engine::Combat.
void InitOnce()
{
    if (sInitialised)
        return;
    sInitialised = true;

    const bool ok = Combat::RegisterDamageModifier(&DamageEngineModifier);
    if (!ok)
    {
        Logf("DamageEngine::Init: RegisterDamageModifier failed "
             "(capacity full?)");
    }
    else
    {
        Logf("DamageEngine::Init: registered damage rule pipeline "
             "(maxRules=%d)", kMaxRules);
    }
}

// Tiny static bootstrap so the damage engine hooks itself into the
// Combat pipeline at plugin load time, without needing explicit calls.
struct AutoInit
{
    AutoInit() { InitOnce(); }
};

static AutoInit sAutoInit;

} // anonymous namespace

bool RegisterRule(RuleFn fn, const char* debugName)
{
    if (!fn)
        return false;

    if (!sInitialised)
        InitOnce();

    if (sNumRules >= kMaxRules)
    {
        Logf("DamageEngine::RegisterRule: table full (max=%d) – "
             "cannot register '%s'",
             kMaxRules,
             debugName ? debugName : "<unnamed>");
        return false;
    }

    sRules[sNumRules].fn   = fn;
    sRules[sNumRules].name = debugName;
    ++sNumRules;

    Logf("DamageEngine::RegisterRule: registered '%s' at slot %d",
         debugName ? debugName : "<unnamed>",
         sNumRules - 1);

    return true;
}

void Init()
{
    InitOnce();
}

} // namespace Damage
} // namespace Engine
} // namespace Fates
