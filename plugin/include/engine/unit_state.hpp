// engine/unit_state.hpp
//
// Lightweight per-map unit registry for Fates-3GX-SDK.
// This module assigns each distinct UnitHandle seen during a map
// a small dense index in [0, N), which other engine modules can use
// to hang their own per-unit data off of.
//
// Design notes:
//  - The registry is map-local. Call UnitState_ResetForMap() at the
//    start of each new map (HpKillTracker does this already).
//  - Indices are stable for the lifetime of the map: calling
//    UnitState_GetOrCreate() for the same unit returns the same index.

#pragma once

#include <cstddef>
#include <cstdint>
#include "engine/types.hpp"

namespace Fates {
namespace Engine {

constexpr std::size_t kMaxUnitStates = 64;

using UnitStateIndex = std::uint16_t;
constexpr UnitStateIndex kInvalidUnitStateIndex =
    static_cast<UnitStateIndex>(0xFFFF);

struct UnitStateEntry
{
    UnitHandle unit;  // identity for this slot (raw Unit* wrapper)
};

// Reset the registry for a new map. All previous entries become invalid.
void UnitState_ResetForMap();

// Look up or assign an index for the given unit.
// Returns kInvalidUnitStateIndex on null unit or if capacity is exceeded.
UnitStateIndex UnitState_GetOrCreate(UnitHandle unit);

// Return the UnitHandle associated with a given index.
// Returns a default-constructed UnitHandle on invalid index.
UnitHandle UnitState_GetHandle(UnitStateIndex index);

// Number of active entries in the registry for the current map.
std::size_t UnitState_GetCount();

// Direct pointer to the internal entry array. Valid indices are
// [0, UnitState_GetCount()) for the current map.
const UnitStateEntry *UnitState_GetEntries();

} // namespace Engine
} // namespace Fates
