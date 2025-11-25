// hooks_handlers.cpp
//
// Implements the C stub handlers for each hook declared in
// core/handlers.hpp. Each stub currently:
//
// NOTE: Ongoing RE
//
//   1) Increments its entry in Fates::gHookCount for telemetry (most hooks).
//   2) Optionally logs to fates_3gx.log.
//
//

#include <CTRPluginFramework.hpp>
#include <cstddef>   // for std::size_t
#include <cstdint>

#include "core/hooks.hpp"
#include "core/runtime.hpp"
#include "core/handlers.hpp"
#include "util/debug_log.hpp"
#include "hook_debug.hpp"   // DumpHookCountsToFile / DumpKillEventsToLog
#include "engine/events.hpp"
#include "engine/skills.hpp"   // NEW: bridge into skill engine

using namespace CTRPluginFramework;

namespace Fates
{
    // Forward-declare Unit so we can point at it from BattleRoot.
    struct Unit;

    // This models the "root" struct observed via BTL_FinalDamage_Pre logging.
    // Layout is based on my personal logs; I only *use* a few fields for now.
    struct BattleRoot
    {
        u32   pad0;      // 0x00, usually 0
        Unit *mainUnit;  // 0x04, matches UNIT_UpdateCloneHP src pointer
        u32   unk08;     // 0x08
        u32   unk0C;     // 0x0C
        u32   flags;     // 0x10, 0x4000xxxx / 0x4001xxxx patterns
        int   unk14;     // 0x14, often -1 / 0 / 1
        u32   unk18;     // 0x18, small ints (hit/slot-style values)
        u32   unk1C;     // 0x1C, small ints
    };

    // Minimal view of BattleCalculator: only care that [0] is BattleRoot*.
    struct BattleCalculator
    {
        BattleRoot *root;  // 0x00
        // Remaining fields currently unknown / unused.
    };

    // Convenience helper to safely peel the BattleRoot* off a raw calc ptr.
    static inline BattleRoot *GetBattleRoot(void *calcRaw)
    {
        if (calcRaw == nullptr)
            return nullptr;

        auto *calc = reinterpret_cast<BattleCalculator *>(calcRaw);
        return calc->root;
    }

    // Convenience: index into gHookCount from a HookId.
    static inline std::size_t IndexOf(HookId id)
    {
        return static_cast<std::size_t>(id);
    }

    // -----------------------------------------------------------------
    // Global turn-side helper (Player / Enemy / Other)
    // -----------------------------------------------------------------

    // Base VA for the branch/turn-state pointer chain discovered earlier.
    static constexpr std::uintptr_t kTurnBranchStateVA = 0x003A4944;
	
	// Approximate battle/map heap range, based on observed pointers like
    // 0x32626E90, 0x328A3DD0, 0x3291F9C0, etc. Use this as a guard
    // before dereferencing pointers from hooks.
    static constexpr std::uintptr_t kHeapMinVA = 0x32000000;
    static constexpr std::uintptr_t kHeapMaxVA = 0x33FFFFFF;

    // Raw helper: returns 0..3 on success, 0xFF on error/unknown.
    static inline std::uint8_t GetTurnSideIndexRaw()
    {
        // Step 1: r1 = *(u32*)0x003A4944;
        auto ptr1 = *reinterpret_cast<std::uintptr_t const *>(kTurnBranchStateVA);
        if (ptr1 == 0)
            return 0xFF;

        // Step 2: r2 = *(u32*)(ptr1 + 0);
        auto ptr2 = *reinterpret_cast<std::uintptr_t const *>(ptr1);
        if (ptr2 == 0)
            return 0xFF;

        auto base = reinterpret_cast<std::uint8_t const *>(ptr2);

        std::uint8_t idx  = base[0x08];
        std::uint8_t side = base[idx];

        return (side <= 3) ? side : 0xFF;
    }

// Called when detect a NEW map root in Hook_SEQ_MapStart.
static inline void MapLife_OnNewMap(void *seq, TurnSide side)
{
    gMapState.seqRoot = seq;

    // New map => bump generation counter.
    ++gMapState.generation;

    gMapState.startSide   = side;
    gMapState.currentSide = side;

    gMapState.totalTurns = 0;
    for (int i = 0; i < 4; ++i)
        gMapState.turnCount[i] = 0;

    gMapState.killEvents = 0;
    gMapState.mapActive  = true;

    // Treat kill buffer + stats as per-map.
    ResetKillEvents();
    ResetMapStats();

    // NOTE:
    // We intentionally *do not* reset the legacy debug-skill table here.
    // Units that were given the debug skill (0x000E) during data
    // load *before* the first map should still be visible in RE logs.
    //
    // The *canonical* per-map skill view lives in engine/skills.cpp
    // (Skills::InitDebugSkills + Skills::OnUnitSkillLearnRaw).
    // This legacy table is RE-only scaffolding and may be removed
    // in a future cleanup.
    // DebugSkills_Reset();
}

    // Called by Hook_SEQ_TurnBegin.
    static inline void MapLife_OnTurnBegin(TurnSide side)
    {
        gMapState.currentSide = side;

        ++gMapState.totalTurns;

        int idx = static_cast<int>(side);
        if (0 <= idx && idx <= 3)
            ++gMapState.turnCount[idx];
    }

    // Called when the map fully ends (MapEnd).
    static inline void MapLife_OnMapEnd()
    {
        gMapState.mapActive = false;
    }


    // Enum wrapper: convert raw 0..3 into TurnSide.
    static inline TurnSide GetTurnSideEnum()
    {
        std::uint8_t raw = GetTurnSideIndexRaw();
        switch (raw)
        {
        case 0: return TurnSide::Side0;
        case 1: return TurnSide::Side1;
        case 2: return TurnSide::Side2;
        case 3: return TurnSide::Side3;
        default: return TurnSide::Unknown;
        }
    }
	
	struct LevelUpPayload
    {
        Unit           *unit;   // main unit pointer
        std::uint8_t    level;  // unit's new level after the ding
        std::uint8_t    _pad[3]; // reserved for future (class id, flags, etc.)
    };

