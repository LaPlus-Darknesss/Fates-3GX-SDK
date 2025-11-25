// engine/hit_stats_module.cpp
//
// Implementation of a simple hit telemetry module.
// Listens to HitCalc events and records hit/attempt counts per side
// for each map. Logs a summary when the map ends.
//
// This is a pure observer: it never modifies hit results.

#include "engine/hit_stats_module.hpp"
#include "engine/bus.hpp"
#include "engine/events.hpp"
#include "util/debug_log.hpp"
#include "core/runtime.hpp"  // TurnSideToString

#include <cstdint>
#include <cstddef>

namespace Fates {
namespace Engine {

namespace {

struct HitSideStats
{
    std::uint32_t attempts = 0;
    std::uint32_t hits     = 0;
};

struct HitStats
{
    HitSideStats  bySide[4];       // Side1..Side4 mapped to [0..3]
    std::uint32_t totalAttempts = 0;
    std::uint32_t totalHits     = 0;
};

static HitStats gHitStats;

// Map TurnSide -> index [0..3], or -1 if Unknown / out of range.
static int SideToIndex(TurnSide side)
{
    int s = static_cast<int>(side);
    // In your runtime, Side1 == 1, Side2 == 2, etc., Unknown == 0
    if (s >= 1 && s <= 4)
        return s - 1;
    return -1;
}

static void ResetHitStats()
{
    gHitStats.totalAttempts = 0;
    gHitStats.totalHits     = 0;

    for (int i = 0; i < 4; ++i)
    {
        gHitStats.bySide[i].attempts = 0;
        gHitStats.bySide[i].hits     = 0;
    }
}

// MapBegin: clear stats for the new map.
static void HandleMapBegin(const MapContext &ctx)
{
    ResetHitStats();

    Logf("HitStatsModule: reset for new map (gen=%u, startSide=%s)",
         static_cast<unsigned>(ctx.generation),
         TurnSideToString(ctx.startSide));
}

// MapEnd: dump a short summary of hit behaviour this map.
static void HandleMapEnd(const MapContext &ctx)
{
    const std::uint32_t totalA = gHitStats.totalAttempts;
    const std::uint32_t totalH = gHitStats.totalHits;
    const std::uint32_t totalRate =
        (totalA > 0) ? (totalH * 100u / totalA) : 0u;

    Logf("HitStatsModule: MapEnd gen=%u total attempts=%u hits=%u hitRate=%u%%",
         static_cast<unsigned>(ctx.generation),
         static_cast<unsigned>(totalA),
         static_cast<unsigned>(totalH),
         static_cast<unsigned>(totalRate));

    for (int i = 0; i < 4; ++i)
    {
        const std::uint32_t a = gHitStats.bySide[i].attempts;
        const std::uint32_t h = gHitStats.bySide[i].hits;
        const std::uint32_t r = (a > 0) ? (h * 100u / a) : 0u;

        // Sides are 1-based in logging.
        Logf("HitStatsModule:  side S%d attempts=%u hits=%u hitRate=%u%%",
             i + 1,
             static_cast<unsigned>(a),
             static_cast<unsigned>(h),
             static_cast<unsigned>(r));
    }
}

// HitCalc handler: record attempts + successes per side.
static void HandleHitCalc(const HitCalcContext &ctx)
{
    gHitStats.totalAttempts++;
    if (ctx.result != 0)
        gHitStats.totalHits++;

    int sideIdx = SideToIndex(ctx.turn.side);
    if (0 <= sideIdx && sideIdx < 4)
    {
        HitSideStats &ss = gHitStats.bySide[sideIdx];
        ss.attempts++;
        if (ctx.result != 0)
            ss.hits++;
    }

    // Optional per-call logging, capped to avoid spam.
    static int sLogCount = 0;
    if (sLogCount >= 64)
        return;

    ++sLogCount;

    Logf("HitStatsModule::HandleHitCalc: base=%d res=%d "
         "side=%s gen=%u sideTurn=%u totalTurns=%u (n=%d)",
         ctx.baseRate,
         ctx.result,
         TurnSideToString(ctx.turn.side),
         static_cast<unsigned>(ctx.map.generation),
         static_cast<unsigned>(ctx.turn.sideTurnIndex),
         static_cast<unsigned>(ctx.map.totalTurns),
         sLogCount);
}

} // anonymous namespace

// ---------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------

bool HitStatsModule_RegisterHandlers()
{
    bool ok = true;

    ok = ok && RegisterMapBeginHandler(&HandleMapBegin);
    ok = ok && RegisterMapEndHandler(&HandleMapEnd);
    ok = ok && RegisterHitCalcHandler(&HandleHitCalc);

    if (!ok)
        Logf("HitStatsModule_RegisterHandlers: FAILED");
    else
        Logf("HitStatsModule_RegisterHandlers: handlers registered");

    return ok;
}

} // namespace Engine
} // namespace Fates
