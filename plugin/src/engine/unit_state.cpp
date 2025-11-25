// engine/unit_state.cpp
//
// Implementation of the lightweight per-map unit registry declared
// in engine/unit_state.hpp. This module does *not* own any gameplay
// logic; it only maps UnitHandle -> small dense indices so that other
// engine modules can maintain per-unit data structures.

#include "engine/unit_state.hpp"
#include "util/debug_log.hpp"

namespace Fates {
namespace Engine {

namespace {

UnitStateEntry sEntries[kMaxUnitStates];
std::size_t    sCount = 0;

} // anonymous namespace

void UnitState_ResetForMap()
{
    sCount = 0;
}

UnitStateIndex UnitState_GetOrCreate(UnitHandle unit)
{
    void *raw = unit.Raw();
    if (raw == nullptr)
        return kInvalidUnitStateIndex;

    // Look for an existing entry.
    for (std::size_t i = 0; i < sCount; ++i)
    {
        if (sEntries[i].unit.Raw() == raw)
            return static_cast<UnitStateIndex>(i);
    }

    // Need a new entry.
    if (sCount >= kMaxUnitStates)
    {
        Logf("UnitState_GetOrCreate: capacity (%u) reached; dropping unit=%p",
             static_cast<unsigned>(kMaxUnitStates),
             raw);
        return kInvalidUnitStateIndex;
    }

    std::size_t idx = sCount++;
    sEntries[idx].unit = unit;

    return static_cast<UnitStateIndex>(idx);
}

UnitHandle UnitState_GetHandle(UnitStateIndex index)
{
    if (index == kInvalidUnitStateIndex)
        return UnitHandle(nullptr);

    std::size_t idx = static_cast<std::size_t>(index);
    if (idx >= sCount)
        return UnitHandle(nullptr);

    return sEntries[idx].unit;
}

std::size_t UnitState_GetCount()
{
    return sCount;
}

const UnitStateEntry *UnitState_GetEntries()
{
    return sEntries;
}

} // namespace Engine
} // namespace Fates
