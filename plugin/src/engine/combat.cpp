// engine/combat.cpp
//
// Implementation of the low-level damage modifier chain.
//
// This module is intentionally small:
//   * Holds a fixed-size table of DamageModifierFn callbacks.
//   * Builds DamageContext (map + turn + unit handles) from global
//     runtime state.
//   * Applies modifiers in registration order, with a final clamp
//     to keep obviously bad values from leaking into the game.
//
// Higher-level systems (Engine::Damage, skills, terrain, etc.) plug
// into this via Combat::RegisterDamageModifier.

#include <cstdint>

#include "engine/combat.hpp"
#include "core/runtime.hpp"   // gMapState, gCurrentTurnSide, TurnSide
#include "util/debug_log.hpp"

namespace Fates {
namespace Engine {
namespace Combat {

namespace {

constexpr int kMaxDamageModifiers = 8;

DamageModifierFn sModifiers[kMaxDamageModifiers] = {};
int              sNumModifiers                   = 0;

// Global damage mode. Default to Vanilla; callers can promote this
// to Mirror/Full during plugin initialisation once everything is
// wired and tested.
static DamageMode sCurrentMode = DamageMode::Vanilla;

// NOTE: These helpers intentionally mirror the ones in engine/events.cpp.
// If you change MapContext / TurnContext layout or semantics, keep the
// two BuildMapContext/BuildTurnContext implementations in sync.
static MapContext BuildMapContext()
{
    MapContext ctx{};
    ctx.seqRoot     = gMapState.seqRoot;
    ctx.generation  = gMapState.generation;
    ctx.startSide   = gMapState.startSide;
    ctx.currentSide = gMapState.currentSide;
    ctx.totalTurns  = gMapState.totalTurns;
    ctx.killEvents  = gMapState.killEvents;
    return ctx;
}

static TurnContext BuildTurnContext(TurnSide side)
{
    TurnContext tc{};
    tc.map  = BuildMapContext();
    tc.side = side;

    int idx = static_cast<int>(side);
    if (0 <= idx && idx < 4)
        tc.sideTurnIndex = gMapState.turnCount[idx];
    else
        tc.sideTurnIndex = 0;

    return tc;
}

} // anonymous namespace

DamageMode GetCurrentDamageMode()
{
    return sCurrentMode;
}

void SetCurrentDamageMode(DamageMode mode)
{
    sCurrentMode = mode;
}

bool RegisterDamageModifier(DamageModifierFn fn)
{
    if (!fn)
        return false;

    if (sNumModifiers >= kMaxDamageModifiers)
    {
        Logf("Engine::Combat::RegisterDamageModifier: capacity full (%d)",
             kMaxDamageModifiers);
        return false;
    }

    sModifiers[sNumModifiers++] = fn;
    Logf("Engine::Combat::RegisterDamageModifier: registered #%d",
         sNumModifiers);
    return true;
}

int ApplyDamageModifiersEx(void        *root,
                           void        *calc,
                           void        *attackerRaw,
                           void        *defenderRaw,
                           int          baseDamage,
                           DamageOrigin origin,
                           bool         isFinalPass,
                           int          hpBefore,
                           int          hpAfter,
                           int          forecastTotal,
                           int          numHits,
                           int          dmgPerHit)
{
    // If nothing is registered, just clamp and return.
    if (sNumModifiers == 0)
        return (baseDamage < 0) ? 0 : baseDamage;

    TurnSide side =
        gMapState.mapActive ? gCurrentTurnSide : TurnSide::Unknown;

    DamageContext ctx{};
    ctx.map        = BuildMapContext();
    ctx.turn       = BuildTurnContext(side);
    ctx.attacker   = UnitHandle(attackerRaw);
    ctx.defender   = UnitHandle(defenderRaw);
    ctx.root       = root;
    ctx.calc       = calc;
    ctx.baseDamage = baseDamage;

    ctx.origin      = origin;
    ctx.isFinalPass = isFinalPass;
    ctx.mode        = GetCurrentDamageMode();

    // Invariant flags / metadata.
    ctx.didChangeFromVanilla = false;

    ctx.hpBefore      = hpBefore;
    ctx.hpAfter       = hpAfter;
    ctx.forecastTotal = forecastTotal;
    ctx.numHits       = numHits;
    ctx.dmgPerHit     = dmgPerHit;

    // Pipeline snapshot: starts at baseDamage and is updated as we
    // walk the modifier chain.
    ctx.currentDamage = baseDamage;

    int current = baseDamage;
    for (int i = 0; i < sNumModifiers; ++i)
    {
        DamageModifierFn fn = sModifiers[i];
        if (!fn)
            continue;

        int next = fn(ctx, current);

        // Keep the snapshot in sync with the pipeline.
        current           = next;
        ctx.currentDamage = current;

        // Once flipped to true, this stays true even if future rules
        // bring the value back to baseDamage. This tells us that the
        // pipeline touched the damage at some point.
        if (!ctx.didChangeFromVanilla && current != ctx.baseDamage)
            ctx.didChangeFromVanilla = true;
    }

    if (current < 0)
        current = 0;

    return current;
}

int ApplyDamageModifiers(void  *root,
                         void  *calc,
                         void  *attackerRaw,
                         void  *defenderRaw,
                         int    baseDamage)
{
    // Legacy front-door: we just call the extended version with
    // minimal metadata so older call sites keep working.
    return ApplyDamageModifiersEx(
        root,
        calc,
        attackerRaw,
        defenderRaw,
        baseDamage,
        DamageOrigin::Unknown,
        /*isFinalPass=*/false,
        /*hpBefore=*/0,
        /*hpAfter=*/0,
        /*forecastTotal=*/0,
        /*numHits=*/0,
        /*dmgPerHit=*/0);
}

} // namespace Combat
} // namespace Engine
} // namespace Fates
