// engine/combat.cpp
//
// Implementation of the post-battle HP modifier pipeline.
// Hooks like SEQ_HpDamage call ApplyPostBattleHpModifiers(), which
// builds a context snapshot and then runs all registered modifiers.

#include "engine/combat.hpp"
#include "core/runtime.hpp"     // gMapState, gCurrentTurnSide, TurnSide
#include "util/debug_log.hpp"

namespace Fates {
namespace Engine {
namespace Combat {

namespace {

constexpr int kMaxPostBattleHpModifiers = 8;

PostBattleHpModifierFn sModifiers[kMaxPostBattleHpModifiers] = {};
int                    sNumModifiers                         = 0;

// Local helpers: duplicate the small map/turn snapshot logic
// from engine/events.cpp so this module can work independently.
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

bool RegisterPostBattleHpModifier(PostBattleHpModifierFn fn)
{
    if (fn == nullptr)
        return false;

    if (sNumModifiers >= kMaxPostBattleHpModifiers)
    {
        Logf("Engine::Combat::RegisterPostBattleHpModifier: capacity full (%d)",
             kMaxPostBattleHpModifiers);
        return false;
    }

    sModifiers[sNumModifiers++] = fn;
    Logf("Engine::Combat::RegisterPostBattleHpModifier: registered #%d",
         sNumModifiers);
    return true;
}

std::uint32_t ApplyPostBattleHpModifiers(void        *seq,
                                         int          mode,
                                         int          slot,
                                         std::uint32_t hp,
                                         void        *attackerRaw)
{
    // Start from the engine-provided value.
    int currentHp = static_cast<int>(hp);

    // Build a best-effort map/turn snapshot.
    TurnSide side =
        gMapState.mapActive ? gCurrentTurnSide : TurnSide::Unknown;

    PostBattleHpContext ctx{};
    ctx.map        = BuildMapContext();
    ctx.turn       = BuildTurnContext(side);
    ctx.attacker   = UnitHandle(attackerRaw);
    ctx.target     = UnitHandle(nullptr); // filled in later when RE'd
    ctx.seq        = seq;
    ctx.slot       = slot;
    ctx.mode       = mode;
    ctx.originalHp = hp;

    for (int i = 0; i < sNumModifiers; ++i)
    {
        if (sModifiers[i] != nullptr)
            currentHp = sModifiers[i](ctx, currentHp);
    }

    // Clamp to a sane range: HP can't go below 0.
    if (currentHp < 0)
        currentHp = 0;

    // If we ever want a max-HP upper clamp, it can be applied here.
    return static_cast<std::uint32_t>(currentHp);
}

} // namespace Combat
} // namespace Engine
} // namespace Fates
