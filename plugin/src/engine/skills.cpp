// engine/skills.cpp
//
// First-pass skill engine wiring.
//
// This module now does three things:
//
//   1) Maintains a lightweight per-map skill table based on
//      UNIT_SkillLearn (fed from hooks_handlers.cpp). Other engine
//      modules can query this via Skills::UnitHasSkill().
//
//   2) Provides a debug-only “flat damage increase” observer on the
//      HP-change event bus. This remains read-only; we just log what
//      the damage *would* look like with a bonus so we can validate
//      shapes and ordering safely.
//
//   3) Registers a *real* final-damage modifier with the combat
//      pipeline. This runs at the same stage as vanilla skills
//      (BTL_FinalDamage_Pre), so its effect appears in the combat
//      forecast and HP loss exactly like a normal skill would.

#include "engine/skills.hpp"
#include "engine/bus.hpp"
#include "engine/events.hpp"
#include "util/debug_log.hpp"
#include "core/runtime.hpp"  // TurnSideToString
#include "engine/combat.hpp"

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

constexpr std::size_t kMaxTrackedUnits  = 256;
constexpr std::size_t kMaxSkillsPerUnit = 8;

// Tunable debug constant: flat damage bonus.
// Used both by the HP-change logger (hypothetical) and the real
// final-damage modifier. Set to 0 to effectively disable, >0 to
// see the effect in logs / forecast.
constexpr int kDebugFlatDamageBonus = 1;

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

// Simple helper: “is this unit present in our per-map skill table at all?”
static bool IsTrackedUnit(void *unitRaw)
{
    if (unitRaw == nullptr)
        return false;

    return FindSkillSet(unitRaw) != nullptr;
}

// ---------------------------------------------------------------------
// HP-change debug observer (read-only) with probes
// ---------------------------------------------------------------------
//
// Simple HP-change handler that *observes* damage and logs what a
// flat bonus would do. It does NOT write back to the unit.
//
// For now the hypothetical bonus is gated only on the acting side
// (player-side / Side1) so tests are stable and do not depend on
// any particular skill ID being learned.

void HpChange_DebugFlatDamage(const HpChangeContext &ctx)
{
    // PROBE: log the first 64 HP events so we can see what pointers,
    // sides and tracking flags we're actually getting.
    static int sProbeCount = 0;
    if (sProbeCount < 64)
    {
        void *src = ctx.core.source.Raw();
        void *tgt = ctx.core.target.Raw();

        bool srcTracked = IsTrackedUnit(src);
        bool tgtTracked = IsTrackedUnit(tgt);

        Logf("[Skills::HpProbe] amt=%d src=%p tgt=%p "
             "srcTracked=%d tgtTracked=%d side=%s",
             ctx.core.amount,
             src,
             tgt,
             srcTracked ? 1 : 0,
             tgtTracked ? 1 : 0,
             TurnSideToString(ctx.turn.side));

        ++sProbeCount;
    }

    // Be conservative about sign: treat *any* non-zero as "interesting".
    const int baseAmount = ctx.core.amount;
    if (baseAmount == 0)
        return;

    // New stable gating: only consider player-side HP changes.
    // This keeps tests simple and avoids depending on any specific
    // skill ID being learned.
    if (ctx.turn.side != TurnSide::Side1)
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

    Logf("[Skills::DebugFlatDamage] (HP) base=%d bonus=%d -> total=%d "
         "(gen=%u side=%s sideTurn=%u totalTurns=%u, n=%d)",
         baseAmount,
         bonusAmount,
         totalAmount,
         static_cast<unsigned>(ctx.map.generation),
         TurnSideToString(ctx.turn.side),
         static_cast<unsigned>(ctx.turn.sideTurnIndex),
         static_cast<unsigned>(ctx.map.totalTurns),
         sLogCount);
}


// ---------------------------------------------------------------------
// Final damage modifier: real "+damage" test
// ---------------------------------------------------------------------
//
// This runs in the final damage pipeline (Combat::ApplyDamageModifiers),
// which is called from BTL_FinalDamage_Pre. It *actually* changes the
// number vanilla uses, so it affects both the forecast window and the
// HP loss.
//
// For now it's a flat bonus on player-side attacks (Side1).
static int Damage_DebugFlatBonus(
    const ::Fates::Engine::Combat::DamageContext &ctx,
    int                                           currentDamage)
{
    void *srcRaw = ctx.attacker.Raw();

    // PROBE: log first 32 damage-modifier calls.
    static int sProbeCount = 0;
    if (sProbeCount < 32)
    {
        bool tracked = IsTrackedUnit(srcRaw);

        Logf("[Skills::DmgProbe] atk=%p base=%d cur=%d "
             "srcTracked=%d side=%s",
             srcRaw,
             ctx.baseDamage,
             currentDamage,
             tracked ? 1 : 0,
             TurnSideToString(ctx.turn.side));

        ++sProbeCount;
    }

    if (!srcRaw)
        return currentDamage;

    // New stable gating: only apply the bonus for player-side attacks.
    if (ctx.turn.side != TurnSide::Side1)
        return currentDamage;

    const int bonus = kDebugFlatDamageBonus;
    if (bonus == 0)
        return currentDamage;

    int newDamage = currentDamage + bonus;

    // Small capped log so you can prove it's firing without
    // spamming the entire run.
    static int sLogCount = 0;
    if (sLogCount < 64)
    {
        Logf("[Skills::DebugFlatDamage/Final] atk=%p base=%d cur=%d "
             "bonus=%d -> new=%d "
             "(gen=%u side=%s sideTurn=%u totalTurns=%u, n=%d)",
             srcRaw,
             ctx.baseDamage,
             currentDamage,
             bonus,
             newDamage,
             static_cast<unsigned>(ctx.map.generation),
             TurnSideToString(ctx.turn.side),
             static_cast<unsigned>(ctx.turn.sideTurnIndex),
             static_cast<unsigned>(ctx.map.totalTurns),
             sLogCount + 1);
        ++sLogCount;
    }

    return newDamage;
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

static void DebugDumpSkillSets()
{
    Logf("[Skills::DebugDump] gNumUnitSkillSets=%u",
         static_cast<unsigned>(gNumUnitSkillSets));
    for (std::size_t i = 0; i < gNumUnitSkillSets; ++i)
    {
        UnitSkillSet &set = gUnitSkills[i];
        Logf("[Skills::DebugDump] %02u: unit=%p num=%u "
             "s[0..7]={%04X,%04X,%04X,%04X,%04X,%04X,%04X,%04X}",
             static_cast<unsigned>(i),
             set.unit,
             static_cast<unsigned>(set.numSkills),
             set.skills[0], set.skills[1], set.skills[2], set.skills[3],
             set.skills[4], set.skills[5], set.skills[6], set.skills[7]);
    }
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

    // Register our HP-change listener with the engine bus (logging only).
    ok = ok && ::Fates::Engine::RegisterHpChangeHandler(
        &HpChange_DebugFlatDamage);

    // Register our *real* final-damage modifier so we can see a tiny,
    // deterministic change in the forecast for player-side attacks.
    ok = ok && ::Fates::Engine::Combat::RegisterDamageModifier(
        &Damage_DebugFlatBonus);

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

    static bool sDumped = false;
    if (!sDumped)
    {
        DebugDumpSkillSets();
        sDumped = true;
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
