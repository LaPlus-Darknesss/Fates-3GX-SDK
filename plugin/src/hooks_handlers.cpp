// hooks_handlers.cpp
//
// Implements the C stub handlers for each hook declared in
// core/handlers.hpp. Each stub currently:
//
//   1) Increments its entry in Fates::gHookCount for telemetry (most hooks).
//   2) Optionally logs to fates_3gx.log.
//   3) Bridges into the engine bus (events.hpp), skills engine, and
//      combat pipeline where appropriate.
//
// Ongoing RE: many hooks are still observation-only.

#include <CTRPluginFramework.hpp>
#include <cstddef>   // for std::size_t
#include <cstdint>

#include "core/hooks.hpp"
#include "core/runtime.hpp"
#include "core/handlers.hpp"
#include "util/debug_log.hpp"
#include "hook_debug.hpp"   // DumpHookCountsToFile / DumpKillEventsToLog
#include "engine/events.hpp"
#include "engine/skills.hpp"   // bridge into skill engine
#include "engine/combat.hpp"   // final-damage / forecast pipeline
#include "engine/DamageStages.hpp"  // Damage::Stage enum for pipeline roots


using namespace CTRPluginFramework;

namespace Fates
{
    // Forward-declare Unit so we can point at it from BattleRoot.
    struct Unit;

    // NEW: forward declaration for the skill-damage rule registration.
    namespace Engine {
    namespace Skills {
        void RegisterDamageRules();
    }
    }

    // This models the "root" battle struct we see consistently in both
    // BTL_AttackStance_ApplySupport and BTL_FinalDamage_Pre logs.
    //
    // The only field we *actively* depend on is mainUnit, which has been
    // confirmed to match the Unit* that UNIT_UpdateCloneHP uses as its
    // "source" unit. The rest are kept as opaque for now, but the comments
    // document what we've observed so far.
    struct BattleRoot
    {
        std::uint32_t pad0;      // 0x00 - usually 0
        Unit         *mainUnit;  // 0x04 - Unit* for the main attacker
        std::uint32_t unk08;     // 0x08 - unknown, non-null in tests
        std::uint32_t unk0C;     // 0x0C - unknown
        std::uint32_t flags;     // 0x10 - 0x4000xxxx / 0x4001xxxx patterns
        int           unk14;     // 0x14 - small -1 / 0 / 1 slot-ish values
        std::uint32_t unk18;     // 0x18 - small ints (hit/slot-style values)
        std::uint32_t unk1C;     // 0x1C - small ints
    };

    // Minimal view of BattleCalculator: only care that [0] is BattleRoot*.
    struct BattleCalculator
    {
        BattleRoot *root;  // 0x00
        // Remaining fields currently unknown / unused.
    };

    // Minimal overlay for the "final damage calc" object seen in
    // BTL_FinalDamage_Pre.
    //
    // Ghidra struct: FinalDamageCalcProbe_t (size 0x20), which we
    // currently model as:
    //
    //   +0x00 : BattleRoot_t *root
    //   +0x04 : int maxHp
    //   +0x08 : int hpAfter
    //   +0x0C : int unk0C
    //   +0x10 : int hpBefore
    //   +0x14 : int unk14
    //   +0x18 : int unk18
    //   +0x1C : int unk1C
    //
    // BTL_FinalDamage_Pre is called multiple times per battle. On the early
    // passes we see small deltas (e.g. 3 damage), and on the final pass the
    // delta matches the full damage actually applied. We currently treat each
    // call independently and only *read* the fields.
    struct FinalDamageCalcProbe
    {
        BattleRoot  *root;      // 0x00 - BattleRoot_t*
        std::int32_t maxHp;     // 0x04
        std::int32_t hpAfter;   // 0x08
        std::int32_t unk0C;     // 0x0C
        std::int32_t hpBefore;  // 0x10
        std::int32_t unk14;     // 0x14
        std::int32_t unk18;     // 0x18
        std::int32_t unk1C;     // 0x1C
    };

    // Alias to mirror the Ghidra datatype name.
    using FinalDamageCalcProbe_t = FinalDamageCalcProbe;

    // Compile-time guard to keep the C++ overlay in lockstep with Ghidra.
    static_assert(sizeof(FinalDamageCalcProbe_t) == 0x20,
                  "FinalDamageCalcProbe_t layout mismatch; "
                  "check Ghidra FinalDamageCalcProbe_t");

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
	
	// Offset inside the *simple* BattleInfo::Side record where the Unit*
	// for that side is stored. Confirmed from Ghidra:
	//   BattleInfoSimpleSide_t
	//     +0x00: slotKind_orSide_00 (u8)
	//     +0x04: unitPtr_04         (Unit*)
	//     ...
	static constexpr std::size_t kBattleInfoSimpleSide_UnitOffset = 0x0004u;

	// Safely peel a Unit* out of a simple BattleInfo::Side record.
	static inline void *GetUnitFromSimpleSide(void *sideRaw)
	{
		if (sideRaw == nullptr)
			return nullptr;

		auto *base = reinterpret_cast<std::uint8_t *>(sideRaw);
		void *unit = *reinterpret_cast<void **>(
			base + kBattleInfoSimpleSide_UnitOffset);

		// Heap sanity guard; keeps us from treating nullptr or junk
		// as a real Unit*.
		std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(unit);
		if (addr < kHeapMinVA || addr > kHeapMaxVA)
			return nullptr;

		return unit;
	}
	
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

