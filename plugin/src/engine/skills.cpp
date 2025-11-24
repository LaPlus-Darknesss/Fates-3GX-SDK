// engine/skills.cpp
//
// First-pass skill engine wiring for Fates-3GX-SDK.
//
// This module registers handlers with the engine event bus and
// implements a single debug feature:
//
//   "HP Change Logging for Debug Skill":
//       Any unit that learns a specific debug skill ID will have
//       its HP change events logged via the engine event bus.
//
// The goal is to prove end-to-end wiring without mutating HP:
//   - Unit_AddEquipSkill -> OnUnitSkillLearn -> skill table
//   - UNIT_UpdateCloneHP -> OnUnitHpSync -> OnHpChange
//   - Skill engine handler filters by skill and logs context.
//
// No gameplay behavior is changed; this is purely observational.

#include "engine/skills.hpp"
#include "engine/bus.hpp"
#include "engine/events.hpp"
#include "util/debug_log.hpp"

#include <cstdint>

namespace Fates {
namespace Engine {

namespace {

// TEMP: Debug skill ID that marks units whose HP changes log.
// Feel free to change this to a proper custom skill ID.
constexpr std::uint16_t kDebugSkillId = 0x000E;

// Tiny side-table of units that have the debug skill.
// Track raw unit pointers (UnitHandle::Raw()) and do a linear scan.
// This is intentionally small and simple.
constexpr int kMaxDebugSkillUnits = 64;

void *sDebugSkillUnits[kMaxDebugSkillUnits] = {};
int   sNumDebugSkillUnits = 0;

// One-time initialisation guard for Skills::InitDebugSkills().
bool sInitialized = false;

void ClearDebugSkillUnits()
{
    sNumDebugSkillUnits = 0;
}

// Add a unit to the debug-skill table if there's room. No-op if
// it's already present.
void RegisterDebugSkillUnit(void *unitRaw)
{
    if (unitRaw == nullptr)
        return;

    // Already tracked?
    for (int i = 0; i < sNumDebugSkillUnits; ++i)
    {
        if (sDebugSkillUnits[i] == unitRaw)
            return;
    }

    if (sNumDebugSkillUnits >= kMaxDebugSkillUnits)
    {
        static bool sLogged = false;
        if (!sLogged)
        {
            Logf("SkillEngine[Debug]: debug-skill table full (cap=%d)", kMaxDebugSkillUnits);
            sLogged = true;
        }
        return;
    }

    sDebugSkillUnits[sNumDebugSkillUnits++] = unitRaw;

    Logf("SkillEngine[Debug]: unit=%p marked as having debug skill (count=%d)",
         unitRaw,
         sNumDebugSkillUnits);
}

// Check if a unit is in the debug-skill table.
bool UnitHasDebugSkill(void *unitRaw)
{
    if (unitRaw == nullptr)
        return false;

    for (int i = 0; i < sNumDebugSkillUnits; ++i)
    {
        if (sDebugSkillUnits[i] == unitRaw)
            return true;
    }
    return false;
}

// == Bus handlers ====================================================

// Map end: clear per-map skill state.
void MapEnd_DebugSkillReset(const MapContext &ctx)
{
    (void)ctx;
    ClearDebugSkillUnits();
    Logf("SkillEngine[Debug]: MapEnd -> cleared debug-skill table");
}

// Skill learn: if a unit successfully learns the debug skill, track it.
void SkillLearn_DebugTrackUnit(const SkillLearnContext &ctx)
{
    // Only care about successful learns.
    if (ctx.result <= 0)
        return;

    if (ctx.skillId != kDebugSkillId)
        return;

    void *unitRaw = ctx.unit.Raw();
    RegisterDebugSkillUnit(unitRaw);
}

// HP change: log HP changes only if the *target* has the debug skill.
void HpChange_DebugLogForMarkedUnit(const HpChangeContext &ctx)
{
    const HpEvent &ev = ctx.core;

    void *targetUnit = ev.target.Raw();
    if (targetUnit == nullptr)
        return;

    if (!UnitHasDebugSkill(targetUnit))
        return;

    // Lightweight logging so that the handler is firing,
    // but capped so it won't spam the log forever.
    static int sLogCount = 0;
    if (sLogCount < 64)
    {
        Logf("SkillEngine[Debug]: HpChange unit=%p amt=%d flags=0x%08X gen=%u side=%s sideTurn=%u",
             targetUnit,
             ev.amount,
             static_cast<unsigned>(ev.flags),
             static_cast<unsigned>(ctx.map.generation),
             TurnSideToString(ctx.turn.side),
             static_cast<unsigned>(ctx.turn.sideTurnIndex));
        ++sLogCount;
    }
}

} // anonymous namespace

namespace Skills {

void InitDebugSkills()
{
    if (sInitialized)
        return;

    sInitialized = true;
    ClearDebugSkillUnits();

    bool okEnd   = RegisterMapEndHandler(&MapEnd_DebugSkillReset);
    bool okLearn = RegisterSkillLearnHandler(&SkillLearn_DebugTrackUnit);
    bool okHp    = RegisterHpChangeHandler(&HpChange_DebugLogForMarkedUnit);

    if (!okEnd || !okLearn || !okHp)
    {
        Logf("SkillEngine[Debug]: InitDebugSkills FAILED (end=%d learn=%d hp=%d)",
             okEnd ? 1 : 0,
             okLearn ? 1 : 0,
             okHp ? 1 : 0);
    }
    else
    {
        Logf("SkillEngine[Debug]: InitDebugSkills complete (debugSkillId=0x%04X)",
             static_cast<unsigned>(kDebugSkillId));
    }
}

} // namespace Skills

namespace {

// Static bootstrap so debug skill is registered automatically
// when the plugin is loaded. If you ever prefer explicit initialisation,
// you can remove this and instead call
// Fates::Engine::Skills::InitDebugSkills();
struct SkillEngineBootstrap
{
    SkillEngineBootstrap()
    {
        Skills::InitDebugSkills();
    }
};

static SkillEngineBootstrap sSkillEngineBootstrap;

} // anonymous namespace

} // namespace Engine
} // namespace Fates
