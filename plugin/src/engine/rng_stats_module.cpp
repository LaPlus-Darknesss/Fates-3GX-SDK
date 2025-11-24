// rng_stats_module.cpp
//
// Example engine module that listens to MapBegin/MapEnd and Rng
// events. It aggregates simple stats:
//
//   * Total RNG calls this map.
//   * RNG calls per side (by turn owner).
//   * A small histogram of distinct "bound" values requested.
//
// This is another self-contained reference for SDK users who want to
// build telemetry-style modules.

#include <cstdint>

#include "engine/bus.hpp"       // Register*Handler, context types
#include "util/debug_log.hpp"   // Logf

namespace Fates {
namespace Engine {

namespace {

constexpr int kMaxSides   = 4;
constexpr int kMaxBounds  = 8;  // cap on distinct bound values we track

struct BoundBucket
{
    std::uint32_t bound;  // upper bound seen in OnRngCall
    std::uint32_t count;  // how many times we saw this bound
};

struct RngStats
{
    std::uint32_t totalCalls;              // total RNG calls this map
    std::uint32_t callsPerSide[kMaxSides]; // indexed by TurnSide 0..3

    BoundBucket   bounds[kMaxBounds];
    int           numBounds;
};

static RngStats gRngStats{};

// Convert TurnSide to 0..3 index, or -1 if Unknown/out of range.
static int SideIndex(TurnSide side)
{
    int idx = static_cast<int>(side);
    if (0 <= idx && idx < kMaxSides)
        return idx;
    return -1;
}

static void ResetStats()
{
    gRngStats.totalCalls = 0;
    gRngStats.numBounds  = 0;

    for (int i = 0; i < kMaxSides; ++i)
        gRngStats.callsPerSide[i] = 0;

    for (int i = 0; i < kMaxBounds; ++i)
    {
        gRngStats.bounds[i].bound = 0;
        gRngStats.bounds[i].count = 0;
    }
}

static void HandleMapBegin(const MapContext &ctx)
{
    ResetStats();

    Logf("RngStatsModule: reset for new map (gen=%u, startSide=%s)",
         static_cast<unsigned>(ctx.generation),
         TurnSideToString(ctx.startSide));
}

static void HandleRng(const RngContext &ctx)
{
    ++gRngStats.totalCalls;

    // Attribute call to the current side if known.
    int idx = SideIndex(ctx.turn.side);
    if (idx >= 0)
        ++gRngStats.callsPerSide[idx];

    // Track distinct bound values, capped at kMaxBounds.
    std::uint32_t bound = ctx.bound;
    int found = -1;

    for (int i = 0; i < gRngStats.numBounds; ++i)
    {
        if (gRngStats.bounds[i].bound == bound)
        {
            found = i;
            break;
        }
    }

    if (found >= 0)
    {
        ++gRngStats.bounds[found].count;
    }
    else if (gRngStats.numBounds < kMaxBounds)
    {
        int slot = gRngStats.numBounds++;
        gRngStats.bounds[slot].bound = bound;
        gRngStats.bounds[slot].count = 1;
    }
    // If we exceed kMaxBounds distinct bounds, we silently drop new ones.
    // This keeps memory usage predictable and small.
}

static void HandleMapEnd(const MapContext &ctx)
{
    Logf("RngStatsModule: map summary gen=%u totalTurns=%u totalRngCalls=%u",
         static_cast<unsigned>(ctx.generation),
         static_cast<unsigned>(ctx.totalTurns),
         static_cast<unsigned>(gRngStats.totalCalls));

    // Per-side calls
    for (int i = 0; i < kMaxSides; ++i)
    {
        if (gRngStats.callsPerSide[i] == 0)
            continue;

        TurnSide side = static_cast<TurnSide>(i);

        Logf("  [%s] rngCalls=%u",
             TurnSideToString(side),
             static_cast<unsigned>(gRngStats.callsPerSide[i]));
    }

    // Bound histogram
    if (gRngStats.numBounds > 0)
    {
        Logf("  Bounds seen this map (capped at %d distinct):", kMaxBounds);
        for (int i = 0; i < gRngStats.numBounds; ++i)
        {
            const BoundBucket &b = gRngStats.bounds[i];
            Logf("    bound=%u calls=%u",
                 static_cast<unsigned>(b.bound),
                 static_cast<unsigned>(b.count));
        }
    }
}

} // anonymous namespace

bool RngStatsModule_RegisterHandlers()
{
    bool ok = true;

    ok = ok && RegisterMapBeginHandler(&HandleMapBegin);
    ok = ok && RegisterMapEndHandler(&HandleMapEnd);
    ok = ok && RegisterRngHandler(&HandleRng);

    if (ok)
    {
        Logf("RngStatsModule_RegisterHandlers: handlers registered");
    }
    else
    {
        Logf("RngStatsModule_RegisterHandlers: WARNING: some registrations failed");
    }

    return ok;
}

} // namespace Engine
} // namespace Fates
