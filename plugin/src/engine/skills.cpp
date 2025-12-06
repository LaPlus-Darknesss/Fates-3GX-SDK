// engine/skills.cpp
//
// First-pass skill engine wiring.
//
// This module currently does three things:
//
//   1) Maintains a lightweight per-map skill table keyed by
//      UnitState indices. The table is populated from
//      UNIT_SkillLearn + UNIT_HasSkillById observations (fed from
//      hooks_handlers) plus other "skill evidence" sites such as
//      skill-effect hooks and BattleRoot driver flags. Anything that
//      can assert "unit U has skill S" for this map is allowed to
//      feed the table.
//
//   2) Resets that table on each new map via a MapBegin handler.
//
//   3) Exposes a vanilla-first UnitHasSkill() front door. When the
//      hook layer provides a thunk for Unit__HasSkillById,
//      UnitHasSkill() consults that function as canonical truth and
//      uses the per-map table as a derived snapshot / debug view,
//      not the primary source of truth.
//
// There is intentionally *no* damage or HP modification logic here.
// The combat pipeline is driven by Engine::Damage; this module just
// provides per-unit skill presence + applied-skill-id helpers.

#include "engine/skills.hpp"
#include "engine/bus.hpp"
#include "engine/events.hpp"
#include "engine/unit_state.hpp"
#include "util/debug_log.hpp"
#include "core/runtime.hpp"   // TurnSideToString
#include "engine/combat.hpp"  // Combat::DamageContext
#include "engine/skill_damage_rules.hpp"  // skill-based damage rules

#include <cstddef>
#include <cstdint>