    // Called when we detect a NEW map root in Hook_SEQ_MapStart.
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
    }

    // Called by Hook_SEQ_TurnBegin.
    static inline void MapLife_OnTurnBegin(TurnSide side)
    {
        gMapState.currentSide = side;

        ++gMapState.totalTurns;

        int idx = static_cast<int>(side);
        if (0 <= idx && idx <= 3)
            gMapState.turnCount[idx] = gMapState.turnCount[idx] + 1;
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

    // Debug helper: dump the first 0x80 bytes of a battle-root-like struct.
    // tag: small label so we know which hook called it (1 = AttackStance, 2 = FinalDamage, etc.).
    // dumpIdx: per-hook index passed in by the caller.
    static inline void DebugDumpBattleRoot(void *root, int tag, int dumpIdx)
    {
        if (root == nullptr)
            return;

        std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(root);
        if (addr < kHeapMinVA || addr > kHeapMaxVA)
            return; // ignore weird / stack-ish pointers

        // Global-ish cap to avoid spamming logs to death.
        static int sTotalRootDumps = 0;
        if (sTotalRootDumps >= 16)
            return;
        ++sTotalRootDumps;

        auto *base = reinterpret_cast<std::uint8_t *>(root);

        Logf("[RootDump] tag=%d idx=%d root=%p",
             tag,
             dumpIdx,
             root);

        for (int off = 0; off < 0x80; off += 0x10)
        {
            auto *p = reinterpret_cast<std::uint32_t *>(base + off);
            Logf("  +0x%02X : %08X %08X %08X %08X",
                 off,
                 p[0], p[1], p[2], p[3]);
        }
    }

    // Debug helper: dump the BattleInfo::Side-ish struct that
    // uses offsets 0x64..0x78 in CalculateEfficacy / HpWindow.
    //
    // phaseTag: "pre", "post", "hpwin", etc.
    // dumpIdx : small index so we can correlate multiple calls.
    static inline void DumpBattleInfoSide(void *sideRaw,
                                          const char *phaseTag,
                                          int dumpIdx)
    {
        if (sideRaw == nullptr || phaseTag == nullptr)
            return;

        std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(sideRaw);
        if (addr < kHeapMinVA || addr > kHeapMaxVA)
            return; // ignore non-heap pointers

        auto *base = reinterpret_cast<std::uint8_t *>(sideRaw);

        auto read32 = [base](std::size_t off) -> std::uint32_t {
            return *reinterpret_cast<std::uint32_t *>(base + off);
        };

        // Raw 32-bit views of the interesting window.
        std::uint32_t w64 = read32(0x64);
        std::uint32_t w68 = read32(0x68);
        std::uint32_t w70 = read32(0x70);
        std::uint32_t w74 = read32(0x74);
        std::uint32_t w78 = read32(0x78);

        Logf("BattleInfoSide[%s]: side=%p idx=%d "
             "w64=%08X w68=%08X w70=%08X w74=%08X w78=%08X",
             phaseTag,
             sideRaw,
             dumpIdx,
             w64, w68, w70, w74, w78);

        // Treat +0x74/+0x78 as pointer candidates and log them.
        void *ptr74 = *reinterpret_cast<void * *>(base + 0x74);
        void *ptr78 = *reinterpret_cast<void * *>(base + 0x78);

        Logf("BattleInfoSide[%s]: side=%p ptr74=%p ptr78=%p",
             phaseTag,
             sideRaw,
             ptr74,
             ptr78);

        // If these look like heap pointers, piggyback the root hexdump
        // so we can see if they match our BattleRoot samples.
        std::uintptr_t p74Addr = reinterpret_cast<std::uintptr_t>(ptr74);
        if (p74Addr >= kHeapMinVA && p74Addr <= kHeapMaxVA)
        {
            DebugDumpBattleRoot(ptr74, /*tag=*/3, dumpIdx);
        }

        std::uintptr_t p78Addr = reinterpret_cast<std::uintptr_t>(ptr78);
        if (p78Addr >= kHeapMinVA && p78Addr <= kHeapMaxVA)
        {
            DebugDumpBattleRoot(ptr78, /*tag=*/4, dumpIdx);
        }
    }

    // Try to peel a BattleRoot* out of a BattleInfo::Side-ish struct.
    // We reuse the same 0x74/0x78 heuristics used by DumpBattleInfoSide.
    static inline BattleRoot *GetRootFromBattleInfoSide(void *sideRaw)
    {
        if (sideRaw == nullptr)
            return nullptr;

        auto *base = reinterpret_cast<std::uint8_t *>(sideRaw);

        void *ptr74 = *reinterpret_cast<void * *>(base + 0x74);
        void *ptr78 = *reinterpret_cast<void * *>(base + 0x78);

        auto choose = [](void *p) -> BattleRoot *
        {
            if (p == nullptr)
                return nullptr;

            std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(p);
            if (addr < kHeapMinVA || addr > kHeapMaxVA)
                return nullptr;

            return reinterpret_cast<BattleRoot *>(p);
        };

        BattleRoot *root = choose(ptr74);
        if (!root)
            root = choose(ptr78);

        return root;
    }
	
	// Shared helper used by various hooks (FinalDamage_Pre,
	// BattleInfo_CalculateSimple, EquipSkillCalculator) to decode
	// 0x4000XXXX / 0x4001XXXX driver flags into Skills::OnUnitHasSkillObserved.
    static inline void ObserveSkillDriverFromFlags(void *unitRaw,
                                                   std::uint32_t flags,
                                                   const char    *tag)
    {
        if (unitRaw == nullptr)
            return;

        // Low 16 bits -> candidate skill id.
        std::uint16_t skillId =
            static_cast<std::uint16_t>(flags & 0xFFFFu);

        bool hasSkillDriver =
            ((flags & 0x40000000u) != 0u) && (skillId != 0u);
        if (!hasSkillDriver)
            return;

        // Only touch heap-like unit pointers.
        std::uintptr_t addr =
            reinterpret_cast<std::uintptr_t>(unitRaw);
        if (addr < kHeapMinVA || addr > kHeapMaxVA)
            return;

        // Feed into the per-map skill table.
        Engine::Skills::OnUnitHasSkillObserved(unitRaw, skillId);

        // Light logging so we can correlate which path saw the driver.
        static int sLogCount = 0;
        if (sLogCount < 32)
        {
            Logf("SkillDriver[%s]: unit=%p flags=%08X skill=0x%04X (n=%d)",
                 (tag != nullptr) ? tag : "?",
                 unitRaw,
                 flags,
                 static_cast<unsigned>(skillId),
                 sLogCount + 1);
            ++sLogCount;
        }
    }

    // -----------------------------------------------------------------
    // Unit__HasSkillById stats (global skill-query observation)
    // -----------------------------------------------------------------

    struct UnitHasSkillByIdStats
    {
        std::uint32_t totalCalls    = 0;
        std::uint32_t totalPositive = 0; // result != 0
        std::uint32_t totalNegative = 0; // result == 0
    };

    static UnitHasSkillByIdStats g_UnitHasSkillByIdStats;

    struct LevelUpPayload
    {
        Unit         *unit;    // main unit pointer
        std::uint8_t  level;   // unit's new level after the ding
        std::uint8_t  _pad[3]; // reserved for future (class id, flags, etc.)
    };

    struct SkillLearnPayload
    {
        Unit           *unit;     // learner
        std::uint16_t   skillId;  // learned skill
        std::uint16_t   flags;    // reserved (source: level, scroll, script, etc.)
    };

    // Minimal view of the event instance passed to EVENT_ActionEnd.
    struct UnitCommandEvent
    {
        void         *vtable;     // [0x00]
        void         *unk04;      // [0x04]
        void         *unk08;      // [0x08]
        void         *updateFunc; // [0x0C] -> 0x00354704 (ProcSequence__UnitMove)
        void         *seqMap;     // [0x10] -> matches SEQ_MapStart seq
        void         *unk14;      // [0x14]
        void         *unk18;      // [0x18]
        void         *cmdData;    // [0x1C] -> likely command data/context
        std::uint32_t cmdId;      // [0x20] -> 0x0C in attack test (prob. command type)
        std::uint32_t side;       // [0x24] -> 1 = Side1 (player)
        std::uint32_t unk28;      // [0x28] -> 6
        void         *unk2C;      // [0x2C]
        void         *unk30;      // [0x30]
        void         *unk34;      // [0x34]
        void         *unk38;      // [0x38]
        void         *unk3C;      // [0x3C]
    };

} // namespace Fates

// ---------------------------------------------------------------------
// Internal state for post-battle HP / damage experiments
// ---------------------------------------------------------------------

// Tracks the most recent battle root observed in BTL_FinalDamage_Pre.
// (Currently used only for RE; safe to ignore.)
static Fates::BattleRoot *sLastBattleRoot = nullptr;

// Legacy FINAL_HP write-back guard.
//
// This used to gate experimental HP write-back in BTL_FinalDamage_Pre.
// There are no callers in the v2 pipeline; we keep the symbol around
// as documentation of the old experiment. If it starts getting in the
// way, it is safe to delete.
//[[maybe_unused]]
static constexpr bool kDamageWritebackEnabled = false;

// Global guard for forecast-side write-back from
// Hook_MAP_BattleInfo_CalculateSimple.
//
// Keep this false in normal / public builds. Turning it on should only
// happen in tightly controlled tests where FINAL_HP hooks are already
// wired through the same damage engine, so that HUD, forecast, and HP
// stay in lockstep.
static constexpr bool kForecastWritebackEnabled = false;


// Forward decl (defined later).
extern "C" int Hook_SEQ_HpDamage_Helper(void *a0,
                                        void *a1,
                                        void *a2,
                                        void *a3);

extern "C" {

// ---------------------------------------------------------------------
// Battle math hooks
// ---------------------------------------------------------------------

// BTL_HitCalc_Main / map__battle__detail__RandomCalculateHit
//
// Stage: HIT_RNG
//
// This hook sits on the RNG roll step, not the stat-based hit formula.
// r0 carries a pre-computed hit fraction/percent ("true hit" threshold),
// and the original function warps it via nn::math::SinFIdx, compares
// against Random__GetValue, and returns non-zero on hit, 0 on miss.
//
// CONTRACT (v2 hygiene):
//   * Treat hitThreshold as an already-computed threshold.
//   * Do NOT recompute displayed Hit% here.
//   * Do NOT touch BattleInfo/BattleResult or any HUD-facing fields.
//   * It is safe to:
//       - Observe/log thresholds and results.
//       - Eventually adjust hitThreshold for custom RNG curves,
//         as long as the forecast layer stays in sync elsewhere.
//
int Hook_BTL_HitCalc_Main(int hitThreshold)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    // Telemetry: track how often the hit RNG is called.
    std::size_t idx = IndexOf(HookId_BTL_HitCalc_Main);
    gHookCount[idx]++;

    HookContext &ctx = HookContext::GetCurrent();

    // Call the original map__battle__detail__RandomCalculateHit(hitThreshold).
    // Under the hood this returns a bool (non-zero = hit, 0 = miss).
    int result = ctx.OriginalFunction<int, int>(hitThreshold);

    // Engine-level summary (map/turn aware).
    // Signature stays (threshold, result) as before.
    Engine::OnHitCalc(hitThreshold, result);

    // Light logging window to correlate thresholds with outcomes.
    static int sLogCount = 0;
    if (sLogCount < 64)
    {
        Logf("Hook_BTL_HitCalc_Main(RandomCalculateHit): "
             "threshold=%d -> result=%d (n=%d)",
             hitThreshold,
             result,
             sLogCount + 1);
        ++sLogCount;
    }

    return result;
}

