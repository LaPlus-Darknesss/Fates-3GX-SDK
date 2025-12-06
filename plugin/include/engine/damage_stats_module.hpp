// engine/damage_stats_module.hpp
//
// Lightweight damage stats / RE helper module. This plugs into the
// Engine::Combat final-damage pipeline as a no-op modifier and emits
// structured logs for each call, so we can see how often and when
// BTL_FinalDamage_Pre fires without changing game behaviour.

#pragma once

namespace Fates {
namespace Engine {

// Entry point used by InitCoreModules(). This forwards to
// DamageStats::Init() and is idempotent, so calling it more than
// once is safe.
void DamageStatsModule_RegisterHandlers();

namespace DamageStats {

// Initialise the damage stats module (idempotent).
//
// Normally you don't need to call this directly; the engine startup
// path invokes DamageStatsModule_RegisterHandlers() for you. This
// entrypoint is exposed mainly for tests or explicit wiring.
void Init();

} // namespace DamageStats
} // namespace Engine
} // namespace Fates
