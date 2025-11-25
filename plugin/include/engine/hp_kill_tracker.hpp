// engine/hp_kill_tracker.hpp
//
// Small per-map HP + kill summary engine built on top of the
// engine/bus event system. Listens to HpChange, Kill, and Map
// begin/end events and maintains lightweight aggregates that other
// systems (logging, logic systems, etc) can query.

#pragma once

#include <cstddef>
#include <cstdint>
#include "engine/types.hpp"
#include "core/runtime.hpp"

namespace Fates {
namespace Engine {

struct SideHpStats
{
    // Total HP *damage dealt* by this side during the current map.
    // (Sum of HpEvent.amount where amount > 0 and turn.side == this side.)
    std::int32_t damageDealt;

    // Total HP *healing done* by this side during the current map.
    // (Sum of -HpEvent.amount where amount < 0 and turn.side == this side.)
    std::int32_t healingDone;
};

struct UnitHpStatsSnapshot
{
    UnitHandle   unit;
    std::int32_t damageTaken;
    std::int32_t healingReceived;
};

/// Register the HP/kill tracker with the engine bus.
///
/// Call this once during plugin startup (after the bus is available)
/// to hook into MapBegin/MapEnd, HpChange, and Kill events.
///
/// Returns true on success; false if any registration failed.
bool HpKillTracker_RegisterHandlers();

/// Returns a pointer to an internal array of 4 per-side stats
/// (indices 0..3 correspond to TurnSide::Side0..Side3).
///
/// Valid only for the *current map*. Data is reset on each MapBegin.
const SideHpStats *HpKillTracker_GetSideStats();

/// Returns a pointer to an internal array of per-unit stats plus count.
/// The array contains one entry per unit that has taken damage or
/// received healing during the current map. Data is reset on MapBegin.
///
/// The underlying storage is owned by the tracker; callers must not
/// modify it.
void HpKillTracker_GetUnitStats(const UnitHpStatsSnapshot *&outArray,
                                std::size_t               &outCount);

/// Convenience: return stats for a specific side, or nullptr if
/// the side is Unknown or out of range. Pointer is valid only for
/// the current map (reset on MapBegin).
const SideHpStats *HpKillTracker_GetSideStatsFor(TurnSide side);

/// Convenience: query per-unit HP stats for a specific unit.
/// Returns true and fills outStats if the unit has a registered
/// entry this map, or false if the unit has not taken damage or
/// received healing yet.
///
/// outStats is a by-value snapshot; callers own the copy.
bool HpKillTracker_QueryUnitStats(UnitHandle              unit,
                                  UnitHpStatsSnapshot    &outStats);

} // namespace Engine
} // namespace Fates