// TODO RNG v2 audit:
//   This hook currently forwards to vanilla SYS_Rng32, but we have not
//   exhaustively verified that our hook sites and upperBound usage are
//   100% identical to vanilla behavior. Early testing suggested some
//   suspicious hit/crit streaks (e.g. clusters of low-percent crits),
//   but nothing conclusively broken.
//   Priority: low (gameplay is stable), but before any public release
//   we should:
//     * Reconfirm the exact RNG distribution vs vanilla for hit/crit,
//     * Ensure we are not biasing rolls in specific contexts
//       (battle calc vs forecast UI, skills, etc.),
//     * Document any intentional deviations if we ever change it.
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

    // Core RNG-step function at 0x0044AE14.
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
int Hook_BTL_CritCalc_Main(void *unit,
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

    // Light logging.
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

// BTL_FinalDamage_Pre – FINAL_HP observation hook.
//
// Host (NA v1.1): BTL_FinalDamage_Pre @ 0x003628BC
// Stage: FINAL_HP.
//
// Behaviour summary (from RE + logs):
//   * Called multiple times per battle, each time with a FinalDamageCalc-like
//     object whose root points at the shared BattleRoot.
//   * hpBefore / hpAfter on the calc give the exact HP delta for THAT pass.
//   * The last pass’s delta matches the full damage actually applied.
//
// v2 hygiene:
//   * This hook is **observation-only**. We treat it as a place to:
//       - Log hpBefore/hpAfter and deltas per call.
//       - Probe BattleRoot fields (numHits, dmgPerHit, snapshots, etc.).
//       - Feed BattleRoot::flags into the skills engine.
//   * We do **not**:
//       - Change hpBefore / hpAfter.
//       - Recompute primary damage here.
//       - Try to “fix” the forecast from this stage.
//   * If we ever introduce DamageOrigin::FinalHp, it should be for last-ditch
//     adjustments only, never the main damage formula.
int Hook_BTL_FinalDamage_Pre(void *calc,
                             void *arg1,
                             void *arg2,
                             void *arg3)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    // Telemetry
    std::size_t idx = IndexOf(HookId_BTL_FinalDamage_Pre);
    gHookCount[idx]++;

    HookContext &ctx = HookContext::GetCurrent();

    // Call the original function once so the game does its normal work.
	int retRaw =
		ctx.OriginalFunction<int,
							void *, void *, void *, void *>(
			calc, arg1, arg2, arg3);

    // Light entry logging.
    static int sEntryLogCount = 0;
    if (sEntryLogCount < 32)
    {
        Logf("Hook_BTL_FinalDamage_Pre(entry): calc=%p arg1=%p arg2=%p "
             "arg3=%p retRaw=%08X (n=%d)",
             calc,
             arg1,
             arg2,
             arg3,
             static_cast<unsigned>(retRaw),
             sEntryLogCount + 1);
        ++sEntryLogCount;
    }

    // Guard against nonsense pointers.
    if (calc == nullptr)
        return retRaw;

    std::uintptr_t calcAddr = reinterpret_cast<std::uintptr_t>(calc);
    if (calcAddr < kHeapMinVA || calcAddr > kHeapMaxVA)
        return retRaw;

    auto *probe = reinterpret_cast<FinalDamageCalcProbe *>(calc);

    // ------------------------
    // Resolve root pointer.
    // ------------------------
    BattleRoot *root = nullptr;
    if (probe != nullptr && probe->root != nullptr)
    {
        std::uintptr_t rootAddr =
            reinterpret_cast<std::uintptr_t>(probe->root);
        if (rootAddr >= kHeapMinVA && rootAddr <= kHeapMaxVA)
            root = probe->root;
    }

    sLastBattleRoot = root;

    // ------------------------
    // HP snapshot: treat hpBefore - hpAfter as
    // "damage for this pass".
    // ------------------------
    int  maxHp        = 0;
    int  hpBefore     = 0;
    int  hpAfter      = 0;
    int  damageFromHp = 0;   // damage for THIS call
    bool haveHpDamage = false;

    if (probe != nullptr)
    {
        maxHp        = probe->maxHp;
        hpAfter      = probe->hpAfter;
        hpBefore     = probe->hpBefore;
        damageFromHp = hpBefore - hpAfter;

        // Basic sanity: ignore obviously bogus values.
        if (maxHp > 0 && maxHp <= 200 &&
            hpBefore >= 0 && hpBefore <= maxHp &&
            hpAfter  >= 0 && hpAfter  <= maxHp &&
            damageFromHp >= 0 && damageFromHp <= 200)
        {
            haveHpDamage = true;
        }

        static int sHpLogCount = 0;
        if (haveHpDamage && sHpLogCount < 32)
        {
            Logf("Hook_BTL_FinalDamage_Pre(hp): calc=%p root=%p mainUnit=%p "
                 "maxHp=%d hpBefore=%d hpAfter=%d delta=%d retRaw=%08X (n=%d)",
                 calc,
                 root,
                 (root != nullptr)
                     ? static_cast<void *>(root->mainUnit)
                     : nullptr,
                 maxHp,
                 hpBefore,
                 hpAfter,
                 damageFromHp,
                 static_cast<unsigned>(retRaw),
                 sHpLogCount + 1);
            ++sHpLogCount;
        }
    }

    // ------------------------
    // Extra struct-field logging for RE.
    // ------------------------
    static int sCalcFieldLogCount = 0;
    if (probe != nullptr && sCalcFieldLogCount < 32)
    {
        auto *calcBytes = reinterpret_cast<std::uint8_t *>(calc);

        std::uint8_t  b04 = calcBytes[0x04];
        std::uint8_t  b05 = calcBytes[0x05];
        std::int32_t  w08 = *reinterpret_cast<std::int32_t *>(calcBytes + 0x08);
        std::int32_t  w0C = *reinterpret_cast<std::int32_t *>(calcBytes + 0x0C);
        std::int32_t  w10 = *reinterpret_cast<std::int32_t *>(calcBytes + 0x10);

        Logf("Hook_BTL_FinalDamage_Pre(fields): "
             "b04=%u b05=%u w08=%d w0C=%d w10=%d",
             static_cast<unsigned>(b04),
             static_cast<unsigned>(b05),
             w08,
             w0C,
             w10);

        ++sCalcFieldLogCount;
    }

    // ------------------------
    // Root-level probe: forecast / per-hit fields.
    // (Read-only, used for logging + engine context.)
    // ------------------------
    static int sRootProbeLogCount = 0;
    int        numHitsMaybe       = 0;
    int        dmgPerHitMaybe     = 0;
    int        forecastTotal      = 0;

    if (root != nullptr)
    {
        auto *rootWords = reinterpret_cast<std::int32_t *>(root);

        int hitRateMaybe      = rootWords[0x30 / 4]; // expect 100
        int critRateMaybe     = rootWords[0x34 / 4]; // expect 13
        int numHitsField      = rootWords[0x3C / 4]; // expect 2
        int displayCritMaybe  = rootWords[0x48 / 4]; // expect 13
        int displayHpMaybe    = rootWords[0x4C / 4]; // expect 25
        int dmgPerHitField    = rootWords[0x50 / 4]; // expect 3

        int snapHpMaybe       = rootWords[0x70 / 4]; // expect 25
        int snapDmgMaybe      = rootWords[0x74 / 4]; // expect 3
        int snapOtherHpMaybe  = rootWords[0x78 / 4]; // expect 30
        int snapOtherDmgMaybe = rootWords[0x7C / 4]; // expect 3

        numHitsMaybe   = numHitsField;
        dmgPerHitMaybe = dmgPerHitField;

        // Derive a forecast total if the fields look sane.
        if (numHitsMaybe > 0 &&
            numHitsMaybe <= 8 &&
            dmgPerHitMaybe >= 0 &&
            dmgPerHitMaybe <= 200)
        {
            forecastTotal = numHitsMaybe * dmgPerHitMaybe;
        }

        if (sRootProbeLogCount < 32)
        {
            Logf("RootDamageProbe: hit=%d crit=%d numHits=%d "
                 "dispCrit=%d dispHp=%d dmgPerHit=%d "
                 "snapHp=%d snapDmg=%d snapOtherHp=%d snapOtherDmg=%d "
                 "forecastTotal=%d",
                 hitRateMaybe,
                 critRateMaybe,
                 numHitsField,
                 displayCritMaybe,
                 displayHpMaybe,
                 dmgPerHitField,
                 snapHpMaybe,
                 snapDmgMaybe,
                 snapOtherHpMaybe,
                 snapOtherDmgMaybe,
                 forecastTotal);
            ++sRootProbeLogCount;
        }
    }

    // ------------------------
    // Dynamic skill detection from BattleRoot::flags
    // ------------------------
    if (root != nullptr && root->mainUnit != nullptr)
    {
        ObserveSkillDriverFromFlags(
            static_cast<void *>(root->mainUnit),
            root->flags,
            "BTL_FinalDamage_Pre");
    }

    // ------------------------
    // HP-commit path (observation only for now).
    //
    // At this stage we just log and harvest metadata (hpBefore/hpAfter,
    // skill driver flags, etc.). The actual damage logic for normal
    // skills is hosted in the forecast pipeline and allowed to flow
    // naturally into HP via the vanilla engine.
    //
    // If we ever need a true "FinalHp-only" rule, we can reintroduce a
    // DamageOrigin::FinalHp call here, but it should be treated as a
    // last-ditch adjustment, not the primary damage source.
    // ------------------------

    // ------------------------
    // Root snapshots + calc hexdump (unchanged RE helpers).
    // ------------------------
    static int sRootDumpIdx = 0;
    if (root != nullptr && sRootDumpIdx < 4)
    {
        DebugDumpBattleRoot(root, /*tag=*/2, sRootDumpIdx);
        ++sRootDumpIdx;
    }

    auto *w32 = reinterpret_cast<std::uint32_t *>(calc);

    static int sDumpCount = 0;
    if (sDumpCount < 16)
    {
        Logf("Hook_BTL_FinalDamage_Pre(dump): calc=%p root=%p mainUnit=%p "
             "retRaw=%08X dumpIdx=%d",
             calc,
             root,
             (root != nullptr)
                 ? static_cast<void *>(root->mainUnit)
                 : nullptr,
             static_cast<unsigned>(retRaw),
             sDumpCount);

        for (int i = 0; i < 32; i += 4)
        {
            Logf("  +0x%02X : %08X %08X %08X %08X",
                 i * 4,
                 w32[i + 0],
                 w32[i + 1],
                 w32[i + 2],
                 w32[i + 3]);
        }

        ++sDumpCount;
    }

    // NOTE: we still return retRaw; all meaningful HP changes are
    // driven via hpBefore/hpAfter. With write-back disabled this
    // remains read-only.
    return retRaw;
}