    struct SkillLearnPayload
    {
        Unit           *unit;     // learner
        std::uint16_t   skillId;  // learned skill
        std::uint16_t   flags;    // reserved (source: level, scroll, script, etc.)
    };
	
    // -----------------------------------------------------------------
    // LEGACY debug-skill tracking (per-map, Unit* -> "has debug skill").
    //
    // NOTE: This table is only used for extra logging in
    // Hook_BTL_FinalDamage_Pre and Hook_UNIT_SkillLearn. The *canonical*
    // path for skills at the SDK layer is engine/skills.cpp +
    // the event bus (Engine::Skills::InitDebugSkills, HpChange handlers).
    //
    // Left in place for ongoing RE; safe to remove once the skill engine
    // fully takes over your experiments.
    // -----------------------------------------------------------------

    // Hard-coded debug skill ID for now; messy and serves only for testing
    static constexpr std::uint16_t kDebugSkillId  = 0x000E;
    static constexpr std::size_t   kDebugMaxUnits = 256;

    struct DebugSkillEntry
    {
        Unit *unit;
        bool  hasSkill;
    };

    static DebugSkillEntry gDebugSkillTable[kDebugMaxUnits];
    static std::size_t     gDebugSkillCount = 0;

    // Clear all debug-skill state (called on new map).
    static inline void DebugSkills_Reset()
    {
        gDebugSkillCount = 0;
        for (std::size_t i = 0; i < kDebugMaxUnits; ++i)
        {
            gDebugSkillTable[i].unit     = nullptr;
            gDebugSkillTable[i].hasSkill = false;
        }
    }

    // Record that a unit has learned debug skill.
    static inline void DebugSkills_OnSkillLearn(Unit *unit,
                                                std::uint16_t skillId)
    {
        if (unit == nullptr)
            return;

        if (skillId != kDebugSkillId)
            return;

        // Look for an existing entry first.
        for (std::size_t i = 0; i < gDebugSkillCount; ++i)
        {
            if (gDebugSkillTable[i].unit == unit)
            {
                gDebugSkillTable[i].hasSkill = true;
                return;
            }
        }

        // Otherwise append a new one, if we have room.
        if (gDebugSkillCount < kDebugMaxUnits)
        {
            gDebugSkillTable[gDebugSkillCount].unit     = unit;
            gDebugSkillTable[gDebugSkillCount].hasSkill = true;
            ++gDebugSkillCount;
        }
    }

    // Does this unit have debug skill?
    static inline bool DebugSkills_Has(Unit *unit)
    {
        if (unit == nullptr)
            return false;

        for (std::size_t i = 0; i < gDebugSkillCount; ++i)
        {
            if (gDebugSkillTable[i].unit == unit)
                return gDebugSkillTable[i].hasSkill;
        }
        return false;
    }
	    struct UnitCommandEvent
    {
        void *vtable;       // [0x00]
        void *unk04;        // [0x04]
        void *unk08;        // [0x08]
        void *updateFunc;   // [0x0C] -> 0x00354704 (ProcSequence__UnitMove)
        void *seqMap;       // [0x10] -> matches SEQ_MapStart seq
        void *unk14;        // [0x14]
        void *unk18;        // [0x18]
        void *cmdData;      // [0x1C] -> likely command data/context
        std::uint32_t cmdId;// [0x20] -> 0x0C in attack test (prob. command type)
        std::uint32_t side; // [0x24] -> 1 = Side1 (player)
        std::uint32_t unk28;// [0x28] -> 6 
        void *unk2C;        // [0x2C]
        void *unk30;        // [0x30]
        void *unk34;        // [0x34]
        void *unk38;        // [0x38]
        void *unk3C;        // [0x3C]
    };

} // namespace Fates


// ---------------------------------------------------------------------
// Internal state for post-battle HP experiments
// ---------------------------------------------------------------------

// Tracks the most recent battle root observed in BTL_FinalDamage_Pre.
static Fates::BattleRoot *sLastBattleRoot = nullptr;

// Debug knobs for the HP overlay.
//
// Leave this disabled (-1) for normal play. 
// Mostly a leftover from earlier tests, needs to be cleaned up.
static constexpr int kDebugTestSlotIndex = -1;  // 0..3 to target a slot, -1 = off
static constexpr int kDebugHpDelta       = 1;   // subtract 1 HP when enabled
static constexpr int kDebugMaxMods       = 16;  // safety cap

// Forward decl (defined later).
extern "C" int Hook_SEQ_HpDamage_Helper(void *a0,
                               void *a1,
                               void *a2,
                               void *a3);

extern "C" {

// ---------------------------------------------------------------------
// Battle math hooks
// ---------------------------------------------------------------------

int Hook_BTL_HitCalc_Main(int hitRate)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    // Telemetry: track how often the hit RNG is called.
    std::size_t idx = IndexOf(HookId_BTL_HitCalc_Main);
    gHookCount[idx]++;

    // Call the original RandomCalculateHit(int).
    HookContext &ctx = HookContext::GetCurrent();
    int result = ctx.OriginalFunction<int, int>(hitRate);
	
	 // engine-level hit summary (map/turn aware).
    Engine::OnHitCalc(hitRate, result);

    // Light logging window 
    static int sLogCount = 0;
    if (sLogCount < 64)
    {
        Logf("Hook_BTL_HitCalc_Main(RandomCalculateHit): rate=%d -> %d (n=%d)",
             hitRate, result, sLogCount + 1);
        ++sLogCount;
    }

    return result;
}

