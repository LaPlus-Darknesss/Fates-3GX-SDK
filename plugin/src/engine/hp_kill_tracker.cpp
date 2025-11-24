// engine/hp_kill_tracker.cpp
//
// Small per-map HP + kill summary engine built on top of the
// engine/bus event system. Listens to HpChange, Kill, and Map
// begin/end events and maintains lightweight aggregates that other
// systems (logging, logic systems, etc) can query.

#include "engine/hp_kill_tracker.hpp"
#include "engine/bus.hpp"
#include "engine/events.hpp"
#include "util/debug_log.hpp"

namespace Fates {
namespace Engine {

namespace {

// How many distinct units to track per map for per-unit stats.
constexpr std::size_t kMaxTrackedUnits = 64;

// Per-side HP stats (indices 0..3 correspond to TurnSide::Side0..Side3).
SideHpStats sSideStats[4] = {};

// Per-unit stats.
UnitHpStatsSnapshot sUnitStats[kMaxTrackedUnits];
std::size_t         sNumUnitStats = 0;

// Kill counts by side (0..3) + total kills for the current map.
std::uint32_t sKillsBySide[4] = {};
std::uint32_t sTotalKills     = 0;

// Simple metadata for summary logs.
std::uint32_t sMapGeneration   = 0;
std::uint32_t sTotalTurnsAtEnd = 0;

// Helpers ------------------------------------------------------------

// Convert a TurnSide into a 0..3 index, or -1 if Unknown/out of range.
static int SideToIndex(TurnSide side)
{
    int idx = static_cast<int>(side);
    if (idx < 0 || idx >= 4)
        return -1;
    return idx;
}

// Find or create the per-unit stats entry for a UnitHandle.
static UnitHpStatsSnapshot *FindOrCreateUnitStats(UnitHandle unit)
{
    void *raw = unit.Raw();
    if (raw == nullptr)
        return nullptr;

    // Look for an existing entry.
    for (std::size_t i = 0; i < sNumUnitStats; ++i)
    {
        if (sUnitStats[i].unit.Raw() == raw)
            return &sUnitStats[i];
    }

    // Need a new entry.
    if (sNumUnitStats >= kMaxTrackedUnits)
        return nullptr;  // silently drop if at capacity

    UnitHpStatsSnapshot &slot = sUnitStats[sNumUnitStats++];
    slot.unit            = unit;
    slot.damageTaken     = 0;
    slot.healingReceived = 0;
    return &slot;
}

// Reset all states for a new map.
static void ResetForMap(const MapContext &ctx)
{
    for (int i = 0; i < 4; ++i)
    {
        sSideStats[i].damageDealt   = 0;
        sSideStats[i].healingDone   = 0;
        sKillsBySide[i]             = 0;
    }

    sNumUnitStats     = 0;
    sTotalKills       = 0;
    sMapGeneration    = ctx.generation;
    sTotalTurnsAtEnd  = 0;

    Logf("HpKillTracker: MapBegin gen=%u seq=%p",
         static_cast<unsigned>(ctx.generation),
         ctx.seqRoot);
}

// Bus handlers -------------------------------------------------------

// MapBegin: reset per-map aggregates.
static void OnMapBeginHandler(const MapContext &ctx)
{
    ResetForMap(ctx);
}

// MapEnd: emit a summary log of what was tracked this map.
static void OnMapEndHandler(const MapContext &ctx)
{
    sTotalTurnsAtEnd = ctx.totalTurns;

    Logf("HpKillTracker: MapEndSummary gen=%u totalTurns=%u totalKills=%u",
         static_cast<unsigned>(sMapGeneration),
         static_cast<unsigned>(sTotalTurnsAtEnd),
         static_cast<unsigned>(sTotalKills));

    Logf("  KillsBySide: S0=%u S1=%u S2=%u S3=%u",
         static_cast<unsigned>(sKillsBySide[0]),
         static_cast<unsigned>(sKillsBySide[1]),
         static_cast<unsigned>(sKillsBySide[2]),
         static_cast<unsigned>(sKillsBySide[3]));

    // Per-side HP aggregates.
    for (int i = 0; i < 4; ++i)
    {
        Logf("  Side%d HP: dmgDealt=%d healDone=%d",
             i,
             static_cast<int>(sSideStats[i].damageDealt),
             static_cast<int>(sSideStats[i].healingDone));
    }

    // Per-unit stats: log a capped number to avoid spam.
    const std::size_t maxLogUnits = 32;
    for (std::size_t i = 0; i < sNumUnitStats && i < maxLogUnits; ++i)
    {
        const UnitHpStatsSnapshot &u = sUnitStats[i];
        Logf("  Unit%02u: ptr=%p dmgTaken=%d healRecv=%d",
             static_cast<unsigned>(i),
             u.unit.Raw(),
             static_cast<int>(u.damageTaken),
             static_cast<int>(u.healingReceived));
    }
}

// HpChange: update per-side and per-unit aggregates.
static void OnHpChangeHandler(const HpChangeContext &hc)
{
    const HpEvent &ev = hc.core;

    // Update per-side stats based on whose turn it is.
    int sideIdx = SideToIndex(hc.turn.side);
    if (sideIdx >= 0 && sideIdx < 4)
    {
        if (ev.amount > 0)
        {
            // Damage dealt by this side.
            sSideStats[sideIdx].damageDealt += ev.amount;
        }
        else if (ev.amount < 0)
        {
            // Healing done by this side.
            sSideStats[sideIdx].healingDone += -ev.amount;
        }
    }

    // Update per-target stats.
    UnitHpStatsSnapshot *slot = FindOrCreateUnitStats(ev.target);
    if (!slot)
        return;

    if (ev.amount > 0)
    {
        slot->damageTaken += ev.amount;
    }
    else if (ev.amount < 0)
    {
        slot->healingReceived += -ev.amount;
    }
}

// Kill: bump total kills and per-side kill counts.
static void OnKillHandler(const KillContext &kc)
{
    ++sTotalKills;

    int sideIdx = SideToIndex(kc.turn.side);
    if (sideIdx >= 0 && sideIdx < 4)
        ++sKillsBySide[sideIdx];
}

} // anonymous namespace

// Public API ---------------------------------------------------------

bool HpKillTracker_RegisterHandlers()
{
    bool ok = true;

    ok &= RegisterMapBeginHandler(&OnMapBeginHandler);
    ok &= RegisterMapEndHandler(&OnMapEndHandler);
    ok &= RegisterHpChangeHandler(&OnHpChangeHandler);
    ok &= RegisterKillHandler(&OnKillHandler);

    if (ok)
        Logf("HpKillTracker_RegisterHandlers: registered OK");
    else
        Logf("HpKillTracker_RegisterHandlers: FAILED to register one or more handlers");

    return ok;
}

const SideHpStats *HpKillTracker_GetSideStats()
{
    return sSideStats;
}

void HpKillTracker_GetUnitStats(const UnitHpStatsSnapshot *&outArray,
                                std::size_t               &outCount)
{
    outArray = sUnitStats;
    outCount = sNumUnitStats;
}

} // namespace Engine
} // namespace Fates