// Deprecated / RE-only, corresponds to legacy B4_FinalDamage_Post.
// This is a mid-function site and not a clean "post-final-damage"
// wrapper. Kept only so the concept exists and can be wired up for
// experiments if you ever re-enable the optional hook entry.
//
// Normal builds should treat this as unused.
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
void Hook_BTL_GuardGauge_Add(void *battleContext,
                             void *attacker,
                             void *defender)
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
void Hook_BTL_GuardGauge_Spend(void *battleContext,
                               void *attacker,
                               void *defender)
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

// Internal glue for SEQ_HpDamage.
//
// For now we do NOT use post-HP modifications here; this hook is kept
// as an observation point only. Any real damage changes will be routed
// through the forecast pipeline (MAP_BattleInfo_CalculateSimple), and
// HP is allowed to follow vanilla behaviour.

static std::uint32_t ApplyPostBattleHpDebug(void *seq,
                                            int   mode,
                                            int   slot,
                                            std::uint32_t hp)
{
    (void)seq;
    (void)mode;
    (void)slot;
    return hp;
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
        std::uint32_t base = reinterpret_cast<std::uint32_t>(unit);

        // Source HP: signed 8-bit value at +0xF3.
        std::int8_t srcHp    = *reinterpret_cast<signed char *>(base + 0xF3);
        int         srcHpInt = static_cast<int>(srcHp);

        // Engine-level: treat this as “unit HP has just been synced”.
        // This is now the canonical driver for HpChange events.
        Engine::OnUnitHpSync(unit, srcHpInt);

        // Clone pointer lives at +0xAC. You will most likely never touch this.
        void *clone = *reinterpret_cast<void * *>(base + 0xAC);

        int cloneHpInt = -1;
        if (clone != nullptr)
        {
            std::uint32_t cloneBase = reinterpret_cast<std::uint32_t>(clone);
            std::int8_t   cloneHp   = *reinterpret_cast<signed char *>(cloneBase + 0xF3);
            cloneHpInt              = static_cast<int>(cloneHp);
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

// UNIT_HpDamage (anonymous_namespace__UnitHpDamage)
//
// Stage: HP_FINAL_GATE (battle -> map HP driver).
//
// Ghidra shows this helper as a thin gate that:
//   * Checks a small-range integer argument (0 < x < 0xFB).
//   * Verifies some UnitPool / Force state.
//   * If checks pass, calls map__SequenceHelper__HpDamage(0).
//   * Otherwise returns without touching HP.
//
// In the SDK, we keep this hook strictly observation-only:
//   * All four parameters are treated as opaque r0..r3 copies.
//   * We only bump telemetry and, if enabled, log the raw register
//     values for reverse-engineering.
//   * We do NOT modify HP or emit engine HP events here; the real
//     semantics live in Hook_SEQ_HpDamage_Helper (SequenceHelper::HpDamage)
//     and Hook_UNIT_UpdateCloneHP (HP sync / clone sync).
//
// If future RE gives us a concrete signature, we can tighten the
// parameter types and payload—but v2 hygiene rule is: do not let
// this hook change game behaviour.
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

    // For now, treat the four registers as opaque and only log their
    // raw values for RE. The call chain is:
    //   anonymous_namespace__UnitHpDamage -> map__SequenceHelper__HpDamage
    // and we rely on the SequenceHelper hook + Unit__UpdateCloneHP for
    // real HP semantics.
    std::intptr_t r0Raw = reinterpret_cast<std::intptr_t>(a0);
    std::intptr_t r1Raw = reinterpret_cast<std::intptr_t>(a1);
    std::intptr_t r2Raw = reinterpret_cast<std::intptr_t>(a2);
    std::intptr_t r3Raw = reinterpret_cast<std::intptr_t>(a3);

    static int sLogCount = 0;
    if (gHpApplyLogEnabled && sLogCount < 64)
    {
        Logf("Hook_UNIT_HpDamage: total=%u r0=%p r1=0x%08X r2=0x%08X r3=0x%08X (n=%d)",
             total,
             a0,
             static_cast<std::uint32_t>(r1Raw),
             static_cast<std::uint32_t>(r2Raw),
             static_cast<std::uint32_t>(r3Raw),
             sLogCount + 1);
        ++sLogCount;
    }

    HookContext &ctx = HookContext::GetCurrent();
    int result = ctx.OriginalFunction<int, void *, void *, void *, void *>(
        a0, a1, a2, a3);

    return result;
}

void Hook_HP_KillCheck(void *seqBattle,
                       void *contextOrFlags)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    // Telemetry: count how many times this hook fires.
    std::size_t idx = IndexOf(HookId_HP_KillCheck);
    gHookCount[idx]++;

    // Run the real ProcSequence::DeadEvent first so that all of its
    // side-effects (event::Die, BGM, HP window handling, camera) are
    // committed before we inspect the sequence.
    HookContext &ctx = HookContext::GetCurrent();
    ctx.OriginalFunction<void, void *, void *>(seqBattle, contextOrFlags);

    if (seqBattle == nullptr)
        return;

    // Layout from Ghidra:
    //   +0x280 : killFlags bitfield
    //   +0x284 : dead slot 0 (Unit* or nullptr)
    //   +0x288 : dead slot 1 (Unit* or nullptr)
    std::uint32_t base      = reinterpret_cast<std::uint32_t>(seqBattle);
    std::uint32_t killFlags = *reinterpret_cast<std::uint32_t *>(base + 0x280);
    void         *dead0     = *reinterpret_cast<void * *>(base + 0x284);
    void         *dead1     = *reinterpret_cast<void * *>(base + 0x288);

    // From decomp:
    //   if ((killFlags & 1) != 0) {
    //       for i = 0..1:
    //           if (dead[i] != 0) HpWindow::Close();
    //       ... camera move ...
    //   }
    //
    // Treat "real kill" as: bit 0 set AND at least one dead slot.
    bool hasKillFlag = (killFlags & 1u) != 0u;
    bool hasDeadSlot = (dead0 != nullptr) || (dead1 != nullptr);

    if (!(hasKillFlag && hasDeadSlot))
        return;

    KillEvent ev{};
    ev.seq   = seqBattle;
    ev.dead0 = dead0;
    ev.dead1 = dead1;
    ev.flags = killFlags;

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

    // Let the engine know about the kill, keyed on current turn side.
    TurnSide side = gCurrentTurnSide;
    Engine::OnKill(ev, side);

    // Light logging window.
    static int sLogCount = 0;
    if (sLogCount < 64)
    {
        Logf("Hook_HP_KillCheck: seq=%p flags=0x%08X dead0=%p dead1=%p "
             "pushed=%d (eventIdx=%d, mapGen=%u mapKills=%u, "
             "totalKills=%u [S0=%u S1=%u S2=%u S3=%u], n=%d)",
             seqBattle,
             killFlags,
             dead0,
             dead1,
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

// Stage: FINAL_HP_DRIVER (map damage).
//
// This hook sees non-battle map damage being committed to a Unit:
//   r0 = seqHelper / proc root (opaque)
//   r1 = Unit* (damage target)
//   r2 = damageAmount (int)
//   r3 = modeOrFlags (int)
//
// v2 hygiene:
//   * Observation-only. We let the game apply HP and then rely on
//     UNIT_UpdateCloneHP + Engine::OnUnitHpSync for canonical
//     HP-change events.
int Hook_SEQ_HpDamage_Helper(void *seqHelper,
                             void *unit,
                             void *damageRaw,
                             void *modeRaw)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    // Telemetry: how often this hook fires.
    std::size_t idx = IndexOf(HookId_SEQ_HpDamage_Helper);
    gHookCount[idx]++;
    unsigned total = static_cast<unsigned>(gHookCount[idx]);

    // Third argument is the raw damage amount (positive int) in r2.
    int damageAmount =
        static_cast<int>(reinterpret_cast<std::intptr_t>(damageRaw));

    static int sLogCount = 0;
    if (gHpApplyLogEnabled && sLogCount < 64)
    {
        Logf("Hook_SEQ_HpDamage_Helper: total=%u dmg=%d seq=%p unit=%p mode=%p (n=%d)",
             total,
             damageAmount,
             seqHelper,
             unit,
             modeRaw,
             sLogCount + 1);
        ++sLogCount;
    }

    // IMPORTANT: no modification here. Just observe and forward.
    HookContext &ctx = HookContext::GetCurrent();
    int result = ctx.OriginalFunction<int,
                                      void *, void *, void *, void *>(
        seqHelper,  // r0: SequenceHelper* / context
        unit,       // r1: Unit*
        damageRaw,  // r2: original damage amount
        modeRaw     // r3: flags / mode
    );

    // Canonical HP-change events are handled via UNIT_UpdateCloneHP
    // (Hook_UNIT_UpdateCloneHP -> Engine::OnUnitHpSync). Do not add
    // direct Engine::OnHpChange calls from here.
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


// Map special sequence hooks (terrain heal / trick statue heal / skill cannon)
//
// Stage: MAP_PROC_HEAL/TRAP front-ends.
// These are Proc entrypoints that *schedule* terrain/trick/statue/skill-cannon
// effects and eventually drive HP via SequenceHelper::HpDamage and
// Unit__UpdateCloneHP, but do not directly write unit HP themselves.
//
// SDK policy:
//   * Observation-only hooks.
//   * Do not modify HP or proc state from here; treat them as RE probes or
//     light telemetry points, with real HP rules kept at the FINAL_HP and
//     HP_SYNC layers.
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

    // PRE: keep the existing structural logging (limited spam).
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

    // Call the original event handler so the game does its work.
    HookContext &ctx = HookContext::GetCurrent();
    int result = ctx.OriginalFunction<int, void *>(eventInstance);

    // Engine-level notification: generic "action has ended" event.
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

    // POST log (once per call pair).
    if (sLogCount < 16)
    {
        Logf("Hook_EVENT_ActionEnd(post): inst=%p -> %d (n=%d)",
             eventInstance, result, sLogCount + 1);
        ++sLogCount;
    }

    return result;
}

// ---------------------------------------------------------------------
// Attack Stance hooks
//
// Backed by the following vanilla functions (NA v1.1):
//
//   Hook_BTL_AttackStance_Check
//     -> map__Situation__CanDual @ 0x005281B8
//        Stage: ATTACK_STANCE_CHECK / eligibility gate.
//
//        Calling convention (observed):
//          * r0 = Situation* self
//          * r1 = indexOrOffset: byte offset into Situation that holds
//                a small per-slot state byte.
//
//        Vanilla behaviour (high level):
//          * If (self+0x4 & 0x80000) != 0 OR GameUserData(+0x28) bit 0x8
//            is clear, returns 1 (Attack Stance always allowed).
//          * Otherwise reads a byte at (self + indexOrOffset) and returns
//            1 only if that byte is exactly 1, else 0.
//
//        SDK rule:
//          * Pure boolean gate. Do NOT touch HP, BattleInfo, or any
//            result structs here. For v2 hygiene we only log + pass
//            through vanilla's result.
//
//   Hook_BTL_AttackStance_ApplySupport
//     -> map__BattleInfo__CalculateDual @ 0x00347350
//        Stage: PREVIEW_DETAIL / dual-guard finalizer.
//
//        Calling convention:
//          * r0 = BattleInfo* root.
//
//        Root layout (observed):
//          * root+0x804 : flags; bit 0x10 disables dual calc.
//          * root+0x28  : flags; bit 0x40 also gates dual/guard calc.
//
//        Per-side layout (sideIndex in {0,1},
//                         side = root + 0x200 * sideIndex):
//          * side+0x90 : Unit* supportUnit (or nullptr).
//          * side+0x94 : u8 dualEnabled flag.
//          * side+0x98 : u32 synergy accumulator (lead/support bytes@0x135).
//          * side+0x28 : sideFlags; bits 0x100 / 0x4000 are dual gates.
//          * side+0x3C : strike count; <1 -> output cleared.
//          * side+0x43C: final dual / guard output field.
//
//        SDK rule:
//          * Forecast-only surface. Never write HP here and never treat
//            this as primary damage math. For v2 hygiene we just log
//            key fields after vanilla runs.
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

    // Telemetry
    std::size_t idx = IndexOf(HookId_BTL_AttackStance_ApplySupport);
    gHookCount[idx]++;

    HookContext &ctx = HookContext::GetCurrent();

    // Let the game do its normal ApplySupport work first.
    ctx.OriginalFunction<void, void *>(battleInfo);

    if (battleInfo == nullptr)
        return;

    // Treat the argument as our BattleRoot.
    auto *root = reinterpret_cast<BattleRoot *>(battleInfo);

    // Guard: only touch heap-like pointers.
    std::uintptr_t rootAddr = reinterpret_cast<std::uintptr_t>(root);
    if (rootAddr < kHeapMinVA || rootAddr > kHeapMaxVA)
        return;

    // Basic structural dumps for RE.
    static int sRootDumpIdx = 0;
    if (sRootDumpIdx < 8)
    {
        // tag = 1 for AttackStance / forecast side.
        DebugDumpBattleRoot(root, /*tag=*/1, sRootDumpIdx);
        ++sRootDumpIdx;
    }

    static int sBasicLogCount = 0;
    if (sBasicLogCount < 16)
    {
        std::uint32_t *w = reinterpret_cast<std::uint32_t *>(root);

        Logf("Hook_BTL_AttackStance_ApplySupport(CalculateDual): root=%p "
             "w0=%08X w1=%08X flags=%08X unk14=%d unk18=%u unk1C=%u (n=%d)",
             root,
             w[0], w[1],
             root->flags,
             root->unk14,
             root->unk18,
             root->unk1C,
             sBasicLogCount + 1);
        ++sBasicLogCount;
    }

    // NOTE:
    //  * No damage pipeline call here anymore.
    //  * No write-back into the root.
    //  * This hook is now observation-only; the forecast pipeline is
    //    hosted inside MAP_BattleInfo_CalculateSimple.
}

// ---------------------------------------------------------------------
// HUD and skill hooks
// ---------------------------------------------------------------------
//
// HUD_Battle_HPGaugeUpdate
//
// Stage: HUD_BATTLE_HP.
//
// This hook currently just:
//   * Increments telemetry.
//   * Forwards directly to the vanilla draw routine.
//
// It is kept as an observation / future-overlay surface for in-battle HP
// gauges (e.g. barrier indicators, extra pips, etc.). v2 hygiene rule:
// no game-state changes from here; treat it as HUD-only.

void Hook_HUD_Battle_HPGaugeUpdate(void *hudContext,
                                   void *unit)
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
             battleContext,
             attacker,
             defender,
             skillIdOrFlags,
             sLogCount + 1);
        ++sLogCount;
    }

    HookContext &ctx = HookContext::GetCurrent();
    int result = ctx.OriginalFunction<int, void *, void *, void *, std::uint32_t>(
        battleContext, attacker, defender, skillIdOrFlags);

    // NEW: treat this as evidence that the attacker has the skill id
    // encoded in the low 16 bits of skillIdOrFlags.
    std::uint16_t skillId =
        static_cast<std::uint16_t>(skillIdOrFlags & 0xFFFFu);

    if (attacker != nullptr && skillId != 0)
    {
        std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(attacker);
        bool isHeapLike =
            (addr >= kHeapMinVA) && (addr <= kHeapMaxVA);

        if (isHeapLike)
        {
            Engine::Skills::OnUnitHasSkillObserved(attacker, skillId);

            static int sSkillLogCount = 0;
            if (sSkillLogCount < 32)
            {
                Logf("Hook_BTL_SkillEffect_Apply(skill): "
                     "unit=%p skill=0x%04X (n=%d)",
                     attacker,
                     static_cast<unsigned>(skillId),
                     sSkillLogCount + 1);
                ++sSkillLogCount;
            }
        }
    }

    return result;
}