std::uint32_t Hook_SYS_Rng32(void *rngState,
                             std::uint32_t upperBound)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    // Telemetry: track how often the global RNG is called.
    std::size_t idx = IndexOf(HookId_SYS_Rng32);
    gHookCount[idx]++;

    // Local toggle: set to true temporarily if you ever want RNG log spam.
    static bool sRngDebug = false;

    HookContext &ctx = HookContext::GetCurrent();
    (void)ctx;  // currently unused, but kept for consistency.

    using CoreFn = std::uint32_t (*)(void *state);

    // Core RNG-step function 
    CoreFn core = reinterpret_cast<CoreFn>(0x0044AE14);

    // Step the RNG state and get the raw 31-bit value.
    std::uint32_t raw = core(rngState);

    // Final value to return to the game.
    std::uint32_t result = 0u;

    if (upperBound != 0u)
    {
        // Replicate the engine's scaling:
        // high 32 bits of (raw * upperBound)
        std::uint64_t product =
            static_cast<std::uint64_t>(raw) *
            static_cast<std::uint64_t>(upperBound);

        result = static_cast<std::uint32_t>(product >> 32);
    }

    // Optional: only log if sRngDebug is enabled.
    static int sLogCount = 0;
    if (sRngDebug && sLogCount < 32)
    {
        Logf("Hook_SYS_Rng32: state=%p raw=%08X bound=%u -> %u (n=%d)",
             rngState,
             raw,
             upperBound,
             result,
             sLogCount + 1);
        ++sLogCount;
    }

    // Engine-level summary (map/turn-aware).
    Engine::OnRngCall(rngState, raw, upperBound, result);

    return result;
}

// Do not use, this is unfunctional and will be revisited later. 

int Hook_BTL_CritCalc_Main(void * unit,
                           int   indexOrFlag)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    // Telemetry
    std::size_t idx = IndexOf(HookId_BTL_CritCalc_Main);
    gHookCount[idx]++;

    HookContext &ctx = HookContext::GetCurrent();

    // Call the original Unit__GetCritical.
    int crit = ctx.OriginalFunction<int, void *, int>(unit, indexOrFlag);

    // Light logging 
    static int sLogCount = 0;
    if (sLogCount < 64)
    {
        Logf("Hook_BTL_CritCalc_Main(Unit__GetCritical): unit=%p idx=%d -> crit=%d (n=%d)",
             unit,
             indexOrFlag,
             crit,
             sLogCount + 1);
        ++sLogCount;
    }

    return crit;
}


void Hook_BTL_FinalDamage_Pre(void *calcRaw,
                              void *arg1,
                              void *arg2,
                              void *arg3)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    // Count invocations for telemetry.
    std::size_t idx = IndexOf(HookId_BTL_FinalDamage_Pre);
    gHookCount[idx]++;

    // Peel the BattleRoot off the calculator and remember it for the
    // upcoming SEQ_Battle_UpdateHp / SEQ_HpDamage pass.
    BattleRoot *root = GetBattleRoot(calcRaw);
    sLastBattleRoot  = root;

    // Only do deep logging for the first few calls so log will be readable.
    static int sLogCount = 0;
    if (sLogCount < 16)
    {
        Logf("Hook_BTL_FinalDamage_Pre: calc=%p root=%p arg1=%p arg2=%p arg3=%p (n=%d)",
             calcRaw, root, arg1, arg2, arg3, sLogCount + 1);

        if (root != nullptr)
        {
            u32 *w = reinterpret_cast<u32 *>(root);

            Logf("  root[0x00..0x3C] = "
                 "{%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,"
                 "%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X}",
                 w[0],  w[1],  w[2],  w[3],
                 w[4],  w[5],  w[6],  w[7],
                 w[8],  w[9],  w[10], w[11],
                 w[12], w[13], w[14], w[15]);

            Unit *mainUnit = root->mainUnit;
            u32   flags    = root->flags;
            int   unk14    = root->unk14;
            u32   unk18    = root->unk18;
            u32   unk1C    = root->unk1C;

            Logf("  root view: main=%p flags=%08X unk14=%d unk18=%u unk1C=%u",
                 mainUnit, flags, unk14, unk18, unk1C);

            // NEW: see whether this main unit is marked as having the
            // debug skill 0x000E for this map. (see above on for debug skill info)
            if (DebugSkills_Has(mainUnit))
            {
                Logf("  [DebugSkill] main unit %p has debug skill 0x%04X (BTL_FinalDamage_Pre)",
                     mainUnit,
                     static_cast<unsigned>(kDebugSkillId));
            }
        }

        ++sLogCount;
    }

    // Pure MITM pass-through for now.
    HookContext &ctx = HookContext::GetCurrent();
    ctx.OriginalFunction<void, void *, void *, void *, void *>(
        calcRaw, arg1, arg2, arg3);
}

// Depricated, do not rely on or use, left only as a named concept.

void Hook_BTL_FinalDamage_Post(void *battleContext,
                               void *attacker,
                               void *defender)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_BTL_FinalDamage_Post);
    gHookCount[idx]++;

    static int sLogCount = 0;
    if (sLogCount < 64)
    {
        Logf("Hook_BTL_FinalDamage_Post: ctx=%p atk=%p def=%p (n=%d)",
             battleContext, attacker, defender, sLogCount + 1);
        ++sLogCount;
    }

    HookContext &ctx = HookContext::GetCurrent();
    ctx.OriginalFunction<void, void *, void *, void *>(
        battleContext, attacker, defender);
}

// Not functional, will be revisited later, reserved for now.

void Hook_BTL_GuardGauge_Add(void * battleContext,
                             void * attacker,
                             void * defender)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_BTL_GuardGauge_Add);
    gHookCount[idx]++;

    HookContext &ctx = HookContext::GetCurrent();
    ctx.OriginalFunction<void, void *, void *, void *>(
        battleContext, attacker, defender);
}

// Not functional, will be revisited later, reserved for now.

void Hook_BTL_GuardGauge_Spend(void * battleContext,
                               void * attacker,
                               void * defender)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_BTL_GuardGauge_Spend);
    gHookCount[idx]++;

    HookContext &ctx = HookContext::GetCurrent();
    ctx.OriginalFunction<void, void *, void *, void *>(
        battleContext, attacker, defender);
}

// ---------------------------------------------------------------------
// HP and map damage hooks
// ---------------------------------------------------------------------

