// damage_stats_module.cpp
//
// Example engine module that listens to MapBegin/MapEnd, HpChange,
// and Kill events, aggregates a few simple stats per side, and
// prints a summary when the map ends.
//
// This is intentionally small and self-contained so SDK users can
// copy its structure for their own modules.

#include <cstdint>

#include "engine/bus.hpp"       // Register*Handler, context types
#include "util/debug_log.hpp"   // Logf
// engine/bus.hpp includes engine/events.hpp, which in turn includes
// core/runtime.hpp (TurnSide, TurnSideToString, etc.).

namespace Fates {
namespace Engine {

namespace {

// Match gMapState.turnCount[4] in core/runtime.hpp.
constexpr int kMaxSides = 4;

struct DamageStats
{
    // For each TurnSide index 0..3 (Player, Enemy, Other, ???)
    std::uint32_t hpEvents[kMaxSides];    // number of HpChange events seen
    std::int32_t  totalDamage[kMaxSides]; // sum of damage (amount > 0)
    std::int32_t  totalHeals[kMaxSides];  // sum of healing (amount < 0, stored as positive)
    std::uint32_t kills[kMaxSides];       // number of Kill events
};

static DamageStats gStats{};

// Helper: convert TurnSide to 0..3 index, or -1 if Unknown/out of range.
static int SideIndex(TurnSide side)
{
    int idx = static_cast<int>(side);
    if (0 <= idx && idx < kMaxSides)
        return idx;
    return -1;
}

// MapBegin: reset stats at the start of each map.
static void HandleMapBegin(const MapContext &ctx)
{
    (void)ctx;  // unused for now

    gStats = DamageStats{};  // value-init to zero

    Logf("DamageStatsModule: reset for new map (gen=%u, startSide=%s)",
         static_cast<unsigned>(ctx.generation),
         TurnSideToString(ctx.startSide));
}

// HpChange: accumulate damage / healing by whose turn it is.
static void HandleHpChange(const HpChangeContext &ctx)
{
    int idx = SideIndex(ctx.turn.side);
    if (idx < 0)
        return;  // Unknown side or out-of-range

    int amount = ctx.core.amount;
    if (amount == 0)
        return;

    ++gStats.hpEvents[idx];

    if (amount > 0)
    {
        // Damage taken by target during this side's turn.
        gStats.totalDamage[idx] += amount;
    }
    else
    {
        // Healing received during this side's turn.
        gStats.totalHeals[idx] += -amount;  // store as positive
    }
}

// Kill: increment kill count for the active side at time of kill.
static void HandleKill(const KillContext &ctx)
{
    int idx = SideIndex(ctx.turn.side);
    if (idx < 0)
        return;

    ++gStats.kills[idx];
}

// MapEnd: log a per-side summary for the map.
static void HandleMapEnd(const MapContext &ctx)
{
    Logf("DamageStatsModule: map summary gen=%u totalTurns=%u",
         static_cast<unsigned>(ctx.generation),
         static_cast<unsigned>(ctx.totalTurns));

    for (int i = 0; i < kMaxSides; ++i)
    {
        // Skip sides that never had any HP events or kills this map.
        if (gStats.hpEvents[i] == 0 && gStats.kills[i] == 0)
            continue;

        TurnSide side = static_cast<TurnSide>(i);

        Logf("  [%s] hpEvents=%u damage=%d heals=%d kills=%u",
             TurnSideToString(side),
             static_cast<unsigned>(gStats.hpEvents[i]),
             static_cast<int>(gStats.totalDamage[i]),
             static_cast<int>(gStats.totalHeals[i]),
             static_cast<unsigned>(gStats.kills[i]));
    }
}

} // anonymous namespace

bool DamageStatsModule_RegisterHandlers()
{
    bool ok = true;

    ok = ok && RegisterMapBeginHandler(&HandleMapBegin);
    ok = ok && RegisterMapEndHandler(&HandleMapEnd);
    ok = ok && RegisterHpChangeHandler(&HandleHpChange);
    ok = ok && RegisterKillHandler(&HandleKill);

    if (ok)
    {
        Logf("DamageStatsModule_RegisterHandlers: handlers registered");
    }
    else
    {
        Logf("DamageStatsModule_RegisterHandlers: WARNING: some registrations failed");
    }

    return ok;
}

} // namespace Engine
} // namespace Fates