// MAP_EquipSkillCalculator_Calculate
//
// Host: map__EquipSkillCalculator__Calculate @ 0x???????? (see na_v11.yml)
//
// Stage: EQUIP_SKILL_CONFIG.
//
// Behaviour (from RE so far):
//   * Computes a bitfield / small struct describing which equip skills are
//     active for a unit / battle context.
//   * The surrounding config uses the same 0x4000XXXX flag patterns we see
//     on BattleRoot::flags, with the low 16 bits looking like a skill id.
//
// SDK usage (v2 hygiene):
//   * This hook is **RE + skill-driver observation only**.
//   * We call the vanilla function first, then:
//       - Heuristically derive a "root-ish" struct at calc - 0xB4.
//       - If that looks heap-like, treat root[1] as a candidate Unit*
//         and root[0x10/4] as candidate 0x4000XXXX flags.
//       - Feed those flags into ObserveSkillDriverFromFlags(), which has
//         its own heap/bit-pattern guards.
//   * We do **not** write into `calc` or the derived root, and we do not
//     change equip-skill behaviour here. If the heuristic ever proves
//     wrong, we can safely tighten or even disable the driver bridge.
int Hook_MAP_EquipSkillCalculator_Calculate(void *calc,
                                            void *a1,
                                            void *a2,
                                            void *a3)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    // Telemetry
    std::size_t idx = IndexOf(HookId_MAP_EquipSkillCalculator_Calculate);
    gHookCount[idx]++;
    unsigned total = static_cast<unsigned>(gHookCount[idx]);

    HookContext &ctx = HookContext::GetCurrent();

    // Call the real function first so we don't disturb behaviour.
    int result = ctx.OriginalFunction<int, void *, void *, void *, void *>(
        calc, a1, a2, a3);

    // Guard: ignore clearly bogus calc pointers.
    if (calc == nullptr)
        return result;

    std::uintptr_t calcAddr = reinterpret_cast<std::uintptr_t>(calc);
    if (calcAddr < kHeapMinVA || calcAddr > kHeapMaxVA)
        return result;

    static int sLogCount = 0;
    if (sLogCount < 48)  // cap so logs stay sane
    {
        auto         *calcBytes  = reinterpret_cast<std::uint8_t *>(calc);
        std::uint8_t *calcBase8  = calcBytes;

        // Heuristic: in many cases we observed
        //   root ~= (BattleInfoSideRoot*)calc - 0xB4.
        auto *root = reinterpret_cast<std::uint32_t *>(calcBytes - 0xB4);

        void         *unit  = nullptr;
        std::uint32_t flags = 0;

        // Only treat the derived root as valid if it looks heap-like.
        std::uintptr_t rootAddr = reinterpret_cast<std::uintptr_t>(root);
        if (rootAddr >= kHeapMinVA && rootAddr <= kHeapMaxVA)
        {
            unit  = reinterpret_cast<void *>(root[1]);         // main unit ptr
            flags = root[0x10 / 4];                            // 0x10 word (0x4000xxxx)
        }

        // NEW: bridge any 0x4000XXXX-style flags into the skills engine.
        if (unit != nullptr && flags != 0u)
        {
            ObserveSkillDriverFromFlags(unit,
                                        flags,
                                        "MAP_EquipSkillCalc");
        }

        Logf("Hook_MAP_EquipSkillCalculator_Calculate: total=%u "
             "calc=%p root~=%p unit~=%p flags=%08X "
             "a1=%08X a2=%08X a3=%08X -> %d (n=%d)",
             total,
             calc,
             root,
             unit,
             flags,
             static_cast<std::uint32_t>(
                 reinterpret_cast<std::uintptr_t>(a1)),
             static_cast<std::uint32_t>(
                 reinterpret_cast<std::uintptr_t>(a2)),
             static_cast<std::uint32_t>(
                 reinterpret_cast<std::uintptr_t>(a3)),
             result,
             sLogCount + 1);

        // For the first few calls, also dump more of the calc subobject itself.
        if (sLogCount < 8)
        {
            // 1) 0x00..0x7C as 4-word rows (like RootDump does).
            for (int off = 0; off < 0x80; off += 0x10)
            {
                auto *row = reinterpret_cast<std::uint32_t *>(calcBase8 + off);
                Logf("  calc[+0x%02X] = %08X %08X %08X %08X",
                     off,
                     row[0], row[1], row[2], row[3]);
            }

            // 2) Explicit view of the bytes around 0x30..0x3F (equip flags region).
            std::uint8_t *flagsBase = calcBase8 + 0x30;
            Logf("  calc[0x30..0x3F] bytes = "
                 "%02X %02X %02X %02X %02X %02X %02X %02X "
                 "%02X %02X %02X %02X %02X %02X %02X %02X",
                 flagsBase[0],  flagsBase[1],  flagsBase[2],  flagsBase[3],
                 flagsBase[4],  flagsBase[5],  flagsBase[6],  flagsBase[7],
                 flagsBase[8],  flagsBase[9],  flagsBase[10], flagsBase[11],
                 flagsBase[12], flagsBase[13], flagsBase[14], flagsBase[15]);

            // 3) Candidate "bonus" fields around 0x70/0x74/0x78,
            //    which the assembly patch updates (e.g. +0x74).
            std::int32_t w70 = *reinterpret_cast<std::int32_t *>(calcBase8 + 0x70);
            std::int32_t w74 = *reinterpret_cast<std::int32_t *>(calcBase8 + 0x74);
            std::int32_t w78 = *reinterpret_cast<std::int32_t *>(calcBase8 + 0x78);

            Logf("  calc[+0x70..0x78] words = w70=%08X (%d) w74=%08X (%d) w78=%08X (%d)",
                 static_cast<std::uint32_t>(w70), w70,
                 static_cast<std::uint32_t>(w74), w74,
                 static_cast<std::uint32_t>(w78), w78);
        }

        ++sLogCount;
    }

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

    // Use the last turn side maintained in Hook_SEQ_TurnBegin.
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

        // NEW: ensure the skill-driven damage rules are registered
        // before any battles on this map.
        Engine::Skills::RegisterDamageRules();

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

    Fates::LevelUpPayload payload{};
    payload.unit  = reinterpret_cast<Fates::Unit *>(unitRaw);
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
        // Engine notification (map/turn aware).
        Engine::OnUnitSkillLearn(unitRaw,
                                 payload.skillId,
                                 payload.flags,
                                 result,
                                 gCurrentTurnSide);

        // Skills engine bridge: keep a simple per-unit skill table
        // that other engine modules can query.
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

