// runtime.cpp
//
// Defines the shared runtime data declared in runtime.hpp: hook counters,
// HP logging toggle, the kill-event buffer used by Hook_HP_KillCheck, and
// the basic MapLifeCycleState summary struct.

#include "core/runtime.hpp"

// Global turn-side value (no namespace).
volatile TurnSide gCurrentTurnSide = TurnSide::Unknown;

namespace Fates
{

// ---------------------------------------------------------------------
// Global hook runtime state
// ---------------------------------------------------------------------

extern "C" volatile std::uint32_t gHookCount[
    static_cast<std::size_t>(HookId_Count)
] = {};

extern "C" volatile bool gHpApplyLogEnabled = false;

// Future feature toggle, default disabled.
bool gControlEnemyEnabled = false;

// ---------------------------------------------------------------------
// Map lifecycle state
// ---------------------------------------------------------------------

MapLifeCycleState gMapState = {
    nullptr,              // seqRoot
    0u,                   // generation
    TurnSide::Unknown,    // startSide
    TurnSide::Unknown,    // currentSide
    0u,                   // totalTurns
    {0u, 0u, 0u, 0u},     // turnCount[4]
    0u,                   // killEvents
    false                 // mapActive
};

// Per-map statistics: total kills, kills split by side.
MapStats gMapStats = {
    0u,                   // totalKills
    {0u, 0u, 0u, 0u}      // killsBySide[4]
};

void ResetMapState()
{
    gMapState.seqRoot     = nullptr;
    gMapState.generation  = 0;
    gMapState.startSide   = TurnSide::Unknown;
    gMapState.currentSide = TurnSide::Unknown;

    gMapState.totalTurns = 0;
    for (int i = 0; i < 4; ++i)
        gMapState.turnCount[i] = 0;

    gMapState.killEvents = 0;
    gMapState.mapActive  = false;

    // Treat kill-events as per-map going forward.
    ResetKillEvents();

    // Also reset the per-map stats at startup / hard reset. Engine code
    // is free to call ResetMapStats() again at map begin.
    ResetMapStats();
}

void ResetMapStats()
{
    gMapStats.totalKills = 0;
    for (int i = 0; i < 4; ++i)
        gMapStats.killsBySide[i] = 0;
}

// ---------------------------------------------------------------------
// Kill event buffer
// ---------------------------------------------------------------------

KillEvent gKillEvents[kMaxKillEvents];
int       gKillEventCount = 0;

void ResetKillEvents()
{
    gKillEventCount = 0;

    for (int i = 0; i < kMaxKillEvents; ++i)
    {
        gKillEvents[i].seq   = nullptr;
        gKillEvents[i].dead0 = nullptr;
        gKillEvents[i].dead1 = nullptr;
        gKillEvents[i].flags = 0u;
    }
}

bool PushKillEvent(const KillEvent &ev)
{
    if (gKillEventCount >= kMaxKillEvents)
        return false;

    gKillEvents[gKillEventCount++] = ev;

    // Also update the per-map summary counter.
    ++gMapState.killEvents;

    return true;
}

} // namespace Fates