// Apply testing post-debug adjustment (currently a no-op because
// kDebugTestSlotIndex == -1). This is RE scaffolding, not part of the
// public SDK surface.
static std::uint32_t ApplyPostBattleHpDebug(void *seq,
                                            int   mode,
                                            int   slot,
                                            std::uint32_t hp)
{
    //
    // 1) Future: real post-battle HP effects (currently a no-op)
    //
    if (mode == 0 && sLastBattleRoot != nullptr)
    {
        // TODO: inspect sLastBattleRoot and apply
        // real post-battle auras / poison / regen etc here 
    }

    //
    // 2) Optional debug overlay ("subtract kDebugHpDelta from slot X") Revisit to see if this needs to removed.
    //

    if (mode != 0)
        return hp;

    // Debug disabled unless kDebugTestSlotIndex is in [0, 3].
    if (kDebugTestSlotIndex < 0 || kDebugTestSlotIndex > 3)
        return hp;

    // Only affect the chosen slot (0–3).
    if (slot != kDebugTestSlotIndex)
        return hp;

    static int sModCount = 0;
    if (sModCount >= kDebugMaxMods)
        return hp;

    // Don’t bother if the unit is already at 0.
    if (hp == 0)
        return 0;

    const std::uint32_t delta = static_cast<std::uint32_t>(kDebugHpDelta);
    std::uint32_t oldHp = hp;
    std::uint32_t newHp = (hp > delta) ? (hp - delta) : 0u;

    if (newHp != oldHp)
    {
        ++sModCount;

        Logf("    [MOD] slot=%d oldHp=%u newHp=%u (mode=%d root=%p main=%p)",
             slot,
             oldHp,
             newHp,
             mode,
             sLastBattleRoot,
             sLastBattleRoot ? sLastBattleRoot->mainUnit : nullptr);
    }

    return newHp;
}

void Hook_SEQ_HpDamage(void *seq,
                       int   mode)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_SEQ_HpDamage);
    gHookCount[idx]++;

    static int sLogCount = 0;

    auto *self = reinterpret_cast<std::uint8_t *>(seq);

    auto **resultBasePtr =
        reinterpret_cast<std::uint8_t **>(self + 0x254);
    std::uint8_t *resultBase =
        (resultBasePtr != nullptr) ? *resultBasePtr : nullptr;

    if (resultBase != nullptr)
    {
        // Optional logging of the header, gated by the HP debug toggle.
		// Need to phase out *all* current hotkey toggles, this included.
        if (gHpApplyLogEnabled && sLogCount < 64)
        {
            Logf("Hook_SEQ_HpDamage/UpdateHp: seq=%p mode=%d (hit=%d)",
                 seq, mode, sLogCount + 1);
            Logf("  resultBase=%p", resultBase);
        }

        for (int slot = 0; slot < 4; ++slot)
        {
            auto *hpWordPtr =
                reinterpret_cast<std::uint32_t *>(
                    resultBase + 0x20 + slot * 4);
            std::uint32_t hpWord = *hpWordPtr;

            // Always apply any post-battle HP adjustment logic here.
            std::uint32_t newHp =
                ApplyPostBattleHpDebug(seq, mode, slot, hpWord);

            if (newHp != hpWord)
            {
                *hpWordPtr = newHp;
                hpWord     = newHp;
            }

            // Per-slot logging, gated by HP debug toggle.
			// Phase out hotkey toggle!!
            if (gHpApplyLogEnabled && sLogCount < 64)
            {
                Logf("    slot=%d hpWord=%08X (%u) @%p",
                     slot,
                     hpWord,
                     hpWord,
                     hpWordPtr);
            }
        }

        if (gHpApplyLogEnabled && sLogCount < 64)
            ++sLogCount;
    }

    HookContext &ctx = HookContext::GetCurrent();
    ctx.OriginalFunction<void, void *, int>(seq, mode);
}

void Hook_UNIT_UpdateCloneHP(void *unit)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_UNIT_UpdateCloneHP);
    gHookCount[idx]++;

    // First, run the real implementation so HP actually gets copied.
    HookContext &ctx = HookContext::GetCurrent();
    ctx.OriginalFunction<void, void *>(unit);

    if (unit != nullptr)
    {
        u32 base = reinterpret_cast<u32>(unit);

        // Source HP: signed 8-bit value at +0xF3.
        s8  srcHp    = *reinterpret_cast<signed char *>(base + 0xF3);
        int srcHpInt = static_cast<int>(srcHp);

        // Engine-level: treat this as “unit HP has just been synced”.
        // This is now the canonical driver for HpChange events.
        Engine::OnUnitHpSync(unit, srcHpInt);

        // Clone pointer lives at +0xAC. You will most likely never touch this.
        void *clone = *reinterpret_cast<void * *>(base + 0xAC);

        int cloneHpInt = -1;
        if (clone != nullptr)
        {
            u32 cloneBase = reinterpret_cast<u32>(clone);
            s8  cloneHp   = *reinterpret_cast<signed char *>(cloneBase + 0xF3);
            cloneHpInt    = static_cast<int>(cloneHp);
        }

        // Keep the lightweight debug log, but gate it behind HP toggle.
        static int sLogCount = 0;
        if (gHpApplyLogEnabled && sLogCount < 64)
        {
            Logf("UNIT_UpdateCloneHP: src=%p hp=%d clone=%p hpClone=%d (n=%d)",
                 unit,
                 srcHpInt,
                 clone,
                 cloneHpInt,
                 sLogCount + 1);
            ++sLogCount;
        }
    }
}

int Hook_UNIT_HpDamage(void *a0,
                       void *a1,
                       void *a2,
                       void *a3)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_UNIT_HpDamage);
    gHookCount[idx]++;
    unsigned total = static_cast<unsigned>(gHookCount[idx]);

    int unitIndex = static_cast<int>(reinterpret_cast<std::intptr_t>(a1));
    int dmgAmount = static_cast<int>(reinterpret_cast<std::intptr_t>(a2));

    static int sLogCount = 0;
    if (gHpApplyLogEnabled && sLogCount < 64)
    {
        Logf("Hook_UNIT_HpDamage: total=%u idx=%d dmg=%d a0=%p a3=%p (n=%d)",
             total, unitIndex, dmgAmount, a0, a3, sLogCount + 1);
        ++sLogCount;
    }

    HookContext &ctx = HookContext::GetCurrent();
    int result = ctx.OriginalFunction<int, void *, void *, void *, void *>(
        a0, a1, a2, a3);

    return result;
}

