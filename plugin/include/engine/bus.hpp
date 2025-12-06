// engine/bus.hpp
//
// Lightweight event bus for the Fates-3GX engine. Hook stubs call
// Engine::OnMapBegin/OnTurnBegin/OnKill/etc. which build context
// structs (MapContext, TurnContext, KillContext, RngContext, etc.)
// and then fan those out via this bus to any registered listeners.
//
// Design goals:
//   * No dynamic allocation.
//   * No removal API.
//   * Fixed-size handler arrays per event type.
//   * No duplicate-suppression: if you register the same function
//     twice, it will be called twice. Callers are expected to be
//     idempotent about registration.

#pragma once

#include <cstdint>
#include "engine/events.hpp"  // MapContext, TurnContext, KillContext, ...

namespace Fates {
namespace Engine {

// Handler function types for each event "family".
using MapBeginHandler   = void(*)(const MapContext &);
using MapEndHandler     = void(*)(const MapContext &);
using TurnBeginHandler  = void(*)(const TurnContext &);
using TurnEndHandler    = void(*)(const TurnContext &);
using KillHandler       = void(*)(const KillContext &);
using HpChangeHandler   = void(*)(const HpChangeContext &);
using RngHandler        = void(*)(const RngContext &);
using HitCalcHandler    = void(*)(const HitCalcContext &);
using LevelUpHandler    = void(*)(const LevelUpContext &);
using SkillLearnHandler = void(*)(const SkillLearnContext &);
using ItemGainHandler   = void(*)(const ItemGainContext &);

// Registration API: usually called from engine submodules at startup.
// Returns true on success, false if capacity is full or fn == nullptr.
// As noted above, duplicates are not suppressed: registering the same
// function twice will cause it to be called twice at dispatch time.
bool RegisterMapBeginHandler(MapBeginHandler fn);
bool RegisterMapEndHandler(MapEndHandler fn);
bool RegisterTurnBeginHandler(TurnBeginHandler fn);
bool RegisterTurnEndHandler(TurnEndHandler fn);
bool RegisterKillHandler(KillHandler fn);
bool RegisterHpChangeHandler(HpChangeHandler fn);
bool RegisterRngHandler(RngHandler fn);
bool RegisterHitCalcHandler(HitCalcHandler fn);
bool RegisterLevelUpHandler(LevelUpHandler fn);
bool RegisterSkillLearnHandler(SkillLearnHandler fn);
bool RegisterItemGainHandler(ItemGainHandler fn);

// Internal dispatch API: used by Engine::On* in events.cpp.
// You generally won't call these from outside the Engine module.
void DispatchMapBegin(const MapContext &ctx);
void DispatchMapEnd(const MapContext &ctx);
void DispatchTurnBegin(const TurnContext &ctx);
void DispatchTurnEnd(const TurnContext &ctx);
void DispatchKill(const KillContext &ctx);
void DispatchHpChange(const HpChangeContext &ctx);
void DispatchRngCall(const RngContext &ctx);
void DispatchHitCalc(const HitCalcContext &ctx);
void DispatchLevelUp(const LevelUpContext &ctx);
void DispatchSkillLearn(const SkillLearnContext &ctx);
void DispatchItemGain(const ItemGainContext &ctx);

} // namespace Engine
} // namespace Fates
