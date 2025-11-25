// engine/hp_kill_tracker.cpp
//
// Small per-map HP + kill summary engine built on top of the
// engine/bus event system. Listens to HpChange, Kill, and Map
// begin/end events and maintains lightweight aggregates that other
// systems (logging, logic systems, etc) can query.

#include "engine/hp_kill_tracker.hpp"
#include "engine/bus.hpp"
#include "engine/events.hpp"
#include "engine/unit_state.hpp"
#include "util/debug_log.hpp"

namespace Fates {
namespace Engine {

namespace {

// Per-side HP stats (indices 0..3 correspond to TurnSide::Side0..Side3).
SideHpStats sSideStats[4] = {};

// Simple per-unit accumulators, indexed by UnitStateIndex.
// The UnitState registry guarantees that indices are dense in
// [0, UnitState_GetCount()) for the current map.
struct UnitHpAccum
{
    std::int32_t damageTaken;
    std::int32_t healingReceived;
};

UnitHpAccum sUnitHpAccum[kMaxUnitStates];

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

// Reset all states for a new map.
static void ResetForMap(const MapContext &ctx)
{
    // Reset the shared per-map unit registry first so all indices
    // and handles are fresh for this battle.
    UnitState_ResetForMap();

    for (int i = 0; i < 4; ++i)
    {
        sSideStats[i].damageDealt = 0;
        sSideStats[i].healingDone = 0;
        sKillsBySide[i]           = 0;
    }

    // Clear per-unit accumulators.
    for (std::size_t i = 0; i < kMaxUnitStates; ++i)
    {
        sUnitHpAccum[i].damageTaken     = 0;
        sUnitHpAccum[i].healingReceived = 0;
    }

    sTotalKills      = 0;
    sMapGeneration   = ctx.generation;
    sTotalTurnsAtEnd = 0;

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
    const UnitStateEntry *entries     = UnitState_GetEntries();
    std::size_t           count       = UnitState_GetCount();
    const std::size_t     maxLogUnits = 32;

    if (count > kMaxUnitStates)
        count = kMaxUnitStates;

    for (std::size_t i = 0; i < count && i < maxLogUnits; ++i)
    {
        const UnitStateEntry &e = entries[i];
        const UnitHpAccum    &u = sUnitHpAccum[i];

        Logf("  Unit%02u: ptr=%p dmgTaken=%d healRecv=%d",
             static_cast<unsigned>(i),
             e.unit.Raw(),
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

    // Update per-target stats using the shared UnitState registry.
    UnitStateIndex idx = UnitState_GetOrCreate(ev.target);
    if (idx == kInvalidUnitStateIndex)
        return;

    std::size_t slotIdx = static_cast<std::size_t>(idx);
    if (slotIdx >= kMaxUnitStates)
        return; // should not happen, but guard anyway

    UnitHpAccum &slot = sUnitHpAccum[slotIdx];

    if (ev.amount > 0)
    {
        slot.damageTaken += ev.amount;
    }
    else if (ev.amount < 0)
    {
        slot.healingReceived += -ev.amount;
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
    // Snapshot current per-unit stats into a stable array for callers.
    static UnitHpStatsSnapshot sSnapshot[kMaxUnitStates];

    const UnitStateEntry *entries = UnitState_GetEntries();
    std::size_t           count   = UnitState_GetCount();

    if (count > kMaxUnitStates)
        count = kMaxUnitStates;

    for (std::size_t i = 0; i < count; ++i)
    {
        sSnapshot[i].unit            = entries[i].unit;
        sSnapshot[i].damageTaken     = sUnitHpAccum[i].damageTaken;
        sSnapshot[i].healingReceived = sUnitHpAccum[i].healingReceived;
    }

    outArray = sSnapshot;
    outCount = count;
}

const SideHpStats *HpKillTracker_GetSideStatsFor(TurnSide side)
{
    int idx = SideToIndex(side);
    if (idx < 0 || idx >= 4)
        return nullptr;

    return &sSideStats[idx];
}

bool HpKillTracker_QueryUnitStats(UnitHandle           unit,
                                  UnitHpStatsSnapshot &outStats)
{
    void *raw = unit.Raw();
    if (!raw)
        return false;

    const UnitStateEntry *entries = UnitState_GetEntries();
    std::size_t           count   = UnitState_GetCount();

    if (count > kMaxUnitStates)
        count = kMaxUnitStates;

    for (std::size_t i = 0; i < count; ++i)
    {
        if (entries[i].unit.Raw() == raw)
        {
            outStats.unit            = entries[i].unit;
            outStats.damageTaken     = sUnitHpAccum[i].damageTaken;
            outStats.healingReceived = sUnitHpAccum[i].healingReceived;
            return true;
        }
    }

    return false;
}

} // namespace Engine
} // namespace Fates