void Hook_HP_KillCheck(void *calc,
                       void *contextOrFlags)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    // Count how many times this hook fires.
    std::size_t idx = IndexOf(HookId_HP_KillCheck);
    gHookCount[idx]++;

    // Run the real ProcSequence::DeadEvent first so that all of its
    // side-effects are committed before the sequence is inspected.
    HookContext &ctx = HookContext::GetCurrent();
    ctx.OriginalFunction<void, void *, void *>(calc, contextOrFlags);

    if (calc == nullptr)
        return;

    // Extract Logging fields:
    //   +0x280 = flags bitfield
    //   +0x284 = dead slot 0 (pointer or nullptr)
    //   +0x288 = dead slot 1 (pointer or nullptr)
    u32 base          = reinterpret_cast<u32>(calc);
    unsigned int flags = *reinterpret_cast<unsigned int *>(base + 0x280);
    void *dead0        = *reinterpret_cast<void * *>(base + 0x284);
    void *dead1        = *reinterpret_cast<void * *>(base + 0x288);

    // Only treat this as a "real" kill event if there is actually
    // something meaningful: non-zero flags or at least one dead slot.
    if (flags != 0u || dead0 != nullptr || dead1 != nullptr)
    {
        KillEvent ev;
        ev.seq   = calc;
        ev.dead0 = dead0;
        ev.dead1 = dead1;
        ev.flags = flags;

        bool pushed = PushKillEvent(ev);

        // Per-map stats: only count kills while a map is active.
        if (gMapState.mapActive)
        {
            ++gMapStats.totalKills;

            // Map TurnSide -> stats index 0..3.
            int sideIndex = -1;
            switch (gCurrentTurnSide)
            {
            case TurnSide::Side0: sideIndex = 0; break;
            case TurnSide::Side1: sideIndex = 1; break;
            case TurnSide::Side2: sideIndex = 2; break;
            case TurnSide::Side3: sideIndex = 3; break;
            default: break;
            }

            if (sideIndex >= 0 && sideIndex < 4)
                ++gMapStats.killsBySide[sideIndex];
        }

        // Let the engine know about the kill. Use the current turn
        // side, which Hook_SEQ_TurnBegin keeps in sync.
        TurnSide side = gCurrentTurnSide;
        Engine::OnKill(ev, side);

        // Light logging windows
        static int sLogCount = 0;
        if (sLogCount < 64)
        {
            Logf("Hook_HP_KillCheck: seq=%p flags=0x%08X dead0=%p dead1=%p "
                 "ctx=%p pushed=%d (eventIdx=%d, mapGen=%u mapKills=%u, "
                 "totalKills=%u [S0=%u S1=%u S2=%u S3=%u], n=%d)",
                 calc,
                 flags,
                 dead0,
                 dead1,
                 contextOrFlags,
                 pushed ? 1 : 0,
                 pushed ? (gKillEventCount - 1) : -1,
                 static_cast<unsigned>(gMapState.generation),
                 static_cast<unsigned>(gMapState.killEvents),
                 gMapStats.totalKills,
                 gMapStats.killsBySide[0],
                 gMapStats.killsBySide[1],
                 gMapStats.killsBySide[2],
                 gMapStats.killsBySide[3],
                 sLogCount + 1);
            ++sLogCount;
        }
    }
}

int Hook_SEQ_HpDamage_Helper(void *a0,
                             void *a1,
                             void *a2,
                             void *a3)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    // Count how many times this hook fires.
    std::size_t idx = IndexOf(HookId_SEQ_HpDamage_Helper);
    gHookCount[idx]++;
    unsigned total = static_cast<unsigned>(gHookCount[idx]);

    // Third argument is the raw heal amount (positive int) passed in a2.
    int healAmount = static_cast<int>(reinterpret_cast<std::intptr_t>(a2));

    static int sLogCount = 0;
    if (gHpApplyLogEnabled && sLogCount < 64)
    {
        Logf("Hook_SEQ_HpDamageHelper: total=%u heal=%d a0=%p a1=%p a3=%p (n=%d)",
             total,
             healAmount,
             a0,
             a1,
             a3,
             sLogCount + 1);
        ++sLogCount;
    }

    // IMPORTANT: no modification here. Just observe and forward. (Does this need to removed? Future me revisit!!)
    HookContext &ctx = HookContext::GetCurrent();
    int result = ctx.OriginalFunction<int, void *, void *, void *, void *>(
        a0,  // SequenceHelper* / context
        a1,  // Unit*
        a2,  // original heal amount (positive)
        a3   // flags / mode
    );

    // Canonical HP-change events are now derived from
    // UNIT_UpdateCloneHP via Engine::OnUnitHpSync; do not re-introduce
    // direct Engine::OnHpChange calls from this helper.
    return result;
}


int Hook_SEQ_ItemGain(void *seqHelper,
                      void *unit,
                      void *itemArg,
                      void *modeOrCtx)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    // Count how many times this hook fires.
    std::size_t idx = IndexOf(HookId_SEQ_ItemGain);
    gHookCount[idx]++;
    unsigned total = static_cast<unsigned>(gHookCount[idx]);

    static int sLogCount = 0;
    if (sLogCount < 64)
    {
        Logf("Hook_SEQ_ItemGain: total=%u seq=%p unit=%p itemArg=%p mode=%p (n=%d)",
             total,
             seqHelper,
             unit,
             itemArg,
             modeOrCtx,
             sLogCount + 1);
        ++sLogCount;
    }

    HookContext &ctx = HookContext::GetCurrent();
    int result = ctx.OriginalFunction<int, void *, void *, void *, void *>(
        seqHelper, unit, itemArg, modeOrCtx);

    // Engine notification (map/turn aware).
    Engine::OnItemGain(seqHelper,
                       unit,
                       itemArg,
                       modeOrCtx,
                       result,
                       gCurrentTurnSide);

    return result;
}


void Hook_MAP_ProcSkillDamage(void *seq)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_MAP_ProcSkillDamage);
    gHookCount[idx]++;

    static int sLogCount = 0;
    if (sLogCount < 64)
    {
        Logf("Hook_MAP_ProcSkillDamage (TerrainHeal): seq=%p (n=%d)",
             seq,
             sLogCount + 1);
        ++sLogCount;
    }

    HookContext &ctx = HookContext::GetCurrent();
    ctx.OriginalFunction<void, void *>(seq);
}

