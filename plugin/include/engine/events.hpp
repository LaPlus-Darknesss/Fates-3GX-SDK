// engine/events.hpp
//
// First thin "engine" layer for Fates-3GX-SDK. This module defines
// small, stable event/context types and the public entrypoints that
// hook stubs should call when interesting things happen in-game.
// These functions build snapshots from core/runtime.hpp, emit
// structured logs, and fan out into the lightweight engine bus.

#pragma once

#include <cstdint>
#include "core/runtime.hpp"  // TurnSide, KillEvent, gMapState
#include "engine/types.hpp"

namespace Fates {
namespace Engine {

// High-level event kind vocabulary. The current bus exposes per-event
// registration (RegisterMapBeginHandler, etc.); EventKind is reserved
// for a possible future generic dispatcher.
enum class EventKind : std::uint16_t
{
    MapBegin,
    MapEnd,
    TurnBegin,
    TurnEnd,
    Kill,
    RngCall,
    LevelUp,
    SkillLearn,
    ItemGain,
    HpChange,   // generic damage/heal event
    // Future: ActionBegin, ActionEnd, Damage, Heal...
};


// Map-level context snapshot.
struct MapContext
{
    void         *seqRoot;      // map__Sequence root pointer (gMapState.seqRoot)
    std::uint32_t generation;   // map generation counter (gMapState.generation)
    TurnSide      startSide;    // who started the map
    TurnSide      currentSide;  // who's currently active
    std::uint32_t totalTurns;   // total number of TurnBegin calls so far
    std::uint32_t killEvents;   // number of kill events this map
};

// Turn-level context snapshot.
struct TurnContext
{
    MapContext    map;           // embedded map context
    TurnSide      side;          // whose turn just began / ended
    std::uint32_t sideTurnIndex; // how many turns this side has taken
};

// Extended kill context built from the runtime KillEvent buffer plus
// the current map/turn summaries. Is not in full use yet, but it's
// a good layout for later.
struct KillContext
{
    KillEvent   core;  // raw struct from core/runtime.hpp
    MapContext  map;   // map snapshot at time of kill
    TurnContext turn;  // turn snapshot at time of kill
};

// HP change context: wraps a local HpEvent with map/turn snapshots.
// Convention: amount > 0 = damage taken, amount < 0 = healing received.
struct HpChangeContext
{
    HpEvent    core;  // local HP event (source/target/amount/flags/context)
    MapContext map;   // map snapshot at time of change
    TurnContext turn; // turn snapshot at time of change
};

// RNG call context. Mostly for telemetry & future “RNG” tooling.
// Crit calcs will most likely have to go through here at some point.
// Does not seem to be a dedicated crit address easily hookable.
struct RngContext
{
    MapContext    map;    // map snapshot when RNG is called (may be inactive)
    TurnContext   turn;   // best-effort turn snapshot (side may be Unknown)
    void         *state;  // RNG state pointer
    std::uint32_t raw;    // raw core RNG value (pre-scaling)
    std::uint32_t bound;  // requested upperBound
    std::uint32_t result; // final scaled result returned to the game
};

// Hit calculation context for RandomCalculateHit-style calls.
// This is intentionally minimal for now: we only know the base input
// rate and the final result returned by the engine.
struct HitCalcContext
{
    MapContext   map;      // snapshot when hit calc was performed
    TurnContext  turn;     // whose turn it was (best-effort)
    int          baseRate; // input parameter to RandomCalculateHit
    int          result;   // value returned by the core function
};

// Level-up context
struct LevelUpContext
{
    MapContext    map;    // snapshot at time of level-up
    TurnContext   turn;   // whose turn it was when the level happened
    UnitHandle    unit;   // unit that just levelled
    std::uint8_t  level;  // new level
    std::uint8_t  _pad[3];
};

// Skill-learn context (per successful Unit__AddEquipSkill).
struct SkillLearnContext
{
    MapContext     map;    // snapshot at time of skill learn
    TurnContext    turn;   // whose turn it was
    UnitHandle     unit;   // unit that learned the skill
    std::uint16_t  skillId;
    std::uint16_t  flags;  // reserved for source bits (level, scroll, script...)
    int            result; // underlying Unit__AddEquipSkill return code
};

// Item-gain context (SEQ_ItemGain).
struct ItemGainContext
{
    MapContext   map;      // snapshot at time of item gain
    TurnContext  turn;     // whose turn it was
    void        *seq;      // SequenceHelper* / context
    UnitHandle   unit;     // recipient unit
    void        *itemArg;  // raw item argument (slot/id pointer)
    void        *modeOrCtx;// mode / context pointer
    int          result;   // underlying SEQ_ItemGain return code
};


// Public entrypoints called from hooks_handlers.cpp.
// These are intentionally thin; they don't know about CTRPF, only
// data from the runtime layer.

void OnMapBegin(void *seqRoot, TurnSide side);
void OnMapEnd(void *seqRoot, TurnSide side);

// Called from Hook_SEQ_TurnBegin and Hook_SEQ_TurnEnd.
void OnTurnBegin(TurnSide side);
void OnTurnEnd(TurnSide side, void *seqMaybe);

// Called from Hook_HP_KillCheck each time we detect at least one
// "real" kill event.
void OnKill(const KillEvent &ev, TurnSide side);

// RNG + unit misc events. These are currently log-only; later they’ll
// fan out through engine/bus once I stabilize the shapes.

void OnRngCall(void *state,
               std::uint32_t raw,
               std::uint32_t bound,
               std::uint32_t result);

// NEW: canonical HP sync driver. Called from Hook_UNIT_UpdateCloneHP.
// This tracks last HP per unit and, when it detects a change, emits a
// high-level HpChange event via OnHpChange().
void OnUnitHpSync(void *unit,
                  int   newHp);

// Generic HP-change event (damage or heal).
// Convention: amount > 0 = damage taken, amount < 0 = healing received.
void OnHpChange(void *sourceUnit,
                void *targetUnit,
                int  amount,
                std::uint32_t flags,
                void *context,
                TurnSide side);

// Called from Hook_UNIT_LevelUp after the level has been applied.
void OnUnitLevelUp(void *unit,
                   std::uint8_t level,
                   TurnSide side);

// Called from Hook_UNIT_SkillLearn after Unit__AddEquipSkill returns.
void OnUnitSkillLearn(void *unit,
                      std::uint16_t skillId,
                      std::uint16_t flags,
                      int result,
                      TurnSide side);

// Called from Hook_SEQ_ItemGain.
void OnItemGain(void *seqHelper,
                void *unit,
                void *itemArg,
                void *modeOrCtx,
                int   result,
                TurnSide side);
// Generic "action ended" hook (attack, wait, etc).
// Currently used for structured logging only; no bus dispatch yet.
void OnActionEnd(void *inst,
                 void *seqMap,
                 void *cmdData,
                 std::uint32_t cmdId,
                 std::uint32_t sideRaw,
                 TurnSide side,
                 std::uint32_t unk28);
				 
// Called from Hook_BTL_HitCalc_Main.
// Provides a high-level view of hit RNG without modifying it (yet).
void OnHitCalc(int baseRate,
               int result);


} // namespace Engine
} // namespace Fates
