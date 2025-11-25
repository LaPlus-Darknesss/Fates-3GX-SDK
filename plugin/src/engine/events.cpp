// engine/events.cpp
//
// First implementation layer for engine/events.hpp. These functions
// are called directly from hook stubs in hooks_handlers.cpp.
//
// Responsibilities:
//   1) Build small, stable context snapshots (MapContext, TurnContext,
//      KillContext, etc.) from core/runtime.hpp state.
//   2) Emit structured debug logs (with caps where needed).
//   3) Dispatch the contexts into the lightweight event bus in
//      engine/bus.cpp.
//
// Later, separate engine subsystems (HP engine, skill engine,
// UI overlays, etc.) will register handlers
// with the bus instead of touching hooks directly.

#include <unordered_map>

#include "engine/events.hpp"
#include "engine/bus.hpp"
#include "util/debug_log.hpp"

namespace Fates {
namespace Engine {

// Small per-map HP tracker used to derive delta-based HP events
// from raw UNIT_UpdateCloneHP sync calls.
// Keys are raw Unit* pointers (identity-based). The map is cleared on
// each Engine::OnMapBegin so HP deltas don't leak across maps.

struct HpTracker
{
    std::unordered_map<void*, int> lastHp;
};

static HpTracker gHpTracker;

// Helper: snapshot gMapState into a MapContext.
static MapContext BuildMapContext()
{
    MapContext ctx{};
    ctx.seqRoot     = gMapState.seqRoot;
    ctx.generation  = gMapState.generation;
    ctx.startSide   = gMapState.startSide;
    ctx.currentSide = gMapState.currentSide;
    ctx.totalTurns  = gMapState.totalTurns;
    ctx.killEvents  = gMapState.killEvents;
    return ctx;
}

// Helper: build TurnContext using current map state + side.
static TurnContext BuildTurnContext(TurnSide side)
{
    TurnContext tc{};
    tc.map  = BuildMapContext();
    tc.side = side;

    // Side index 0..3 maps directly to gMapState.turnCount[]
    int idx = static_cast<int>(side);
    if (0 <= idx && idx < 4)
        tc.sideTurnIndex = gMapState.turnCount[idx];
    else
        tc.sideTurnIndex = 0;

    return tc;
}

void OnMapBegin(void *seqRoot, TurnSide side)
{
    // New map: clear the per-map HP tracker so no mixing deltas
    // across different battles.
    gHpTracker.lastHp.clear();
	
	// NOTE: Hook_SEQ_MapStart calls MapLife_OnNewMap() *before* this,
    // so BuildMapContext() already sees the new generation and reset
    // per-map counters.
    MapContext mc = BuildMapContext();

    Logf("Engine::OnMapBegin: seq=%p gen=%u start=%s current=%s totalTurns=%u",
         seqRoot,
         static_cast<unsigned>(mc.generation),
         TurnSideToString(mc.startSide),
         TurnSideToString(mc.currentSide),
         static_cast<unsigned>(mc.totalTurns));

    // For now, ignore the 'side' parameter (it should match mc.startSide).
    (void)side;

    // Fan out to any registered listeners.
    DispatchMapBegin(mc);
}

void OnMapEnd(void *seqRoot, TurnSide side)
{
    MapContext mc = BuildMapContext();

    Logf("Engine::OnMapEnd: seq=%p gen=%u side=%s totalTurns=%u kills=%u",
         seqRoot,
         static_cast<unsigned>(mc.generation),
         TurnSideToString(side),
         static_cast<unsigned>(mc.totalTurns),
         static_cast<unsigned>(mc.killEvents));

    DispatchMapEnd(mc);
}

void OnTurnBegin(TurnSide side)
{
    TurnContext tc = BuildTurnContext(side);

    Logf("Engine::OnTurnBegin: gen=%u side=%s sideTurn=%u totalTurns=%u",
         static_cast<unsigned>(tc.map.generation),
         TurnSideToString(side),
         static_cast<unsigned>(tc.sideTurnIndex),
         static_cast<unsigned>(tc.map.totalTurns));

    DispatchTurnBegin(tc);
}

void OnTurnEnd(TurnSide side, void *seqMaybe)
{
    TurnContext tc = BuildTurnContext(side);

    Logf("Engine::OnTurnEnd: seq=%p gen=%u side=%s sideTurn=%u totalTurns=%u",
         seqMaybe,
         static_cast<unsigned>(tc.map.generation),
         TurnSideToString(side),
         static_cast<unsigned>(tc.sideTurnIndex),
         static_cast<unsigned>(tc.map.totalTurns));

    DispatchTurnEnd(tc);
}

void OnKill(const KillEvent &ev, TurnSide side)
{
    TurnContext tc = BuildTurnContext(side);
    MapContext  mc = tc.map;

    KillContext kc{};
    kc.core = ev;
    kc.map  = mc;
    kc.turn = tc;

    Logf("Engine::OnKill: seq=%p flags=0x%08X dead0=%p dead1=%p "
         "gen=%u side=%s totalTurns=%u mapKills=%u sideTurn=%u",
         ev.seq,
         ev.flags,
         ev.dead0,
         ev.dead1,
         static_cast<unsigned>(mc.generation),
         TurnSideToString(side),
         static_cast<unsigned>(mc.totalTurns),
         static_cast<unsigned>(mc.killEvents),
         static_cast<unsigned>(tc.sideTurnIndex));

    DispatchKill(kc);
}

// ---------------------------------------------------------------------
// RNG + unit meta events
// ---------------------------------------------------------------------

void OnRngCall(void *state,
               std::uint32_t raw,
               std::uint32_t bound,
               std::uint32_t result)
{
    MapContext  mc = BuildMapContext();
    TurnContext tc = BuildTurnContext(mc.currentSide);

    RngContext rc{};
    rc.map    = mc;
    rc.turn   = tc;
    rc.state  = state;
    rc.raw    = raw;
    rc.bound  = bound;
    rc.result = result;

    // Cap logging so performance does not die, but:
    //  - reset the cap per map (generation)
    //  - only log while a map is actually active, so menus/etc. don't
    //    burn all 64 lines before gameplay starts.
    static std::uint32_t sLastGeneration = 0;
    static int           sLogCount       = 0;

    if (mc.generation != sLastGeneration)
    {
        sLastGeneration = mc.generation;
        sLogCount       = 0;
    }

    if (gMapState.mapActive && sLogCount < 64)
    {
        Logf("Engine::OnRngCall: state=%p raw=%08X bound=%u -> %u "
             "gen=%u side=%s sideTurn=%u totalTurns=%u (n=%d)",
             state,
             raw,
             static_cast<unsigned>(bound),
             static_cast<unsigned>(result),
             static_cast<unsigned>(mc.generation),
             TurnSideToString(tc.side),
             static_cast<unsigned>(tc.sideTurnIndex),
             static_cast<unsigned>(mc.totalTurns),
             sLogCount + 1);
        ++sLogCount;
    }

    // Fan out to RNG listeners (stats module, etc.).
    DispatchRngCall(rc);
}

void OnHitCalc(int baseRate,
               int result)
{
    // Use the current turn side if a map is active; otherwise fall
    // back to Unknown (e.g. menu/arena edge cases).
    TurnSide side =
        gMapState.mapActive ? gCurrentTurnSide : TurnSide::Unknown;

    TurnContext tc = BuildTurnContext(side);
    MapContext  mc = tc.map;

    HitCalcContext ctx{};
    ctx.map      = mc;
    ctx.turn     = tc;
    ctx.baseRate = baseRate;
    ctx.result   = result;

    // Lightweight log with a cap so we don't fry the log file.
    static std::uint32_t sLastGeneration = 0;
    static int           sLogCount       = 0;

    if (mc.generation != sLastGeneration)
    {
        sLastGeneration = mc.generation;
        sLogCount       = 0;
    }

    if (gMapState.mapActive && sLogCount < 128)
    {
        Logf("Engine::OnHitCalc: base=%d -> result=%d "
             "gen=%u side=%s sideTurn=%u totalTurns=%u (n=%d)",
             baseRate,
             result,
             static_cast<unsigned>(mc.generation),
             TurnSideToString(side),
             static_cast<unsigned>(tc.sideTurnIndex),
             static_cast<unsigned>(mc.totalTurns),
             sLogCount + 1);
        ++sLogCount;
    }

    // Fan out through the bus so modules can gather hit stats etc.
    DispatchHitCalc(ctx);
}

// Canonical HP sync driver. Called from Hook_UNIT_UpdateCloneHP
// after the game's own logic has written the unit's HP. Track
// the last seen HP per unit and emit an HpChange event when we
// detect a delta.
//
// NOTE: This is now the *only* place that should synthesize
// Engine::OnHpChange() calls. All HP-change logic should hang off
// the bus via DispatchHpChange(), not directly mutate in hooks.
void OnUnitHpSync(void *unit, int newHp)

{
    if (unit == nullptr)
        return;

    auto &map = gHpTracker.lastHp;

    int prev = -1;
    auto it = map.find(unit);
    if (it != map.end())
        prev = it->second;

    // Update the stored HP for this unit.
    map[unit] = newHp;

    // First time unit has been seen, or no change? Don't emit anything.
    if (prev < 0 || prev == newHp)
        return;

    int delta = prev - newHp;  // >0 damage, <0 heal

    // OPTIONAL: extra diagnostics, capped, and gated behind HP debug toggle.
    static std::uint32_t sLastGeneration = 0;
    static int           sHpSyncLogCount = 0;

    if (gMapState.generation != sLastGeneration)
    {
        sLastGeneration   = gMapState.generation;
        sHpSyncLogCount   = 0;
    }

    if (gHpApplyLogEnabled && sHpSyncLogCount < 64)
    {
        Logf("Engine::OnUnitHpSync: unit=%p prev=%d new=%d delta=%d mapActive=%d "
             "(gen=%u, n=%d)",
             unit,
             prev,
             newHp,
             delta,
             gMapState.mapActive ? 1 : 0,
             static_cast<unsigned>(gMapState.generation),
             sHpSyncLogCount + 1);
        ++sHpSyncLogCount;
    }

    TurnSide side =
        gMapState.mapActive ? gCurrentTurnSide : TurnSide::Unknown;

    OnHpChange(
        /*sourceUnit=*/nullptr,
        /*targetUnit=*/unit,
        /*amount=*/delta,
        /*flags=*/0u,
        /*context=*/nullptr,
        /*side=*/side);
}

void OnHpChange(void *sourceUnit,
                void *targetUnit,
                int  amount,
                std::uint32_t flags,
                void *context,
                TurnSide side)
{
    // Build the usual map/turn snapshots.
    TurnContext tc = BuildTurnContext(side);
    MapContext  mc = tc.map;

    // Fill the local HP event.
    HpEvent ev{};
    ev.source  = UnitHandle(sourceUnit);
    ev.target  = UnitHandle(targetUnit);
    ev.amount  = amount;   // >0 damage, <0 heal
    ev.flags   = flags;    // cause bits (battle, terrain, poison, skill, etc.)
    ev.context = context;  // e.g. seq pointer, battle root, or other proc

    // Wrap into a full context for the bus.
    HpChangeContext hc{};
    hc.core = ev;
    hc.map  = mc;
    hc.turn = tc;

    // Lightweight log with a cap, reset per map generation.
    static std::uint32_t sLastGeneration = 0;
    static int           sLogCount       = 0;

    if (mc.generation != sLastGeneration)
    {
        sLastGeneration = mc.generation;
        sLogCount       = 0;
    }

    if (gHpApplyLogEnabled && sLogCount < 128)
    {
        Logf("Engine::OnHpChange: src=%p tgt=%p amt=%d flags=0x%08X "
             "gen=%u side=%s sideTurn=%u totalTurns=%u (n=%d)",
             ev.source.Raw(),
             ev.target.Raw(),
             amount,
             static_cast<unsigned>(flags),
             static_cast<unsigned>(mc.generation),
             TurnSideToString(side),
             static_cast<unsigned>(tc.sideTurnIndex),
             static_cast<unsigned>(tc.map.totalTurns),
             sLogCount + 1);
        ++sLogCount;
    }

    // Fan out to future HP listeners. This is the single canonical
    // HP-change dispatcher; hooks should never call bus dispatch
    // functions directly.
    DispatchHpChange(hc);
}

void OnUnitLevelUp(void *unit,
                   std::uint8_t level,
                   TurnSide side)
{
    TurnContext tc = BuildTurnContext(side);
    MapContext  mc = tc.map;

    LevelUpContext ctx{};
    ctx.map   = mc;
    ctx.turn  = tc;
    ctx.unit  = UnitHandle(unit);
    ctx.level = level;

    Logf("Engine::OnUnitLevelUp: unit=%p level=%u "
         "gen=%u side=%s sideTurn=%u totalTurns=%u",
         ctx.unit.Raw(),
         static_cast<unsigned>(level),
         static_cast<unsigned>(mc.generation),
         TurnSideToString(side),
         static_cast<unsigned>(tc.sideTurnIndex),
         static_cast<unsigned>(tc.map.totalTurns));

    DispatchLevelUp(ctx);
}

void OnUnitSkillLearn(void *unit,
                      std::uint16_t skillId,
                      std::uint16_t flags,
                      int result,
                      TurnSide side)
{
    TurnContext tc = BuildTurnContext(side);
    MapContext  mc = tc.map;

    SkillLearnContext ctx{};
    ctx.map     = mc;
    ctx.turn    = tc;
    ctx.unit    = UnitHandle(unit);
    ctx.skillId = skillId;
    ctx.flags   = flags;
    ctx.result  = result;

    Logf("Engine::OnUnitSkillLearn: unit=%p skill=0x%04X flags=0x%04X result=%d "
         "gen=%u side=%s sideTurn=%u totalTurns=%u",
         ctx.unit.Raw(),
         static_cast<unsigned>(skillId),
         static_cast<unsigned>(flags),
         result,
         static_cast<unsigned>(mc.generation),
         TurnSideToString(side),
         static_cast<unsigned>(tc.sideTurnIndex),
         static_cast<unsigned>(tc.map.totalTurns));

    DispatchSkillLearn(ctx);
}

void OnItemGain(void *seqHelper,
                void *unit,
                void *itemArg,
                void *modeOrCtx,
                int   result,
                TurnSide side)
{
    TurnContext tc = BuildTurnContext(side);
    MapContext  mc = tc.map;

    ItemGainContext ctx{};
    ctx.map       = mc;
    ctx.turn      = tc;
    ctx.seq       = seqHelper;
    ctx.unit      = UnitHandle(unit);
    ctx.itemArg   = itemArg;
    ctx.modeOrCtx = modeOrCtx;
    ctx.result    = result;

    Logf("Engine::OnItemGain: seq=%p unit=%p itemArg=%p mode=%p result=%d "
         "gen=%u side=%s sideTurn=%u totalTurns=%u",
         seqHelper,
         ctx.unit.Raw(),
         itemArg,
         modeOrCtx,
         result,
         static_cast<unsigned>(mc.generation),
         TurnSideToString(side),
         static_cast<unsigned>(tc.sideTurnIndex),
         static_cast<unsigned>(tc.map.totalTurns));

    DispatchItemGain(ctx);
}

void OnActionEnd(void *inst,
                 void *seqMap,
                 void *cmdData,
                 std::uint32_t cmdId,
                 std::uint32_t sideRaw,
                 TurnSide side,
                 std::uint32_t unk28)
{
    // Build map/turn snapshots so can correlate actions later.
    TurnContext tc = BuildTurnContext(side);
    MapContext  mc = tc.map;

    // For now: structured, capped log only. No bus dispatch yet.
    static int sLogCount = 0;
    if (sLogCount >= 32)
        return;

    ++sLogCount;

    Logf("Engine::OnActionEnd: inst=%p seqMap=%p cmdData=%p "
         "cmdId=%u sideRaw=%u side=%s unk28=%u "
         "gen=%u sideTurn=%u totalTurns=%u (n=%d)",
         inst,
         seqMap,
         cmdData,
         static_cast<unsigned>(cmdId),
         static_cast<unsigned>(sideRaw),
         TurnSideToString(side),
         static_cast<unsigned>(unk28),
         static_cast<unsigned>(mc.generation),
         static_cast<unsigned>(tc.sideTurnIndex),
         static_cast<unsigned>(mc.totalTurns),
         sLogCount);
}

} // namespace Engine
} // namespace Fates
