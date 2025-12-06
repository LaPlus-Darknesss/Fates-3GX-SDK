#pragma once

#include <cstdint>

namespace Fates {
namespace Engine {
namespace Damage {

/// High-level stages in the vanilla Fates damage pipeline that we care about.
///
/// These are the same four roots annotated in Ghidra:
///   * PreviewSimple  -> map__BattleInfo__CalculateSimple
///   * FinalHpDriver  -> map__BattleCalculator__CalculateAttack
///   * HitRng         -> BTL_HitCalc_Main
///   * FinalHpObs     -> BTL_FinalDamage_Pre
///
/// In the v2 SDK design:
///   * Normal rules live in PreviewSimple (BattleInfo forecast).
///   * FinalHpDriver/FinalHpObs are observation + mirroring only.
///   * HitRng is for RNG-curve tweaks, not stat math.
enum class Stage : std::uint8_t {
    PreviewSimple = 0,  ///< map__BattleInfo__CalculateSimple (PREVIEW_SIMPLE)
    FinalHpDriver = 1,  ///< map__BattleCalculator__CalculateAttack (FINAL_HP_DRIVER)
    HitRng        = 2,  ///< BTL_HitCalc_Main (HIT_RNG)
    FinalHpObs    = 3,  ///< BTL_FinalDamage_Pre (FINAL_HP_OBS)
};

} // namespace Damage
} // namespace Engine
} // namespace Fates
