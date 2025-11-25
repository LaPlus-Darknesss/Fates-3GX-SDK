// engine/bus.cpp
//
// Implementation of the lightweight engine event bus. Each event
// type (map begin/end, turn begin/end, kill, RNG, level-up, etc.)
// has a small fixed-size handler array. Register*Handler() appends,
// Dispatch*() walks the list and calls each handler.
//
// This is intentionally basic C so it's easy to reason
// about and friendly to the 3DS architecture.

#include "engine/bus.hpp"
#include "util/debug_log.hpp"

namespace Fates {
namespace Engine {

namespace {

// Bump these if you ever need more listeners.
constexpr int kMaxMapBeginHandlers   = 8;
constexpr int kMaxMapEndHandlers     = 8;
constexpr int kMaxTurnBeginHandlers  = 8;
constexpr int kMaxTurnEndHandlers    = 8;
constexpr int kMaxKillHandlers       = 8;
constexpr int kMaxHpChangeHandlers   = 16; 
constexpr int kMaxRngHandlers        = 4;
constexpr int kMaxHitCalcHandlers    = 8;  //
constexpr int kMaxLevelUpHandlers    = 4;
constexpr int kMaxSkillLearnHandlers = 4;
constexpr int kMaxItemGainHandlers   = 4;

// Storage for handler arrays + counts.
MapBeginHandler   sMapBeginHandlers[kMaxMapBeginHandlers]     = {};
MapEndHandler     sMapEndHandlers[kMaxMapEndHandlers]         = {};
TurnBeginHandler  sTurnBeginHandlers[kMaxTurnBeginHandlers]   = {};
TurnEndHandler    sTurnEndHandlers[kMaxTurnEndHandlers]       = {};
KillHandler       sKillHandlers[kMaxKillHandlers]             = {};
HpChangeHandler  sHpChangeHandlers[kMaxHpChangeHandlers]      = {};
RngHandler        sRngHandlers[kMaxRngHandlers]               = {};
HitCalcHandler    sHitCalcHandlers[kMaxHitCalcHandlers]		  = {};
LevelUpHandler    sLevelUpHandlers[kMaxLevelUpHandlers]       = {};
SkillLearnHandler sSkillLearnHandlers[kMaxSkillLearnHandlers] = {};
ItemGainHandler   sItemGainHandlers[kMaxItemGainHandlers]     = {};

int sNumMapBeginHandlers   = 0;
int sNumMapEndHandlers     = 0;
int sNumTurnBeginHandlers  = 0;
int sNumTurnEndHandlers    = 0;
int sNumKillHandlers       = 0;
int sNumHpChangeHandlers   = 0;
int sNumHitCalcHandlers    = 0;  
int sNumRngHandlers        = 0;
int sNumLevelUpHandlers    = 0;
int sNumSkillLearnHandlers = 0;
int sNumItemGainHandlers   = 0;

template <typename Fn>
bool RegisterHandler(Fn fn, Fn *storage, int &count, int capacity, const char *name)
{
    if (fn == nullptr)
        return false;

    if (count >= capacity)
    {
        Logf("Engine::%s: capacity full (%d)", name, capacity);
        return false;
    }

    storage[count++] = fn;
    Logf("Engine::%s: registered handler #%d", name, count);
    return true;
}

template <typename Fn, typename Ctx>
void DispatchHandlers(const Ctx &ctx, Fn *storage, int count)
{
    for (int i = 0; i < count; ++i)
    {
        if (storage[i] != nullptr)
            storage[i](ctx);
    }
}

} // anonymous namespace

// == Registration ====================================================

bool RegisterMapBeginHandler(MapBeginHandler fn)
{
    return RegisterHandler(fn,
                           sMapBeginHandlers,
                           sNumMapBeginHandlers,
                           kMaxMapBeginHandlers,
                           "RegisterMapBeginHandler");
}

bool RegisterMapEndHandler(MapEndHandler fn)
{
    return RegisterHandler(fn,
                           sMapEndHandlers,
                           sNumMapEndHandlers,
                           kMaxMapEndHandlers,
                           "RegisterMapEndHandler");
}

bool RegisterTurnBeginHandler(TurnBeginHandler fn)
{
    return RegisterHandler(fn,
                           sTurnBeginHandlers,
                           sNumTurnBeginHandlers,
                           kMaxTurnBeginHandlers,
                           "RegisterTurnBeginHandler");
}

bool RegisterTurnEndHandler(TurnEndHandler fn)
{
    return RegisterHandler(fn,
                           sTurnEndHandlers,
                           sNumTurnEndHandlers,
                           kMaxTurnEndHandlers,
                           "RegisterTurnEndHandler");
}

bool RegisterKillHandler(KillHandler fn)
{
    return RegisterHandler(fn,
                           sKillHandlers,
                           sNumKillHandlers,
                           kMaxKillHandlers,
                           "RegisterKillHandler");
}

bool RegisterHpChangeHandler(HpChangeHandler fn)
{
    return RegisterHandler(fn,
                           sHpChangeHandlers,
                           sNumHpChangeHandlers,
                           kMaxHpChangeHandlers,
                           "RegisterHpChangeHandler");
}

bool RegisterRngHandler(RngHandler fn)
{
    return RegisterHandler(fn,
                           sRngHandlers,
                           sNumRngHandlers,
                           kMaxRngHandlers,
                           "RegisterRngHandler");
}

bool RegisterHitCalcHandler(HitCalcHandler fn)
{
    return RegisterHandler(fn,
                           sHitCalcHandlers,
                           sNumHitCalcHandlers,
                           kMaxHitCalcHandlers,
                           "RegisterHitCalcHandler");
}

bool RegisterLevelUpHandler(LevelUpHandler fn)
{
    return RegisterHandler(fn,
                           sLevelUpHandlers,
                           sNumLevelUpHandlers,
                           kMaxLevelUpHandlers,
                           "RegisterLevelUpHandler");
}

bool RegisterSkillLearnHandler(SkillLearnHandler fn)
{
    return RegisterHandler(fn,
                           sSkillLearnHandlers,
                           sNumSkillLearnHandlers,
                           kMaxSkillLearnHandlers,
                           "RegisterSkillLearnHandler");
}

bool RegisterItemGainHandler(ItemGainHandler fn)
{
    return RegisterHandler(fn,
                           sItemGainHandlers,
                           sNumItemGainHandlers,
                           kMaxItemGainHandlers,
                           "RegisterItemGainHandler");
}

// == Dispatch ========================================================

void DispatchMapBegin(const MapContext &ctx)
{
    DispatchHandlers(ctx, sMapBeginHandlers, sNumMapBeginHandlers);
}

void DispatchMapEnd(const MapContext &ctx)
{
    DispatchHandlers(ctx, sMapEndHandlers, sNumMapEndHandlers);
}

void DispatchTurnBegin(const TurnContext &ctx)
{
    DispatchHandlers(ctx, sTurnBeginHandlers, sNumTurnBeginHandlers);
}

void DispatchTurnEnd(const TurnContext &ctx)
{
    DispatchHandlers(ctx, sTurnEndHandlers, sNumTurnEndHandlers);
}

void DispatchKill(const KillContext &ctx)
{
    DispatchHandlers(ctx, sKillHandlers, sNumKillHandlers);
}

void DispatchHpChange(const HpChangeContext &ctx)
{
    DispatchHandlers(ctx, sHpChangeHandlers, sNumHpChangeHandlers);
}

void DispatchRngCall(const RngContext &ctx)
{
    DispatchHandlers(ctx, sRngHandlers, sNumRngHandlers);
}

void DispatchHitCalc(const HitCalcContext &ctx)
{
    DispatchHandlers(ctx, sHitCalcHandlers, sNumHitCalcHandlers);
}

void DispatchLevelUp(const LevelUpContext &ctx)
{
    DispatchHandlers(ctx, sLevelUpHandlers, sNumLevelUpHandlers);
}

void DispatchSkillLearn(const SkillLearnContext &ctx)
{
    DispatchHandlers(ctx, sSkillLearnHandlers, sNumSkillLearnHandlers);
}

void DispatchItemGain(const ItemGainContext &ctx)
{
    DispatchHandlers(ctx, sItemGainHandlers, sNumItemGainHandlers);
}

} // namespace Engine
} // namespace Fates