void Hook_MAP_ProcTerrainDamage(void *seq)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_MAP_ProcTerrainDamage);
    gHookCount[idx]++;

    static int sLogCount = 0;
    if (sLogCount < 64)
    {
        Logf("Hook_MAP_ProcTerrainDamage (TrickStatueHeal): seq=%p (n=%d)",
             seq,
             sLogCount + 1);
        ++sLogCount;
    }

    HookContext &ctx = HookContext::GetCurrent();
    ctx.OriginalFunction<void, void *>(seq);
}

void Hook_MAP_ProcTrickDamage(void *seq)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_MAP_ProcTrickDamage);
    gHookCount[idx]++;

    static int sLogCount = 0;
    if (sLogCount < 64)
    {
        Logf("Hook_MAP_ProcTrickDamage (SkillCannonEffect): seq=%p (n=%d)",
             seq,
             sLogCount + 1);
        ++sLogCount;
    }

    HookContext &ctx = HookContext::GetCurrent();
    ctx.OriginalFunction<void, void *>(seq);
}

// ---------------------------------------------------------------------
// Event / action hook
// ---------------------------------------------------------------------

int Hook_EVENT_ActionEnd(void *eventInstance)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    // Telemetry
    std::size_t idx = IndexOf(HookId_EVENT_ActionEnd);
    gHookCount[idx]++;

    static int sLogCount = 0;

    // -------------------------------------------------------------
    // PRE: keep the existing structural logging (limited spam).
    // -------------------------------------------------------------
    if (sLogCount < 16 && eventInstance != nullptr)
    {
        auto *ev    = reinterpret_cast<UnitCommandEvent *>(eventInstance);
        auto *base8 = reinterpret_cast<std::uint8_t *>(eventInstance);
        auto *w     = reinterpret_cast<std::uint32_t *>(eventInstance);

        Logf("Hook_EVENT_ActionEnd(pre): inst=%p cmdId=%u side=%u seqMap=%p cmdData=%p unk28=%u",
             ev,
             ev->cmdId,
             ev->side,
             ev->seqMap,
             ev->cmdData,
             ev->unk28);

        // First 0x40 bytes (words 0..15)
        Logf("  inst[0x00..0x3C] = "
             "{%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,"
             "%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X}",
             w[0],  w[1],  w[2],  w[3],
             w[4],  w[5],  w[6],  w[7],
             w[8],  w[9],  w[10], w[11],
             w[12], w[13], w[14], w[15]);

        // Next 0x40 bytes (words 16..31)
        std::uint32_t *w2 = reinterpret_cast<std::uint32_t *>(base8 + 0x40);
        Logf("  inst[0x40..0x7C] = "
             "{%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,"
             "%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X}",
             w2[0],  w2[1],  w2[2],  w2[3],
             w2[4],  w2[5],  w2[6],  w2[7],
             w2[8],  w2[9],  w2[10], w2[11],
             w2[12], w2[13], w2[14], w2[15]);

        // Peek into cmdData, if present – likely where the acting unit lives.
        if (ev->cmdData != nullptr)
        {
            auto *cmd = reinterpret_cast<std::uint32_t *>(ev->cmdData);
            Logf("  cmdData[0x00..0x3C] = "
                 "{%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,"
                 "%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X}",
                 cmd[0],  cmd[1],  cmd[2],  cmd[3],
                 cmd[4],  cmd[5],  cmd[6],  cmd[7],
                 cmd[8],  cmd[9],  cmd[10], cmd[11],
                 cmd[12], cmd[13], cmd[14], cmd[15]);
        }
    }

    // -------------------------------------------------------------
    // Call the original event handler so the game does its work.
    // -------------------------------------------------------------
    HookContext &ctx = HookContext::GetCurrent();
    int result = ctx.OriginalFunction<int, void *>(eventInstance);

    // -------------------------------------------------------------
    // Engine-level notification: generic "action has ended" event.
    // -------------------------------------------------------------
    if (eventInstance != nullptr)
    {
        auto *ev = reinterpret_cast<UnitCommandEvent *>(eventInstance);

        // Raw side value from the struct (1 = Side1, 2 = Side2, etc.).
        std::uint32_t sideRaw = ev->side;

        // Canonical side from our global turn tracker.
        TurnSide sideEnum = gCurrentTurnSide;

        // Feed a minimal, future-proof payload into the engine.
        Engine::OnActionEnd(
            eventInstance,   // inst
            ev->seqMap,      // seqMap (same as SEQ_MapStart seq)
            ev->cmdData,     // cmdData pointer
            ev->cmdId,       // raw command id
            sideRaw,         // sideRaw from struct
            sideEnum,        // canonical TurnSide
            ev->unk28        // extra mode/flags word
        );
    }

    // -------------------------------------------------------------
    // POST log (once per call pair).
    // -------------------------------------------------------------
    if (sLogCount < 16)
    {
        Logf("Hook_EVENT_ActionEnd(post): inst=%p -> %d (n=%d)",
             eventInstance, result, sLogCount + 1);
        ++sLogCount;
    }

    return result;
}

// ---------------------------------------------------------------------
// Battle support / Attack Stance
// ---------------------------------------------------------------------

int Hook_BTL_AttackStance_Check(void *situation,
                                int   index)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idxCount = IndexOf(HookId_BTL_AttackStance_Check);
    gHookCount[idxCount]++;

    HookContext &ctx = HookContext::GetCurrent();

    // bool map__Situation__CanDual(Situation* self, int index)
    int result = ctx.OriginalFunction<int, void *, int>(
        situation,
        index
    );

    static int sLogCount  = 0;
    static int sDumpCount = 0;

    // Lightweight logging
    if (sLogCount < 16)
    {
        Logf("Hook_BTL_AttackStance_Check(CanDual): sit=%p idx=%d -> %d (n=%d)",
             situation,
             index,
             result,
             sLogCount + 1);
        ++sLogCount;
    }

    // Extra: limited hexdump of the situation struct for RE.
    // Only dump for heap-like addresses, and only a few times.
    if (situation != nullptr && sDumpCount < 8)
    {
        std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(situation);
        if (addr >= kHeapMinVA && addr <= kHeapMaxVA)
        {
            auto *w = reinterpret_cast<std::uint32_t *>(situation);

            Logf("  sit[0x00..0x3C] = "
                 "{%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,"
                 "%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X}",
                 w[0],  w[1],  w[2],  w[3],
                 w[4],  w[5],  w[6],  w[7],
                 w[8],  w[9],  w[10], w[11],
                 w[12], w[13], w[14], w[15]);

            ++sDumpCount;
        }
    }

    return result;
}


