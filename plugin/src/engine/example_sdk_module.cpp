// engine/example_sdk_module.cpp
//
// Minimal example of how a modder would hook into the Fates-3GX-SDK
// event bus. This module just logs a few high-level events using the
// stable context types from engine/events.hpp.

#include "engine/events.hpp"
#include "engine/bus.hpp"
#include "util/debug_log.hpp"

namespace Fates {
namespace Engine {
namespace Example {

// Called at the start of each map.
static void OnMapBeginHandler(const MapContext &ctx)
{
    Logf("[Example] MapBegin: seq=%p gen=%u start=%s current=%s totalTurns=%u kills=%u",
         ctx.seqRoot,
         static_cast<unsigned>(ctx.generation),
         TurnSideToString(ctx.startSide),
         TurnSideToString(ctx.currentSide),
         static_cast<unsigned>(ctx.totalTurns),
         static_cast<unsigned>(ctx.killEvents));
}

// Called whenever a "real" kill is detected by HP_KillCheck.
static void OnKillHandler(const KillContext &kc)
{
    const KillEvent &ev = kc.core;

    Logf("[Example] Kill: seq=%p flags=0x%08X dead0=%p dead1=%p "
         "gen=%u side=%s totalTurns=%u sideTurn=%u",
         ev.seq,
         ev.flags,
         ev.dead0,
         ev.dead1,
         static_cast<unsigned>(kc.map.generation),
         TurnSideToString(kc.turn.side),
         static_cast<unsigned>(kc.map.totalTurns),
         static_cast<unsigned>(kc.turn.sideTurnIndex));
}

// Called whenever an HP change is emitted by OnHpChange / OnUnitHpSync.
// Convention: amount > 0 = damage, amount < 0 = healing.
static void OnHpChangeHandler(const HpChangeContext &hc)
{
    const HpEvent &ev = hc.core;

    Logf("[Example] HpChange: src=%p tgt=%p amt=%d flags=0x%08X "
         "gen=%u side=%s sideTurn=%u",
         ev.source.Raw(),
         ev.target.Raw(),
         ev.amount,
         static_cast<unsigned>(ev.flags),
         static_cast<unsigned>(hc.map.generation),
         TurnSideToString(hc.turn.side),
         static_cast<unsigned>(hc.turn.sideTurnIndex));
}

// Called whenever a unit learns a skill.
static void OnSkillLearnHandler(const SkillLearnContext &ctx)
{
    Logf("[Example] SkillLearn: unit=%p skill=0x%04X flags=0x%04X result=%d "
         "gen=%u side=%s sideTurn=%u",
         ctx.unit.Raw(),
         static_cast<unsigned>(ctx.skillId),
         static_cast<unsigned>(ctx.flags),
         ctx.result,
         static_cast<unsigned>(ctx.map.generation),
         TurnSideToString(ctx.turn.side),
         static_cast<unsigned>(ctx.turn.sideTurnIndex));
}

// Public init called from MainImpl() or your engine bootstrap.
void ExampleSdkModule_RegisterHandlers()
{
    // These Register* functions are provided by engine/bus.cpp.
    // They push the given function into an internal handler list.
    RegisterMapBeginHandler(&OnMapBeginHandler);
    RegisterKillHandler(&OnKillHandler);
    RegisterHpChangeHandler(&OnHpChangeHandler);
    RegisterSkillLearnHandler(&OnSkillLearnHandler);

    Logf("ExampleSdkModule_RegisterHandlers: handlers registered");
}

} // namespace Example
} // namespace Engine
} // namespace Fates
