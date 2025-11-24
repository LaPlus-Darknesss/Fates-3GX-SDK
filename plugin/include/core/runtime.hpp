// runtime.hpp
//
// Declares global runtime data shared between the stub handlers and
// debugging utilities. In particular this exposes the gHookCount array
// used to count how many times each hook has fired during a session,
// plus a simple kill-event buffer hooked from ProcSequence::DeadEvent,
// and a very basic MapLifeCycleState that tracks per-map summary info.

#pragma once

#include <cstddef>
#include <cstdint>
#include "core/hooks.hpp"

// ---------------------------------------------------------------------
// Turn side tracking – global so all modules can share it.
// ---------------------------------------------------------------------

enum class TurnSide : std::uint8_t
{
    Unknown = 0xFF,
    Side0   = 0,
    Side1   = 1,
    Side2   = 2,
    Side3   = 3,
};

// Single global value updated by Hook_SEQ_TurnBegin.
extern volatile TurnSide gCurrentTurnSide;

// Small helper for logging / debugging.
inline const char *TurnSideToString(TurnSide side)
{
    switch (side)
    {
    case TurnSide::Side0: return "Side0";
    case TurnSide::Side1: return "Side1";
    case TurnSide::Side2: return "Side2";
    case TurnSide::Side3: return "Side3";
    default:              return "Unknown";
    }
}

namespace Fates
{

// ---------------------------------------------------------------------
// Global hook runtime state
// ---------------------------------------------------------------------

// Global counter array indexed by HookId. Each stub handler should
// increment the element corresponding to its hook ID whenever the hook
// fires. Debug utilities read from this array when displaying counts.
//
// These are given C linkage so that they are easy to reference from
// assembly, Ghidra labels, etc., without C++ name mangling issues.
extern "C" volatile std::uint32_t gHookCount[static_cast<std::size_t>(HookId_Count)];
extern "C" volatile bool          gHpApplyLogEnabled;

// Global toggle for any future "control enemy" feature.
extern bool gControlEnemyEnabled;

// ---------------------------------------------------------------------
// Kill event buffer
// ---------------------------------------------------------------------
//
// Simple representation of a kill event reported from
// map__SequenceBattle__anonymous_namespace__ProcSequence__DeadEvent.
//
// NOTE: For now only store raw pointers are stored. Will be decoded into
// Unit*/BattleUnit* etc once the structure is fully mapped.
struct KillEvent
{
    void *seq;      // SequenceBattle "this" pointer
    void *dead0;    // first dead entry (may be nullptr)
    void *dead1;    // second dead entry (may be nullptr)
    unsigned int flags;  // raw bitfield from seq+0x280
};

// Maximum number of kill events we store at once.
// This is per-session for now; later can reset per map/chapter.
static const int kMaxKillEvents = 64;

// Global kill event buffer + count (defined in runtime.cpp).
extern KillEvent gKillEvents[kMaxKillEvents];
extern int       gKillEventCount;

// Reset the kill event buffer (call at map start / chapter start).
void ResetKillEvents();

// Append a new kill event; returns false if buffer is full.
bool PushKillEvent(const KillEvent &ev);

// ---------------------------------------------------------------------
// Map lifecycle state (very basic)
// ---------------------------------------------------------------------

struct MapLifeCycleState
{
    void         *seqRoot;      // map__Sequence root pointer for this map
    std::uint32_t generation;   // increments every time a NEW MAP begins
    TurnSide      startSide;    // side when the map first began
    TurnSide      currentSide;  // last TurnBegin side we saw

    std::uint32_t totalTurns;   // total number of TurnBegin calls this map
    std::uint32_t turnCount[4]; // per-side turn counts [0..3]

    std::uint32_t killEvents;   // number of kill events this map

    // True while a map is actively running. Lets higher-level engine code
    // distinguish "real map turns" from stray SEQ_TurnBegin/End noise.
    bool          mapActive;
};

// Single global instance defined in runtime.cpp.
extern MapLifeCycleState gMapState;

// Fully reset map state + kill buffer (used at startup or hard reset).
void ResetMapState();

// ---------------------------------------------------------------------
// Per-map stats (kills, etc.) built on top of the lifecycle state
// ---------------------------------------------------------------------

struct MapStats
{
    std::uint32_t totalKills;
    // Indexed by TurnSide 0..3 (Side0..Side3). Out-of-range sides can
    // be clamped by callers.
    std::uint32_t killsBySide[4];
};

// Single global instance defined in runtime.cpp.
extern MapStats gMapStats;

// Reset the per-map statistics. Intended to be called at map start.
void ResetMapStats();

} // namespace Fates