void Hook_BTL_AttackStance_ApplySupport(void *battleInfo)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_BTL_AttackStance_ApplySupport);
    gHookCount[idx]++;

    HookContext &ctx = HookContext::GetCurrent();
    ctx.OriginalFunction<void, void *>(battleInfo);

    static int sLogCount = 0;
    if (sLogCount < 16 && battleInfo != nullptr)
    {
        auto *root = reinterpret_cast<BattleRoot *>(battleInfo);

        u32 *w = reinterpret_cast<u32 *>(root);

        Logf("Hook_BTL_AttackStance_ApplySupport(CalculateDual): root=%p "
             "w0=%08X w1=%08X flags=%08X unk14=%d unk18=%u unk1C=%u (n=%d)",
             root,
             w[0], w[1],
             root->flags,
             root->unk14,
             root->unk18,
             root->unk1C,
             sLogCount + 1);

        ++sLogCount;
    }
}

// ---------------------------------------------------------------------
// HUD and skill hooks
// ---------------------------------------------------------------------
// THIS IS CURRENTLY NONFUNCTIONAL, DO NOT USE, ONLY RESERVED.
//

void Hook_HUD_Battle_HPGaugeUpdate(void * hudContext,
                                   void * unit)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_HUD_Battle_HPGaugeUpdate);
    gHookCount[idx]++;

    HookContext &ctx = HookContext::GetCurrent();
    ctx.OriginalFunction<void, void *, void *>(hudContext, unit);
}

int Hook_BTL_SkillEffect_Apply(void *battleContext,
                               void *attacker,
                               void *defender,
                               std::uint32_t skillIdOrFlags)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_BTL_SkillEffect_Apply);
    gHookCount[idx]++;

    static int sLogCount = 0;
    if (sLogCount < 64)
    {
        Logf("Hook_BTL_SkillEffect_Apply: bc=%p atk=%p def=%p skill=0x%08X (n=%d)",
             battleContext, attacker, defender, skillIdOrFlags, sLogCount + 1);
        ++sLogCount;
    }

    HookContext &ctx = HookContext::GetCurrent();
    int result = ctx.OriginalFunction<int, void *, void *, void *, std::uint32_t>(
        battleContext, attacker, defender, skillIdOrFlags);

    return result;
}

// ---------------------------------------------------------------------
// Map sequence hooks (global turn + map-end)
// ---------------------------------------------------------------------

void Hook_SEQ_TurnBegin()
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_SEQ_TurnBegin);
    gHookCount[idx]++;

	TurnSide side = GetTurnSideEnum();
	gCurrentTurnSide = side;

	// Feed into map lifecycle summary.
	MapLife_OnTurnBegin(side);

	// Engine notification: a new turn has started.
	Engine::OnTurnBegin(side);

	HookContext &ctx = HookContext::GetCurrent();
	ctx.OriginalFunction<void>();


    static int sLogCount = 0;
    if (sLogCount < 64)
    {
        std::uint8_t raw = GetTurnSideIndexRaw();
        Logf("Hook_SEQ_TurnBegin: sideRaw=%u side=%s (n=%d)",
             static_cast<unsigned>(raw),
             TurnSideToString(side),
             sLogCount + 1);
        ++sLogCount;
    }
}

int Hook_SEQ_TurnEnd(void *seq)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_SEQ_TurnEnd);
    gHookCount[idx]++;

	HookContext &ctx = HookContext::GetCurrent();

	int result = ctx.OriginalFunction<int, void *>(seq);

	// Use the last turn side
	// maintained in Hook_SEQ_TurnBegin.
	TurnSide side = gCurrentTurnSide;

	// Engine notification: a turn just ended.
	Engine::OnTurnEnd(side, seq);

	static int sLogCount = 0;
	if (sLogCount < 64)
	{
		Logf("Hook_SEQ_TurnEnd: seq=%p side=%s -> %d (n=%d)",
			seq,
			TurnSideToString(gCurrentTurnSide),
			result,
			sLogCount + 1);
		++sLogCount;
	}


    return result;
}

int Hook_SEQ_MapEnd(void *seq)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_SEQ_MapEnd);
    gHookCount[idx]++;

    HookContext &ctx = HookContext::GetCurrent();
    int result = ctx.OriginalFunction<int, void *>(seq);

    // Use the same helper as elsewhere to derive side.
    TurnSide side = GetTurnSideEnum();

    // Notify the engine that the map has ended / completed.
    Engine::OnMapEnd(seq, side);

    // Mark map as inactive in the lifecycle summary.
    MapLife_OnMapEnd();

    static int sLogCount = 0;
    if (sLogCount < 64)
    {
        Logf("Hook_SEQ_MapEnd(Complete): seq=%p side=%s -> %d "
             "(n=%d, gen=%u totalTurns=%u totalKills=%u "
             "[S0=%u S1=%u S2=%u S3=%u])",
             seq,
             TurnSideToString(side),
             result,
             sLogCount + 1,
             static_cast<unsigned>(gMapState.generation),
             gMapState.totalTurns,
             gMapStats.totalKills,
             gMapStats.killsBySide[0],
             gMapStats.killsBySide[1],
             gMapStats.killsBySide[2],
             gMapStats.killsBySide[3]);
        ++sLogCount;
    }

    return result;
}