// UNIT_HasSkillById (Unit__HasSkillById)
//
// Stage: SKILL_QUERY_GLOBAL.
//
// v2 hygiene:
//   * Treat this as a hot, global observation point for skill queries.
//   * Allowed: telemetry + tiny stats + bounded logging.
//   * Not allowed: changing the return value or doing heavy engine work.
int Hook_UNIT_HasSkillById(void        *unitRaw,
                           std::int16_t skillId)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    // Per-hook telemetry (shared with all other hooks).
    std::size_t idx = IndexOf(HookId_UNIT_HasSkillById);
    gHookCount[idx]++;

    HookContext &ctx = HookContext::GetCurrent();

    // Call vanilla exactly once.
    //
    // int Unit__HasSkillById(Unit* unit, int16_t skillId)
    int result = ctx.OriginalFunction<int, void *, std::int16_t>(
        unitRaw, skillId);

    // Global stats: treat this as a read-only skill-query observation.
    g_UnitHasSkillByIdStats.totalCalls++;
    if (result != 0)
        g_UnitHasSkillByIdStats.totalPositive++;
    else
        g_UnitHasSkillByIdStats.totalNegative++;

    // Optional lightweight logging for early RE; bounded so we don't
    // spam logs given how hot this path is.
    static int sLogCount = 0;
    if (sLogCount < 32)
    {
        std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(unitRaw);
        bool isHeapLike =
            (addr >= kHeapMinVA) && (addr <= kHeapMaxVA);

        Logf("Hook_UNIT_HasSkillById: unit=%p heap=%d skill=0x%04X -> %d "
             "(calls=%u pos=%u neg=%u, n=%d)",
             unitRaw,
             isHeapLike ? 1 : 0,
             static_cast<unsigned>(
                 static_cast<std::uint16_t>(skillId)),
             result,
             g_UnitHasSkillByIdStats.totalCalls,
             g_UnitHasSkillByIdStats.totalPositive,
             g_UnitHasSkillByIdStats.totalNegative,
             sLogCount + 1);
        ++sLogCount;
    }

    // IMPORTANT: pure pass-through of the vanilla result.
    // No Engine::Skills bridge here; we keep this hook cheap and
    // let the dedicated driver surfaces (BTL_FinalDamage_Pre, etc.)
    // populate the per-map skill tables.
    return result;
}

// Hook_MAP_BattleInfoSide_CalcEfficacy
//
// Host: map__BattleInfo__Side__CalculateEfficacy @ 0x0034899C
//
// Stage: PREVIEW_DETAIL / EFFICACY.
//   * r0 = BattleAttackEntry* / side candidate.
//   * r1..r3 = peer context (opponent entry/unit + extra args).
//
// Vanilla role (from decomp):
//   * Initialises a static config block at iRam00348cfc:
//       - Several equip-skill IDs.
//       - ItemSkill-derived bitfields.
//       - JobCategory-based masks.
//   * Sets bits in the side flags at +0x28 (e.g. 0x200, 0x400) when:
//       - The weapon is effective versus the target's JobCategory.
//       - Item/skill/subkind combinations match (physical/magic/beast/etc.).
//
// SDK v2 hygiene:
//   * This hook is RE/telemetry-only. We snapshot the side before and after
//     vanilla efficacy logic to understand how flags at +0x28 evolve.
//   * MUST NOT mutate fields or alter control flow. Any future “effective vs X”
//     extensions should *augment* these flags elsewhere, not replace this logic.
void Hook_MAP_BattleInfoSide_CalcEfficacy(void *a0,
                                          void *a1,
                                          void *a2,
                                          void *a3)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    // Telemetry: count invocations.
    std::size_t idx = IndexOf(HookId_MAP_BattleInfoSide_CalcEfficacy);
    gHookCount[idx]++;

    void *side = a0; // BattleAttackEntry* / BattleInfo::Side* candidate.

    static int sPreLogCount  = 0;
    static int sPostLogCount = 0;

    // Snapshot before vanilla logic runs (RE-only).
    if (side != nullptr && sPreLogCount < 16)
    {
        DumpBattleInfoSide(side, "eff_pre", sPreLogCount);
        ++sPreLogCount;
    }

    // Call the real CalculateEfficacy with all original register args.
    HookContext &ctx = HookContext::GetCurrent();
    ctx.OriginalFunction<void, void *, void *, void *, void *>(
        a0, a1, a2, a3);

    // Snapshot after vanilla logic (still RE-only).
    if (side != nullptr && sPostLogCount < 16)
    {
        DumpBattleInfoSide(side, "eff_post", sPostLogCount);
        ++sPostLogCount;
    }
}


