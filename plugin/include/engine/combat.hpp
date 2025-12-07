// engine/combat.hpp
//
// Core combat context + modifier pipeline wiring.
//
// This module owns:
//   * DamageOrigin: where in the pipeline a damage calculation came from.
//   * DamageMode: high-level behaviour mode (Vanilla / Mirror / Full).
//   * DamageContext: map/turn/unit metadata for a single damage pass.
//   * DamageModifierFn + registration.
//   * ApplyDamageModifiers / ApplyDamageModifiersEx front doors.
//
// Hooks like MAP_BattleInfo__CalculateSimple (FORECAST) and, in future,
// selected FINAL_HP surfaces, build a DamageContext and call
// ApplyDamageModifiersEx.

#pragma once

#include <cstdint>
#include "engine/events.hpp"  // MapContext, TurnContext, UnitHandle

namespace Fates {
namespace Engine {
namespace Combat {

// Where in the pipeline this ApplyDamageModifiers call originated.
enum class DamageOrigin : std::uint8_t
{
    Unknown  = 0, // legacy / unspecified caller
    Forecast = 1, // forecast / preview calculations
    FinalHp  = 2, // real HP-commit path (e.g. BTL_FinalDamage_Pre)
};

// High-level behaviour mode for the damage engine.
//
// This is a global configuration knob that tells the engine and
// hooks how aggressive they are allowed to be:
//
//   Vanilla : observation-only; rules may read context but hooks
//             must not write back into HUD/HP surfaces.
//   Mirror  : run full rules on both Forecast and FinalHp and
//             compare against vanilla, but still no writeback.
//   Full    : rules may change damage and hooks are allowed to
//             mirror those changes into forecast/HP surfaces
//             when it is safe to do so.
enum class DamageMode : std::uint8_t
{
    Vanilla = 0,
    Mirror  = 1,
    Full    = 2,
};

struct DamageContext
{
    MapContext   map;        // map snapshot at time of calculation
    TurnContext  turn;       // whose turn this damage belongs to
    UnitHandle   attacker;   // main attacker (may be null)
    UnitHandle   defender;   // main defender (may be null)
    void        *root;       // battle root / state object (if known)
    void        *calc;       // calc object passed to FinalDamage (if any)
    int          baseDamage; // engine's notion of "base" damage for this call

    // Extra metadata used for cautious rules and RE:
    //
    // origin:
    //   * Forecast -> preview/forecast-side calculations.
    //   * FinalHp  -> calls that correspond to real HP commits.
    //
    // isFinalPass:
    //   * true  -> canonical “final” call for this origin (Forecast or FinalHp)
    //              for this damage chunk (e.g. the main forecast call, or the
    //              final HP commit).
    //   * false -> intermediate / helper calls where rules should usually
    //              be conservative or no-op.
    DamageOrigin origin;        // Forecast / FinalHp / Unknown
    bool         isFinalPass;   // true for canonical final pass
    DamageMode   mode;          // global damage mode (Vanilla/Mirror/Full)

    // Engine-level invariant flag:
    //   * false -> finalDamage == baseDamage (no net change from vanilla)
    //              AND no rule has changed currentDamage along the way.
    //   * true  -> some rule changed currentDamage at some point in the
    //              chain (even if later rules brought it back to base).
    bool         didChangeFromVanilla;

    int          hpBefore;      // defender HP before this pass (if known)
    int          hpAfter;       // defender HP after this pass (if known)
    int          forecastTotal; // numHits * dmgPerHit when known
    int          numHits;       // forecast number of hits
    int          dmgPerHit;     // forecast per-hit damage

    // Snapshot of the pipeline's current damage value at the moment
    // a rule runs. Starts equal to baseDamage and is updated after
    // each modifier call so rules can reason about cumulative effects.
    int          currentDamage;
};

// Modifier callback.
//
// currentDamage starts equal to ctx.baseDamage. Each modifier returns
// a new damage value which then feeds into the next modifier.
using DamageModifierFn = int(*)(const DamageContext &ctx,
                                int                  currentDamage);

// Global mode controls. In v2 these are intentionally simple; callers
// can set the mode during plugin initialisation. Default is Vanilla.
DamageMode GetCurrentDamageMode();
void       SetCurrentDamageMode(DamageMode mode);

// Convenience helpers so hook code doesn't need to poke at the enum
// directly or keep its own duplicate state.
bool IsDamageModeVanilla();
bool IsDamageModeMirror();
bool IsDamageModeFull();

// Forecast writeback policy.
//
// High-level rule (v2):
//   * Only in DamageMode::Full.
//   * Only if the pipeline's final damage differs from baseDamage.
//   * Still does NOT perform any writes; hooks remain responsible
//     for actually updating BattleInfo fields if this returns true.
bool ShouldWritebackForecast(int baseDamage, int modifiedDamage);

// Register a modifier. Returns true if added, false if null/full.
bool RegisterDamageModifier(DamageModifierFn fn);

// Core front-door used by hooks. Most callers should prefer the
// extended form below; this legacy overload simply fills a minimal
// DamageContext with origin=Unknown and no HP/forecast metadata.
int ApplyDamageModifiers(void  *root,
                         void  *calc,
                         void  *attackerRaw,
                         void  *defenderRaw,
                         int    baseDamage);

// Extended front door that allows callers to provide additional
// metadata (origin, hpBefore/hpAfter, forecast view). Rules can
// use this to stay conservative and avoid double-applying effects.
int ApplyDamageModifiersEx(void        *root,
                           void        *calc,
                           void        *attackerRaw,
                           void        *defenderRaw,
                           int          baseDamage,
                           DamageOrigin origin,
                           bool         isFinalPass,
                           int          hpBefore,
                           int          hpAfter,
                           int          forecastTotal,
                           int          numHits,
                           int          dmgPerHit);

} // namespace Combat
} // namespace Engine
} // namespace Fates