void Hook_SEQ_MapStart(void *seq)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_SEQ_MapStart);
    gHookCount[idx]++;

    static void    *sLastSeq            = nullptr;
    static unsigned sMapGeneration      = 0;
    static int      sPersistentLogCount = 0;

    bool isNewMap = (seq != sLastSeq);

    if (isNewMap)
    {
        ++sMapGeneration;
        sLastSeq            = seq;
        sPersistentLogCount = 0;

		TurnSide side = GetTurnSideEnum();

		// Update global map state.
		MapLife_OnNewMap(seq, side);

		// Tell the engine a new map has begun.
		Engine::OnMapBegin(seq, side);

		Logf("Hook_SEQ_MapStart: NEW MAP gen=%u seq=%p side=%s",
			static_cast<unsigned>(sMapGeneration),
			seq,
			TurnSideToString(side));
    }
    else if (sPersistentLogCount < 8)
    {
        ++sPersistentLogCount;

        TurnSide side = GetTurnSideEnum();

        Logf("Hook_SEQ_MapStart(Persistent): seq=%p tick=%d side=%s",
             seq,
             sPersistentLogCount,
             TurnSideToString(side));
    }

    HookContext &ctx = HookContext::GetCurrent();
    ctx.OriginalFunction<void, void *>(seq);
}

void Hook_SEQ_ItemUse(void *seq)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_SEQ_ItemUse);
    gHookCount[idx]++;
    unsigned total = static_cast<unsigned>(gHookCount[idx]);

    void *unit   = nullptr;
    void *useCtx = nullptr;

    // Mirror what ProcSequence__Use does:
    //   r4 = seq
    //   r0 = [r4 + 0x30]   -> unit*
    //   r1 = r4 + 0x34     -> useCtx
    if (seq != nullptr)
    {
        auto *base = static_cast<std::uint8_t *>(seq);
        unit       = *reinterpret_cast<void **>(base + 0x30);
        useCtx     = base + 0x34;
    }

    static int sLogCount = 0;
    if (sLogCount < 64)
    {
        Logf("Hook_SEQ_ItemUse(ProcSequence__Use): total=%u seq=%p unit=%p useCtx=%p (n=%d)",
             total,
             seq,
             unit,
             useCtx,
             sLogCount + 1);
        ++sLogCount;
    }

    HookContext &ctx = HookContext::GetCurrent();
    // Actual signature is void (void *seq)
    ctx.OriginalFunction<void, void *>(seq);
}

void Hook_UNIT_LevelUp(void *unitRaw)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_UNIT_LevelUp);
    gHookCount[idx]++;
    unsigned total = static_cast<unsigned>(gHookCount[idx]);

    HookContext &ctx = HookContext::GetCurrent();

    // Let the game actually perform the level-up first.
    ctx.OriginalFunction<void, void *>(unitRaw);

    LevelUpPayload payload{};
    payload.unit  = reinterpret_cast<Unit *>(unitRaw);
    payload.level = 0;

    if (unitRaw != nullptr)
    {
        // Level is the byte at +0xF1 (from Unit__LevelUp disasm).
        std::uint32_t base = reinterpret_cast<std::uint32_t>(unitRaw);
        payload.level = *reinterpret_cast<std::uint8_t *>(base + 0xF1);
    }

    static int sLogCount = 0;
    if (sLogCount < 32)
    {
        Logf("Hook_UNIT_LevelUp: total=%u unit=%p level=%u (n=%d)",
             total,
             payload.unit,
             static_cast<unsigned>(payload.level),
             sLogCount + 1);
        ++sLogCount;
    }

    // Engine notification (map/turn aware).
    Engine::OnUnitLevelUp(unitRaw,
                          payload.level,
                          gCurrentTurnSide);
}

int Hook_UNIT_SkillLearn(void          *unitRaw,
                         std::uint32_t  skillIdRaw)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_UNIT_SkillLearn);
    gHookCount[idx]++;
    unsigned total = static_cast<unsigned>(gHookCount[idx]);

    HookContext &ctx = HookContext::GetCurrent();

    // int Unit__AddEquipSkill(Unit* unit, int skillId)
    int result = ctx.OriginalFunction<int, void *, std::uint32_t>(
        unitRaw, skillIdRaw);

    SkillLearnPayload payload{};
    payload.unit    = reinterpret_cast<Unit *>(unitRaw);
    payload.skillId = static_cast<std::uint16_t>(skillIdRaw);
    payload.flags   = 0; // future: set bits for source (level-up / scroll / script)

    static int sLogCount = 0;
    // Filter out the noisy "skillId == 0" + "result == 0" loader churn.
    if (sLogCount < 32 && skillIdRaw != 0 && result != 0)
    {
        Logf("Hook_UNIT_SkillLearn(Unit__AddEquipSkill): "
             "total=%u unit=%p skill=0x%04X result=%d (n=%d)",
             total,
             payload.unit,
             static_cast<unsigned>(payload.skillId),
             result,
             sLogCount + 1);
        ++sLogCount;
    }

    // Only treat real, successful learns as meaningful.
    if (skillIdRaw != 0 && result != 0)
    {
        // 1) Legacy debug tracker (used for RE logging in BTL_FinalDamage_Pre).
        DebugSkills_OnSkillLearn(payload.unit,
                                 payload.skillId);

        // 2) Engine notification (map/turn aware, as before).
        Engine::OnUnitSkillLearn(unitRaw,
                                 payload.skillId,
                                 payload.flags,
                                 result,
                                 gCurrentTurnSide);

        // 3) Skills engine bridge: keep a simple per-unit skill table
        //    that other engine modules can query.
        Engine::Skills::OnUnitSkillLearnRaw(unitRaw,
                                            payload.skillId,
                                            payload.flags,
                                            result,
                                            gCurrentTurnSide);
    }

    return result;
}

void Hook_SEQ_UnitMove(void *seq)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    // Telemetry: count how often player unit actions begin.
    std::size_t idx = IndexOf(HookId_SEQ_UnitMove);
    gHookCount[idx]++;

    HookContext &ctx = HookContext::GetCurrent();

    // Call the original ProcSequence__UnitMove(seq).
    ctx.OriginalFunction<void, void *>(seq);

    // Light logging window
    static int sLogCount = 0;
    if (sLogCount < 64)
    {
        Logf("Hook_SEQ_UnitMove(ProcSequence__UnitMove): seq=%p (n=%d)",
             seq,
             sLogCount + 1);
        ++sLogCount;
    }
}


} // extern "C"