// Hook_MAP_BattleCalculator_CalculateAttack
//
// Host: map__BattleCalculator__CalculateAttack @ 0x00361F70
//
// Stage: DAMAGE_MATH / FINAL_HP_DRIVER.
//   * r0 = BattleResultRoot_t* ("calculator" / result root).
//   * r1 = slotIndexOrKind, r2/r3 = call-site noise (ignored in vanilla).
//
// Vanilla role (from decomp):
//   * Lazily initialises a static config at iRam00362278 using
//     __cxa_guard_acquire:
//       - Several equip-skill IDs (shorts at +4, +6, +8, ...).
//       - ItemSkill-derived bitfields at +0x170..+0x176 etc.
//   * Derives a candidate result side:
//       resultSide = *calculator + slotIndexOrKind * 0x200;
//   * Applies item/flag gates (unit__Item__ToData(), flags & 0x100 / 0x4000).
//   * Uses CAND_MapBattleCalc_* helpers plus the config shorts to pick a
//     sideIndexB "rule", sometimes allocating an extra record describing
//     (slotIndexOrKind, sideIndexB).
//   * Calls BTL_FinalDamage_Pre(resultRoot, slotIndexOrKind, sideIndexB),
//     sometimes in a loop (up to 5 passes) or in a chained two-pass path.
//
// SDK v2 hygiene:
//   * This is a late-stage, high-risk FINAL_HP driver. We treat it as an
//     observation / validation surface only:
//       - Allowed: logging, dumping the BattleResultRoot and associated
//         candidates, building DamageContext for FINAL_HP sanity checks.
//       - Not allowed: changing `calc`, `slotIndexOrKind`, or the candidate
//         selection / BTL_FinalDamage_Pre call pattern without a fully
//         mirrored engine-side implementation.
//   * The body below only logs and forwards to the vanilla function.
int Hook_MAP_BattleCalculator_CalculateAttack(void *calc,
                                              void *a1,
                                              void *a2,
                                              void *a3)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    // Telemetry
    std::size_t idx = IndexOf(HookId_MAP_BattleCalculator_CalculateAttack);
    gHookCount[idx]++;

    // Try to peel a BattleRoot* off the calc, using our minimal struct.
    BattleRoot *root = GetBattleRoot(calc);

    static int sPreLogCount  = 0;
    static int sPostLogCount = 0;

    // -----------------------
    // Pre-call snapshot
    // -----------------------
    if (calc != nullptr && sPreLogCount < 16)
    {
        std::uintptr_t calcAddr = reinterpret_cast<std::uintptr_t>(calc);
        if (calcAddr >= kHeapMinVA && calcAddr <= kHeapMaxVA)
        {
            Logf("Hook_MAP_BattleCalculator_CalculateAttack(pre): "
                 "calc=%p root=%p a1=%p a2=%p a3=%p (n=%d)",
                 calc,
                 root,
                 a1,
                 a2,
                 a3,
                 sPreLogCount + 1);

            // Treat calc as a candidate "result-root-ish" struct and dump a
            // small window (DumpBattleInfoSide is tolerant and only reads a
            // narrow span; we only call it on heap-like pointers).
            DumpBattleInfoSide(calc, "atk_pre", sPreLogCount);

            // If we have a plausible root, piggyback a root dump for RE.
            if (root != nullptr)
            {
                DebugDumpBattleRoot(root, /*tag=*/5, sPreLogCount);
            }

            ++sPreLogCount;
        }
    }

    // Call the real CalculateAttack with all original args.
    HookContext &ctx = HookContext::GetCurrent();
    int result = ctx.OriginalFunction<int, void *, void *, void *, void *>(
        calc, a1, a2, a3);

    // -----------------------
    // Post-call snapshot
    // -----------------------
    if (calc != nullptr && sPostLogCount < 16)
    {
        std::uintptr_t calcAddr = reinterpret_cast<std::uintptr_t>(calc);
        if (calcAddr >= kHeapMinVA && calcAddr <= kHeapMaxVA)
        {
            DumpBattleInfoSide(calc, "atk_post", sPostLogCount);
            ++sPostLogCount;
        }
    }

    return result;
}


// HUD_HpWindow_Draw
//
// Stage: HUD_HP_WINDOW.
//
// Observation-only hook on the HP popup window. We:
//   * Log the first 0x80 bytes a few times for struct RE.
//   * Forward to the vanilla draw routine unchanged.
//
// No HP logic or damage is allowed here; it’s a pure UI surface.
void Hook_HUD_HpWindow_Draw(void *hpWindow,
                            void *a1,
                            void *a2,
                            void *a3)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    std::size_t idx = IndexOf(HookId_HUD_HpWindow_Draw);
    gHookCount[idx]++;

    if (hpWindow != nullptr)
    {
        auto *words = reinterpret_cast<std::uint32_t *>(hpWindow);

        // Keep logs bounded: 0..7 calls total (each prints 2 lines).
        static int sHudLogCount = 0;
        if (sHudLogCount < 8)
        {
            // Dump the first 0x40 bytes (16 words).
            Logf("HUD_HpWindow_Draw[%d]: wnd=%p [00..3C]={%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,"
                 "%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X}",
                 sHudLogCount,
                 hpWindow,
                 words[0],  words[1],  words[2],  words[3],
                 words[4],  words[5],  words[6],  words[7],
                 words[8],  words[9],  words[10], words[11],
                 words[12], words[13], words[14], words[15]);

            // Dump the next 0x40 bytes (16 more words), if the struct is that big.
            Logf("HUD_HpWindow_Draw[%d]: wnd=%p [40..7C]={%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,"
                 "%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X}",
                 sHudLogCount,
                 hpWindow,
                 words[16], words[17], words[18], words[19],
                 words[20], words[21], words[22], words[23],
                 words[24], words[25], words[26], words[27],
                 words[28], words[29], words[30], words[31]);

            ++sHudLogCount;
        }
    }

    // Call the real draw routine with the original arguments.
    HookContext &ctx = HookContext::GetCurrent();
    ctx.OriginalFunction<void, void *, void *, void *, void *>(
        hpWindow, a1, a2, a3);
}

