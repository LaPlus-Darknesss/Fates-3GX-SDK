// engine/damage_stats_module.cpp
//
// Thin RE / telemetry layer on top of Engine::Combat. This module
// registers a single DamageModifierFn that:
//
//   * Sees every call to BTL_FinalDamage_Pre via DamageContext.
//   * Logs a compact, capped summary per call (map gen / side / damage).
//   * Returns currentDamage unchanged so gameplay behaviour is identical.
//
// Later we can either upgrade this into a proper "damage engine" or
// keep it as a debugging aid and add a separate module for real modifiers.

#include <cstdint>

#include "engine/damage_stats_module.hpp"
#include "engine/combat.hpp"
#include "core/runtime.hpp"   // gMapState, gCurrentTurnSide, TurnSideToString
#include "util/debug_log.hpp"

namespace Fates {
namespace Engine {
namespace DamageStats {

namespace {

// Identity damage modifier that just logs the DamageContext.
int DamageStatsModifier(const Combat::DamageContext &ctx,
                        int                          currentDamage)
{
    // Only bother if a map is active; this keeps menu / prologue noise down.
    if (!gMapState.mapActive)
        return currentDamage;

    // We only care about *final, applied HP damage* here.
    //  - Ignore forecast-only passes.
    //  - Ignore non-final passes.
    //  - Ignore non-positive "damage" (guards, misses, pure healing, etc.).
    if (ctx.origin != Combat::DamageOrigin::FinalHp)
        return currentDamage;

    if (!ctx.isFinalPass)
        return currentDamage;

    if (ctx.baseDamage <= 0)
        return currentDamage;

    // Reset per map generation so long campaigns don't exhaust the cap.
    static std::uint32_t sLastGeneration = 0;
    static int           sLogCount       = 0;

    if (ctx.map.generation != sLastGeneration)
    {
        sLastGeneration = ctx.map.generation;
        sLogCount       = 0;
    }

    // Keep the log tight – enough to see patterns, not enough to kill I/O.
    if (sLogCount < 128)
    {
        ++sLogCount;

        const char *sideStr   = TurnSideToString(ctx.turn.side);
        int         originInt = static_cast<int>(ctx.origin);  // should now always be FinalHp
        int         isFinal   = ctx.isFinalPass ? 1 : 0;       // should now always be 1

        Logf("DamageStats: gen=%u side=%s origin=%d final=%d "
             "base=%d current=%d hpB=%d hpA=%d forecast=%d "
             "atk=%p def=%p root=%p calc=%p turnIdx=%u totalTurns=%u (n=%d)",
             static_cast<unsigned>(ctx.map.generation),
             sideStr,
             originInt,
             isFinal,
             ctx.baseDamage,
             currentDamage,
             ctx.hpBefore,
             ctx.hpAfter,
             ctx.forecastTotal,
             ctx.attacker.Raw(),
             ctx.defender.Raw(),
             ctx.root,
             ctx.calc,
             static_cast<unsigned>(ctx.turn.sideTurnIndex),
             static_cast<unsigned>(ctx.map.totalTurns),
             sLogCount);
    }

    // IMPORTANT: identity modifier – never change gameplay.
    return currentDamage;
}

void InitOnce()
{
    static bool sInitialised = false;
    if (sInitialised)
        return;
    sInitialised = true;

    // Attach our identity modifier to the final-damage pipeline.
    bool ok = Combat::RegisterDamageModifier(&DamageStatsModifier);
    if (!ok)
    {
        Logf("DamageStats::Init: RegisterDamageModifier failed (capacity full?)");
    }
    else
    {
        Logf("DamageStats::Init: registered identity damage stats modifier");
    }
}

} // anonymous namespace

void Init()
{
    // Public entrypoint in case you ever want an explicit call.
    InitOnce();
}

} // namespace DamageStats

// Entrypoint called from InitCoreModules().
// Just forwards to DamageStats::Init() so older code keeps working.
void DamageStatsModule_RegisterHandlers()
{
    DamageStats::Init();
}

} // namespace Engine
} // namespace Fates
