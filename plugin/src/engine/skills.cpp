// engine/skills.cpp
//
// First-pass skill engine wiring.
//
// This module now does two things:
//
//   1) Maintains a lightweight per-map skill table based on
//      UNIT_SkillLearn (fed from hooks_handlers.cpp). Other engine
//      modules can query this via Skills::UnitHasSkill().
//
//   2) Provides a debug-only “flat damage increase” observer on the
//      HP-change event bus. This remains read-only for now; we just
//      log what the damage *would* look like with a bonus so we can
//      validate shapes and ordering safely.
//
// The actual combat modification hooks (hit / damage / crit) will be
// layered on top of this in separate modules once the battle math
// events are in place.

#include "engine/skills.hpp"
#include "engine/bus.hpp"
#include "engine/events.hpp"
#include "util/debug_log.hpp"
#include "core/runtime.hpp"  // TurnSideToString

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
// For now this is intentionally simple:
//   * Keyed by raw Unit*.
//   * Fixed caps on units + skills per unit.
//   * Populated only by UNIT_SkillLearn (level-ups, scrolls, scripts).
//
// This is enough to drive “custom skill” experiments and global auras
// without committing to a heavy-weight data model.

constexpr std::size_t kMaxTrackedUnits     = 256;
constexpr std::size_t kMaxSkillsPerUnit    = 8;

// Reuse the same debug skill ID as the legacy table in hooks_handlers.cpp.
// That table is RE-only; this module is the canonical "engine view" of skills.
constexpr std::uint16_t kDebugSkillId      = 0x000E;

// Tunable debug constant: flat damage bonus per HP-change event.
// Set to 0 to effectively disable, >0 to see the hypothetical total.
constexpr int           kDebugFlatDamageBonus = 1;

struct UnitSkillSet
{
    void           *unit;                     // raw Unit*
    std::uint16_t   skills[kMaxSkillsPerUnit];
    std::uint8_t    numSkills;
};

static UnitSkillSet gUnitSkills[kMaxTrackedUnits];
static std::size_t  gNumUnitSkillSets = 0;

// Clear all per-map skill state (called on plugin load + MapBegin).
static void ResetAllSkillSets()
{
    gNumUnitSkillSets = 0;

    for (std::size_t i = 0; i < kMaxTrackedUnits; ++i)
    {
        gUnitSkills[i].unit      = nullptr;
        gUnitSkills[i].numSkills = 0;
        for (std::size_t j = 0; j < kMaxSkillsPerUnit; ++j)
            gUnitSkills[i].skills[j] = 0;
    }
}

static UnitSkillSet *FindSkillSet(void *unitRaw)
{
    if (unitRaw == nullptr)
        return nullptr;

    for (std::size_t i = 0; i < gNumUnitSkillSets; ++i)
    {
        if (gUnitSkills[i].unit == unitRaw)
            return &gUnitSkills[i];
    }
    return nullptr;
}

static UnitSkillSet *FindOrCreateSkillSet(void *unitRaw)
{
    if (unitRaw == nullptr)
        return nullptr;

    if (UnitSkillSet *set = FindSkillSet(unitRaw))
        return set;

    if (gNumUnitSkillSets >= kMaxTrackedUnits)
        return nullptr;

    UnitSkillSet &slot = gUnitSkills[gNumUnitSkillSets++];
    slot.unit      = unitRaw;
    slot.numSkills = 0;
    for (std::size_t j = 0; j < kMaxSkillsPerUnit; ++j)
        slot.skills[j] = 0;

    return &slot;
}

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
        return false;

    set.skills[set.numSkills++] = skillId;
    return true;
}

static bool UnitHasSkillInternal(void *unitRaw, std::uint16_t skillId)
{
    if (unitRaw == nullptr || skillId == 0)
        return false;

    UnitSkillSet *set = FindSkillSet(unitRaw);
    if (!set)
        return false;

    for (std::uint8_t i = 0; i < set->numSkills; ++i)
    {
        if (set->skills[i] == skillId)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------
// HP-change debug observer
// ---------------------------------------------------------------------
//
// Simple HP-change handler that *observes* damage and logs what a
// flat bonus would do. It does NOT write back to the unit yet.
//
// As a small step towards "real skills", this is now gated by the
// presence of kDebugSkillId on the damage source unit.

void HpChange_DebugFlatDamage(const HpChangeContext &ctx)
{
    // We only care about *damage* for this test.
    // Convention: amount > 0 = damage, amount < 0 = healing.
    const int baseAmount = ctx.core.amount;
    if (baseAmount <= 0)
        return;

    // Only apply this hypothetical bonus if the *source* unit has
    // the debug test skill.
    void *srcRaw = ctx.core.source.Raw();
    if (!UnitHasSkillInternal(srcRaw, kDebugSkillId))
        return;

    const int bonusAmount = kDebugFlatDamageBonus;
    if (bonusAmount == 0)
        return;

    const int totalAmount = baseAmount + bonusAmount;

    // Log cap so we don't spam the file to death on big maps.
    static int sLogCount = 0;
    if (sLogCount >= 128)
        return;

    ++sLogCount;

    Logf("[Skills::DebugFlatDamage] src=%p tgt=%p "
         "base=%d bonus=%d -> total=%d "
         "(gen=%u side=%s sideTurn=%u totalTurns=%u, n=%d)",
         ctx.core.source.Raw(),
         ctx.core.target.Raw(),
         baseAmount,
         bonusAmount,
         totalAmount,
         static_cast<unsigned>(ctx.map.generation),
         TurnSideToString(ctx.turn.side),
         static_cast<unsigned>(ctx.turn.sideTurnIndex),
         static_cast<unsigned>(ctx.map.totalTurns),
         sLogCount);
}

// Guard so InitDebugSkills() is idempotent even if called twice.
bool sRegistered = false;

// MapBegin handler: reset per-map skill state.
static void HandleMapBegin(const MapContext &ctx)
{
    (void)ctx;

    ResetAllSkillSets();

    Logf("Skills: ResetAllSkillSets for new map (gen=%u)",
         static_cast<unsigned>(ctx.generation));
}

} // anonymous namespace

// ---------------------------------------------------------------------
// Public entrypoints
// ---------------------------------------------------------------------

void InitDebugSkills()
{
    if (sRegistered)
        return;

    // Clear any stale state in case the plugin survives across maps.
    ResetAllSkillSets();

    bool ok = true;

    // Keep skill tables scoped per map.
    ok = ok && ::Fates::Engine::RegisterMapBeginHandler(&HandleMapBegin);

    // Register our HP-change listener with the engine bus.
    ok = ok && ::Fates::Engine::RegisterHpChangeHandler(
        &HpChange_DebugFlatDamage);

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

    UnitSkillSet *set = FindOrCreateSkillSet(unitRaw);
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
}

bool UnitHasSkill(void          *unitRaw,
                  std::uint16_t  skillId)
{
    return UnitHasSkillInternal(unitRaw, skillId);
}

// ---------------------------------------------------------------------
// Static bootstrap
// ---------------------------------------------------------------------
//
// This tiny struct ensures InitDebugSkills() runs automatically
// when the plugin is loaded, without you having to call it from
// main.cpp or anywhere else.
//

struct SkillsBootstrap
{
    SkillsBootstrap()
    {
        InitDebugSkills();
    }
};

static SkillsBootstrap sSkillsBootstrap;

} // namespace Skills
} // namespace Engine
} // namespace Fates