namespace Fates {
namespace Engine {
namespace Skills {

namespace {

// ---------------------------------------------------------------------
// Basic per-map skill table
// ---------------------------------------------------------------------
//
//   * One slot per UnitState entry.
//   * Fixed caps on skills per unit.
//   * Populated by UNIT_SkillLearn + UNIT_HasSkillById observations.
//
// This is a derived snapshot for debug/telemetry and future
// plugin-only skills. The canonical truth for base-game skills
// comes from vanilla Unit__HasSkillById.

constexpr std::size_t kMaxTrackedUnits  = kMaxUnitStates;
// Bumped slightly so we have room for future aura/experiment slots.
// Vanilla equip slots are well under this.
constexpr std::size_t kMaxSkillsPerUnit = 16;

struct UnitSkillSet
{
    std::uint16_t skills[kMaxSkillsPerUnit];
    std::uint8_t  numSkills;
};

static UnitSkillSet gUnitSkills[kMaxTrackedUnits];

// Pointer to the real vanilla Unit__HasSkillById implementation.
//
// This is provided by the hook layer via SetVanillaUnitHasSkillThunk.
// When null, UnitHasSkill() will fall back to the per-map table only.
static UnitHasSkillByIdThunk sVanillaUnitHasSkill = nullptr;

// Clear all per-map skill state (called on plugin load + MapBegin).
static void ResetAllSkillSets()
{
    for (std::size_t i = 0; i < kMaxTrackedUnits; ++i)
    {
        gUnitSkills[i].numSkills = 0;
        for (std::size_t j = 0; j < kMaxSkillsPerUnit; ++j)
            gUnitSkills[i].skills[j] = 0;
    }
}

// Given a UnitHandle, get (or create) the corresponding per-map
// UnitState slot and return the skill set for that unit.
static UnitSkillSet *GetSkillSetForUnit(UnitHandle unit)
{
    if (!unit.IsValid())
        return nullptr;

    UnitStateIndex idx = UnitState_GetOrCreate(unit);
    if (idx == kInvalidUnitStateIndex)
        return nullptr;

    std::size_t sidx = static_cast<std::size_t>(idx);
    if (sidx >= kMaxTrackedUnits)
        return nullptr;

    return &gUnitSkills[sidx];
}

// Cap logging of overflow so we don't spam the log if something goes
// wrong and a unit accumulates too many skills.
static int sOverflowLogCount = 0;

static bool AddSkillToSet(UnitSkillSet &set, std::uint16_t skillId)
{
    if (skillId == 0)
        return false;

    // Avoid duplicates.
    for (std::uint8_t i = 0; i < set.numSkills; ++i)
    {
        if (set.skills[i] == skillId)
            return false;
    }

    if (set.numSkills >= kMaxSkillsPerUnit)
    {
        if (sOverflowLogCount < 8)
        {
            Logf("Skills::AddSkillToSet: skill=0x%04X dropped "
                 "(numSkills=%u, cap=%u)",
                 static_cast<unsigned>(skillId),
                 static_cast<unsigned>(set.numSkills),
                 static_cast<unsigned>(kMaxSkillsPerUnit));
            ++sOverflowLogCount;
        }
        return false;
    }

    set.skills[set.numSkills++] = skillId;
    return true;
}

// Canonical "does this unit have skill X?" front door.
//
// Behaviour:
//
//   * If sVanillaUnitHasSkill is non-null, call the real
//     Unit__HasSkillById and treat its result as authoritative for
//     base-game skills.
//
//   * When vanilla reports "has skill", we ensure the per-map table
//     also records that fact (it remains a derived snapshot).
//
//   * If the thunk has not yet been set, or if you explicitly want to
//     traffic in plugin-only / experimental skills, the per-map table
//     is consulted as a fallback.
//
// We deliberately do NOT allow the per-map table to override a
// negative vanilla result for base-game skills. That prevents stale
// entries from lying if a unit ever loses a skill mid-map.
static bool UnitHasSkillInternal(void *unitRaw, std::uint16_t skillId)
{
    if (unitRaw == nullptr || skillId == 0)
        return false;

    // Prefer the real vanilla function if wired.
    if (sVanillaUnitHasSkill)
    {
        int res = sVanillaUnitHasSkill(unitRaw,
                                       static_cast<std::int16_t>(skillId));
        bool hasVanilla = (res != 0);

        if (hasVanilla)
        {
            // Keep our per-map snapshot in sync as "known evidence".
            UnitHandle unit(unitRaw);
            if (UnitSkillSet *set = GetSkillSetForUnit(unit))
            {
                AddSkillToSet(*set, skillId);
            }
            return true;
        }

        // If vanilla says "no", we *do not* override that with a
        // stale "yes" from the per-map table for base-game skills.
        // Fall through to table lookup only as a potential extension
        // point for plugin-only skills.
    }

    // Fallback / plugin-only path: consult the per-map snapshot.
    UnitHandle unit(unitRaw);
    UnitSkillSet *set = GetSkillSetForUnit(unit);
    if (!set)
        return false;

    for (std::uint8_t i = 0; i < set->numSkills; ++i)
    {
        if (set->skills[i] == skillId)
            return true;
    }

    return false;
}

static void DebugDumpSkillSets()
{
    const UnitStateEntry *entries = UnitState_GetEntries();
    std::size_t           count   = UnitState_GetCount();

    Logf("[Skills::DebugDump] unitStates=%u (cap=%u)",
         static_cast<unsigned>(count),
         static_cast<unsigned>(kMaxTrackedUnits));

    for (std::size_t i = 0; i < count && i < kMaxTrackedUnits; ++i)
    {
        const UnitStateEntry &entry = entries[i];
        const UnitSkillSet   &set   = gUnitSkills[i];

        Logf("[Skills::DebugDump] %02u: unit=%p num=%u "
             "s[0..15]={%04X,%04X,%04X,%04X,%04X,%04X,%04X,%04X,"
             "%04X,%04X,%04X,%04X,%04X,%04X,%04X,%04X}",
             static_cast<unsigned>(i),
             entry.unit.Raw(),
             static_cast<unsigned>(set.numSkills),
             set.skills[0],  set.skills[1],  set.skills[2],  set.skills[3],
             set.skills[4],  set.skills[5],  set.skills[6],  set.skills[7],
             set.skills[8],  set.skills[9],  set.skills[10], set.skills[11],
             set.skills[12], set.skills[13], set.skills[14], set.skills[15]);
    }
}

// Guard so InitDebugSkills() is idempotent even if called twice.
static bool sRegistered = false;

// MapBegin handler: reset per-map skill state *and* the UnitState
// registry so indices + skill sets stay in lock-step.
static void HandleMapBegin(const MapContext &ctx)
{
    (void)ctx;

    UnitState_ResetForMap();
    ResetAllSkillSets();

    Logf("Skills: ResetAllSkillSets + UnitState for new map (gen=%u)",
         static_cast<unsigned>(ctx.generation));
}

} // anonymous namespace

// ---------------------------------------------------------------------
// Public entrypoints
// ---------------------------------------------------------------------

void SetVanillaUnitHasSkillThunk(UnitHasSkillByIdThunk fn)
{
    sVanillaUnitHasSkill = fn;
}

void InitDebugSkills()
{
    if (sRegistered)
        return;

    // Clear any stale state in case the plugin survives across maps.
    UnitState_ResetForMap();
    ResetAllSkillSets();

    bool ok = true;

    // Keep skill tables scoped per map.
    ok = ok && ::Fates::Engine::RegisterMapBeginHandler(&HandleMapBegin);

    sRegistered = ok;

    Logf("Engine::Skills::InitDebugSkills: handlers -> %s",
         ok ? "OK" : "FAILED");
}

// Called from Hook_UNIT_SkillLearn (hooks_handlers.cpp).
void OnUnitSkillLearnRaw(void          *unitRaw,
                         std::uint16_t  skillId,
                         std::uint16_t  flags,
                         std::uint32_t  result,
                         TurnSide       side)
{
    (void)flags;
    (void)side;

    if (unitRaw == nullptr || skillId == 0 || result == 0)
        return;

    UnitHandle unit(unitRaw);
    UnitSkillSet *set = GetSkillSetForUnit(unit);
    if (!set)
        return;

    bool added = AddSkillToSet(*set, skillId);
    if (!added)
        return;

    static int sLogCount = 0;
    if (sLogCount < 64)
    {
        Logf("Skills::OnUnitSkillLearnRaw: unit=%p skill=0x%04X "
             "result=%u side=%s (n=%d)",
             unitRaw,
             static_cast<unsigned>(skillId),
             static_cast<unsigned>(result),
             TurnSideToString(side),
             sLogCount + 1);
        ++sLogCount;
    }

    static bool sDumped = false;
    if (!sDumped)
    {
        DebugDumpSkillSets();
        sDumped = true;
    }
}

void OnUnitHasSkillObserved(void          *unitRaw,
                            std::uint16_t  skillId)
{
    if (unitRaw == nullptr || skillId == 0)
        return;

    UnitHandle unit(unitRaw);
    UnitSkillSet *set = GetSkillSetForUnit(unit);
    if (!set)
        return;

    bool added = AddSkillToSet(*set, skillId);
    if (!added)
        return;

    static int sLogCount = 0;
    if (sLogCount < 32)
    {
        Logf("Skills::OnUnitHasSkillObserved: unit=%p skill=0x%04X (n=%d)",
             unitRaw,
             static_cast<unsigned>(skillId),
             sLogCount + 1);
        ++sLogCount;
    }
}

bool UnitHasSkill(void          *unitRaw,
                  std::uint16_t  skillId)
{
    return UnitHasSkillInternal(unitRaw, skillId);
}

// Convenience overload for callers that already traffic in UnitHandle.
bool UnitHasSkill(UnitHandle     unit,
                  std::uint16_t  skillId)
{
    return UnitHasSkillInternal(unit.Raw(), skillId);
}

// Decode the applied skill id for this damage context based on the
// battle "root" flags. For support/damage skills that drive a hit,
// the dword at root+0x10 looks like:
//
//   0x400000A3, 0x4001005B, 0x4001005C, ...
//
// The low 16 bits match the skill id driving this effect. If there is
// no root pointer, or no skill driver, returns 0.
std::uint16_t GetAppliedSkillId(const Combat::DamageContext& ctx)
{
    // If there is no root pointer for this context, bail.
    if (!ctx.root)
        return 0;

    const std::uint32_t* words =
        reinterpret_cast<const std::uint32_t*>(ctx.root);

    // dword at +0x10
    std::uint32_t flags = words[0x10 / 4];

    return static_cast<std::uint16_t>(flags & 0xFFFFu);
}

// Debug helper: dump the entire per-map skill table. Thin wrapper so
// other modules / OSD code don't need access to the anonymous-namespace
// helper directly.
void DebugDumpAllSkillSets()
{
    DebugDumpSkillSets();
}

// ---------------------------------------------------------------------
// Static bootstrap
// ---------------------------------------------------------------------
//
// This tiny struct ensures InitDebugSkills() *and* the skill-based
// damage rules registration run automatically when the plugin is
// loaded, without you having to call them manually.
//

struct SkillsBootstrap
{
    SkillsBootstrap()
    {
        // Per-map skill tables + MapBegin reset hook.
        InitDebugSkills();

        // Bridge skill-based damage rules into Engine::Damage. This
        // will in turn pull in the damage engine TU and register the
        // DamageEngineModifier with Combat.
        RegisterDamageRules();
    }
};

static SkillsBootstrap sSkillsBootstrap;

} // namespace Skills
} // namespace Engine
} // namespace Fates
