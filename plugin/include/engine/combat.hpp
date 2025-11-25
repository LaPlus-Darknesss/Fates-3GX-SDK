// engine/combat.hpp
//
// Front door for combat-related helpers, starting with final
// damage modifiers that run in the same stage as vanilla skills.
// Hooks like BTL_FinalDamage_Pre call into this module to let
// skills, auras, etc. tweak the final damage number before it
// is used by the game and before the forecast renders.

#pragma once

#include <cstdint>
#include "engine/events.hpp"  // MapContext, TurnContext, UnitHandle

namespace Fates {
namespace Engine {
namespace Combat {

struct DamageContext
{
    MapContext   map;        // map snapshot at time of calculation
    TurnContext  turn;       // whose turn this damage belongs to
    UnitHandle   attacker;   // main attacker (may be null)
    UnitHandle   defender;   // main defender (may be null)
    void        *root;       // BTL root / battle state object
    void        *calc;       // calc object passed to FinalDamage
    int          baseDamage; // damage as computed by vanilla
};

// Modifier callback.
//
// currentDamage starts equal to ctx.baseDamage. Each modifier returns
// a new damage value which then feeds into the next modifier.
using DamageModifierFn = int(*)(const DamageContext &ctx,
                                int                  currentDamage);

// Register a modifier. Returns true if added, false if null/full.
bool RegisterDamageModifier(DamageModifierFn fn);

// Called from hooks (BTL_FinalDamage_Pre).
//
//   root, calc   - pointers from BTL_FinalDamage_Pre
//   attackerRaw  - Unit* for the main attacker (may be null)
//   defenderRaw  - Unit* for the main defender (may be null)
//   baseDamage   - damage returned by vanilla.
//
// Returns damage after all modifiers, clamped >= 0.
int ApplyDamageModifiers(void  *root,
                         void  *calc,
                         void  *attackerRaw,
                         void  *defenderRaw,
                         int    baseDamage);

} // namespace Combat
} // namespace Engine
} // namespace Fates
