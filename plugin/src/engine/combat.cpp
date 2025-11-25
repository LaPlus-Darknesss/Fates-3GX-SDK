// engine/combat.cpp
//
// Implementation of the final-damage modifier pipeline. This runs
// at the same stage as vanilla's final damage calculation, so anything
// you do here will show up in the forecast and actual HP loss.

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

int ApplyDamageModifiers(void  *root,
                         void  *calc,
                         void  *attackerRaw,
                         void  *defenderRaw,
                         int    baseDamage)
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

    int current = baseDamage;
    for (int i = 0; i < sNumModifiers; ++i)
    {
        if (sModifiers[i])
            current = sModifiers[i](ctx, current);
    }

    if (current < 0)
        current = 0;

    return current;
}

} // namespace Combat
} // namespace Engine
} // namespace Fates
