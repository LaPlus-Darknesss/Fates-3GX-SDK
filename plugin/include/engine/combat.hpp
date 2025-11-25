// engine/combat.hpp
//
// Tiny combat engine glue for post-battle HP adjustments.
// Hooks like SEQ_HpDamage can call into this module to let higher-
// level systems (skills, auras, terrain, etc.) adjust the HP values
// that will be written back to units.
//
// The contract is intentionally narrow:
//
//   * Input: a single slot's post-battle HP value, plus enough
//     context to know who the attacker was, which slot this is,
//     and what the current map/turn state looks like.
//   * Output: the adjusted HP value (clamped >= 0).
//
// Higher-level modules register PostBattleHpModifier callbacks that
// can tweak the HP value in-place. The first concrete user of this is
// Engine::Skills, which can implement "flat damage bonus if unit has
// skill X" without touching hooks directly.

#pragma once

#include <cstdint>
#include "engine/events.hpp"  // MapContext, TurnContext, UnitHandle

namespace Fates {
namespace Engine {
namespace Combat {

struct PostBattleHpContext
{
    MapContext   map;         // snapshot at time of adjustment
    TurnContext  turn;        // whose turn the sequence belongs to
    UnitHandle   attacker;    // best-effort attacker (may be null)
    UnitHandle   target;      // reserved for future (null for now)
    void        *seq;         // SEQ_HpDamage / SEQ_Battle_UpdateHp context
    int          slot;        // 0..3 (main/partner slots)
    int          mode;        // SEQ mode argument (0 = main battle)
    std::uint32_t originalHp; // original HP word from the engine
};

// Callback type: given the current context and HP value, return the
// new HP value. Implementations should be pure (no side-effects) and
// must not assume they run first/last.
using PostBattleHpModifierFn = int(*)(const PostBattleHpContext &ctx,
                                      int                        currentHp);

// Registration API. Returns true if the function was added to the
// modifier list, false if full or fn == nullptr.
bool RegisterPostBattleHpModifier(PostBattleHpModifierFn fn);

// Low-level driver used by hooks. This builds a PostBattleHpContext
// snapshot from the raw inputs and runs all registered modifiers in
// sequence.
//
// Parameters:
//   seq         - SEQ_HpDamage / SEQ_Battle_UpdateHp pointer.
//   mode        - mode argument from SEQ_HpDamage (0 = main battle).
//   slot        - 0..3 slot index in the HP result buffer.
//   hp          - original HP word for this slot.
//   attackerRaw - optional Unit* for the main attacker (may be null).
//
// Returns:
//   The final HP to write back for this slot (clamped >= 0).
std::uint32_t ApplyPostBattleHpModifiers(void        *seq,
                                         int          mode,
                                         int          slot,
                                         std::uint32_t hp,
                                         void        *attackerRaw);

} // namespace Combat
} // namespace Engine
} // namespace Fates