// Hook_MAP_BattleInfo_CalculateSimple
//
// Host: map__BattleInfo__CalculateSimple @ 0x00347C3C
//
// Stage: PREVIEW_SIMPLE.
//   * r0 = BattleInfo* (root; entries[0] is at offset 0).
//
// Vanilla role (from latest decomp):
//   * Early-out if info->flags_804 has 0x10 set or entries[0].flags has 0x40.
//   * Loops sideIndex = 0..3, using a 0x200-byte stride:
//       side      = (BattleInfoSimpleSide_t *)(info->entries + sideIndex);
//       battleCtx = info->entries + pairedIndex;
//   * For each side, calls:
//       - map__BattleInfo__Side__GetSimplePower  -> simplePower_2C
//       - map__BattleInfo__Side__GetSimpleHit    -> simpleHitRaw_30 & gate
//       - map__BattleInfo__Side__GetSimpleTimes  -> simpleTimes_3C
//   * Rebuilds a clamped 0..100 hit rate into simpleHit_0to100_34 using
//     hit stats on the side and opponent, and applies item/skill masks
//     from gBattleInfoGlobals (bitfieldA_lo/hi, bitfieldB_lo/hi).
//   * Computes simpleAuxRate_38 (typically 300 or 400, sometimes halved)
//     and a simpleTimesDetailFlag_40 via Unit__HasSkillById and a cached
//     equip-skill ID.
//
// Confirmed simple-side view (from Ghidra scripts):
//   BattleInfoSimpleSide_t (length 0x28) at a fixed offset in the per-side
//   entry, with:
//     +0x00 : slotKind_orSide_00 (u8)
//     +0x04 : unitPtr_04         (Unit*)
//     +0x10 : simplePower_2C     (int)
//     +0x14 : simpleHitRaw_30    (int)
//     +0x18 : simpleHit_0to100_34
//     +0x1C : simpleAuxRate_38
//     +0x20 : simpleTimes_3C
//     +0x24 : simpleTimesDetailFlag_40 (u8)
//
// SDK v2 hygiene:
//   * This is the canonical simple forecast aggregation point for the
//     FORECAST origin of the damage engine.
//   * Our hook:
//       1) Calls vanilla once to fully populate the simple fields.
//       2) Logs per-side simple values + unit pointers for RE.
//       3) Builds a DamageContext for side 0 (attacker) and runs the shared
//          Combat pipeline with origin = Forecast.
//       4) Optionally mirrors a changed TOTAL back into per-hit/hit-count,
//          but ONLY when kForecastWritebackEnabled is true.
//   * By default (kForecastWritebackEnabled == false) this hook is purely
//     observational and must not change HUD or BattleRoot values.
void Hook_MAP_BattleInfo_CalculateSimple(void *battleInfoRaw)
{
    using namespace Fates;
    using CTRPluginFramework::HookContext;

    // Telemetry
    std::size_t idx = IndexOf(HookId_MAP_BattleInfo_CalculateSimple);
    gHookCount[idx]++;

    HookContext &ctx = HookContext::GetCurrent();

    // Let the game compute the vanilla simple forecast first.
    ctx.OriginalFunction<void, void *>(battleInfoRaw);

    if (battleInfoRaw == nullptr)
        return;

    std::uintptr_t baseAddr =
        reinterpret_cast<std::uintptr_t>(battleInfoRaw);

    // Guard against stack-ish / nonsense pointers.
    if (baseAddr < kHeapMinVA || baseAddr > kHeapMaxVA)
        return;

    auto *base = reinterpret_cast<std::uint8_t *>(battleInfoRaw);

    // Side stride confirmed as 0x200 by CalculateSimple decomp.
    auto getSideBase = [base](int sideIndex) -> std::uint8_t * {
        return base + (sideIndex * 0x200);
    };

    std::uint8_t *sidePtr[4] = {
        getSideBase(0),
        getSideBase(1),
        getSideBase(2),
        getSideBase(3)
    };

    // -----------------------------------------------------------------
    // RE logging: simple stats + unit pointers per side.
    // This is purely observational and helps us later define
    // Attacker / Defender / Support roles in the engine.
    // -----------------------------------------------------------------
    static int sSideRoleLogCount = 0;
    if (sSideRoleLogCount < 16)
    {
        for (int i = 0; i < 4; ++i)
        {
            std::uint8_t *side = sidePtr[i];
            if (side == nullptr)
                continue;

            // slotKind_orSide_00 lives at +0x00 in the simple view.
            std::uint8_t slotKind = side[0];

            // Uses the Ghidra-confirmed +0x04 offset under the hood
            // and guards against non-heap unit pointers.
            void *unit = GetUnitFromSimpleSide(side);

            // Simple Power/Hit/Times come from the simple block we
            // already verified via earlier tests; these offsets are
            // the same ones we use for the pipeline below.
            std::int32_t simplePower =
                *reinterpret_cast<std::int32_t *>(side + 0x2C);
            std::int32_t simpleTimes =
                *reinterpret_cast<std::int32_t *>(side + 0x3C);

            Logf("BattleInfoSimple(sideRole): idx=%d slotKind=%u side=%p "
                 "unit=%p pow=%d times=%d",
                 i,
                 static_cast<unsigned>(slotKind),
                 side,
                 unit,
                 simplePower,
                 simpleTimes);
        }

        ++sSideRoleLogCount;
    }

    // Lightweight logging: dump simplePower/hit/times/effect flags for
    // the first couple of calls so we can correlate with HUD readings.
    static int sSimpleLogCount = 0;
    if (sSimpleLogCount < 16)
    {
        for (int i = 0; i < 2; ++i)
        {
            std::uint8_t *side = sidePtr[i];
            if (side == nullptr)
                continue;

            std::int32_t simplePower   =
                *reinterpret_cast<std::int32_t *>(side + 0x2C);
            std::int32_t simpleHitRaw  =
                *reinterpret_cast<std::int32_t *>(side + 0x30);
            std::int32_t simpleHitRate =
                *reinterpret_cast<std::int32_t *>(side + 0x34);
            std::int32_t auxRate       =
                *reinterpret_cast<std::int32_t *>(side + 0x38);
            std::int32_t simpleTimes   =
                *reinterpret_cast<std::int32_t *>(side + 0x3C);
            std::uint8_t timesFlag     = *(side + 0x40);

            Logf("BattleInfoSimple(pre): side=%d pow=%d hitRaw=%d hitRate=%d "
                 "aux=%d times=%d timesFlag=%u",
                 i,
                 simplePower,
                 simpleHitRaw,
                 simpleHitRate,
                 auxRate,
                 simpleTimes,
                 static_cast<unsigned>(timesFlag));
        }

        ++sSimpleLogCount;
    }

    // -----------------------------------------------------------------
    // Forecast-side damage pipeline for side 0.
    //
    // We:
    //   * Read simplePower/times for side 0 (e.g. 3 x 2).
    //   * Build TOTAL damage = simplePower * times (e.g. 6).
    //   * Resolve BattleRoot + attacker/defender units (best-effort).
    //   * Bridge BattleRoot::flags into the skills engine for observation.
    //   * Call Engine::Combat::ApplyDamageModifiersEx(origin = Forecast).
    //   * If the total changes and kForecastWritebackEnabled is true, write
    //     back an updated per-hit/hits to keep HUD and root in sync.
    // -----------------------------------------------------------------

    // For now we only drive the pipeline for side 0 (main forecast lane).
    std::uint8_t *side0 = sidePtr[0];
    if (side0 == nullptr)
        return;

    // In almost all battle contexts, side 1 is the "other" main side
    // in the simple forecast (defender from side 0's perspective).
    std::uint8_t *side1 = sidePtr[1];

    // Vanilla simple forecast values.
    std::int32_t simplePower =
        *reinterpret_cast<std::int32_t *>(side0 + 0x2C); // "Atk"
    std::int32_t simpleTimes =
        *reinterpret_cast<std::int32_t *>(side0 + 0x3C); // "x2" etc.

    int hits      = (simpleTimes > 0) ? simpleTimes : 1;
    int perHit    = simplePower;
    int baseTotal = perHit * hits;

    if (baseTotal <= 0)
        return; // nothing to do

    // BattleRoot heuristic from the side pointer.
    BattleRoot *root = GetRootFromBattleInfoSide(side0);

    // Best-effort attacker/defender resolution from the simple sides.
    // If the Unit* layout doesn't match or the pointer looks non-heap,
    // GetUnitFromSimpleSide() will return nullptr and we fall back to
    // root->mainUnit for the attacker.
    void *attackerRaw = GetUnitFromSimpleSide(side0);
    void *defenderRaw = GetUnitFromSimpleSide(side1);

    // Fallback: if we fail to find an attacker unit from the side, fall
    // back to root->mainUnit so we at least preserve old behaviour.
    if (attackerRaw == nullptr && root != nullptr && root->mainUnit != nullptr)
        attackerRaw = static_cast<void *>(root->mainUnit);

    // Optional: light logging to confirm unit pointers now that the
    // simple-side unit offset is wired up.
    static int sSideUnitLogCount = 0;
    if (sSideUnitLogCount < 32)
    {
        Logf("BattleInfoSimple(units): side0=%p side1=%p atkUnit=%p defUnit=%p",
             side0,
             side1,
             attackerRaw,
             defenderRaw);
        ++sSideUnitLogCount;
    }

    // Forecast-side skill driver bridge from BattleRoot::flags into the
    // skill engine, plus logging of what we see.
    if (root != nullptr && attackerRaw != nullptr)
    {
        std::uint32_t flags = root->flags;

        static int sRootLogCount = 0;
        if (sRootLogCount < 16)
        {
            std::uint16_t driverSkillId =
                static_cast<std::uint16_t>(flags & 0xFFFFu);

            Logf("MAP_BattleInfo_CalculateSimple(root): root=%p unit=%p "
                 "flags=%08X driverSkill=0x%04X baseTotal=%d hits=%d perHit=%d",
                 root,
                 attackerRaw,
                 flags,
                 static_cast<unsigned>(driverSkillId),
                 baseTotal,
                 hits,
                 perHit);
            ++sRootLogCount;
        }

        // Shared helper: make sure the skills engine sees this driver.
        ObserveSkillDriverFromFlags(attackerRaw,
                                    flags,
                                    "BattleInfoSimple");
    }

    // Normal path: run the shared damage pipeline.
    int modTotal = Engine::Combat::ApplyDamageModifiersEx(
        /*root=*/root,
        /*calc=*/nullptr,
        attackerRaw,
        defenderRaw,
        baseTotal,
        Engine::Combat::DamageOrigin::Forecast,        // forecast host
        /*isFinalPass=*/true,                          // canonical preview
        /*hpBefore=*/0,
        /*hpAfter=*/0,
        /*forecastTotal=*/baseTotal,
        /*numHits=*/hits,
        /*dmgPerHit=*/perHit);

    // Direct pipeline log so we can see if any rule changed the total.
    static int sPipelineLogCount = 0;
    if (sPipelineLogCount < 32)
    {
        Logf("BattleInfoSimple(pipeline): baseTotal=%d -> modTotal=%d "
             "(hits=%d perHit=%d root=%p atk=%p def=%p)",
             baseTotal,
             modTotal,
             hits,
             perHit,
             root,
             attackerRaw,
             defenderRaw);
        ++sPipelineLogCount;
    }

    if (modTotal == baseTotal)
        return; // pipeline was a no-op, nothing to write back

    // v2 hygiene: until we have a fully mapped FINAL_HP hook wired through the
    // same damage engine, keep MAP_BattleInfo_CalculateSimple strictly
    // observational. This guard must remain false in public / default builds.
    if (!kForecastWritebackEnabled)
        return;

    if (modTotal < 0)
        modTotal = 0;

    int newHits = hits;
    if (newHits <= 0)
        newHits = 1;

    // Simple uniform-per-hit assumption for now.
    int newPer = (modTotal + (newHits - 1)) / newHits; // round up
    if (newPer < 0)
        newPer = 0;
    if (newPer > 200)
        newPer = 200;

    // Write back into the simple struct powering the HUD.
    *reinterpret_cast<std::int32_t *>(side0 + 0x2C) = newPer;
    *reinterpret_cast<std::int32_t *>(side0 + 0x3C) = newHits;

    // If we also got a valid BattleRoot, mirror per-hit and hit count into
    // the shared root so later stages (AttackStance, BTL_FinalDamage_Pre)
    // see the same numbers.
    if (root != nullptr)
    {
        auto *rootWords = reinterpret_cast<std::int32_t *>(root);

        // Offsets were confirmed by earlier BattleRoot mapping scripts.
        rootWords[0x3C / 4] = newHits; // numHits
        rootWords[0x50 / 4] = newPer;  // dmgPerHit (main)
        rootWords[0x74 / 4] = newPer;  // per-hit snapshot for this side
    }

    static int sSimpleWriteLogCount = 0;
    if (sSimpleWriteLogCount < 16)
    {
        Logf("MAP_BattleInfo_CalculateSimple(write): "
             "baseTotal=%d modTotal=%d hits=%d perHit_old=%d perHit_new=%d",
             baseTotal,
             modTotal,
             newHits,
             perHit,
             newPer);
        ++sSimpleWriteLogCount;
    }
}

} // extern "C"
