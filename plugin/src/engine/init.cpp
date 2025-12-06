// engine/init.cpp
//
// Implementation of the central engine initialiser. This collects
// all core modules that register handlers on the engine event bus
// so that plugin startup only needs a single call.
//
// Current responsibilities:
//   * HP/kill tracker module
//   * Damage stats module
//   * RNG stats module
//   * Hit stats module (hit attempts / successes per side)
//   * Damage rule pipeline (core + skill-based rules)
//   * Debug skill engine bootstrap (defensive)

#include "engine/init.hpp"

// Core engine modules
#include "engine/hp_kill_tracker.hpp"
#include "engine/damage_stats_module.hpp"
#include "engine/rng_stats_module.hpp"
#include "engine/hit_stats_module.hpp"
#include "engine/skills.hpp"
#include "engine/damage.hpp"
#include "engine/skill_damage_rules.hpp"  // skill-based damage rules

#include "util/debug_log.hpp"

namespace Fates {
namespace Engine {

bool InitCoreModules()
{
    // Per-map HP + kill tracking.
    HpKillTracker_RegisterHandlers();

    // Lightweight per-side damage/heal/kill telemetry.
    DamageStatsModule_RegisterHandlers();

    // Lightweight per-side RNG telemetry.
    RngStatsModule_RegisterHandlers();

    // Lightweight per-side hit telemetry (attempts / successes).
    HitStatsModule_RegisterHandlers();

    // Wire the damage-rule pipeline into Engine::Combat explicitly.
    // This is idempotent and safe even though damage.cpp also has a
    // static bootstrap.
    Damage::Init();

    // Register all skill-based damage rules with the damage engine.
    // This is idempotent and safe to call once at startup.
    Skills::RegisterDamageRules();

    // Defensive: ensure debug skills are initialised.
    // This is idempotent and safe even though skills.cpp
    // also uses a static bootstrap.
    Skills::InitDebugSkills();

    Logf("Engine::InitCoreModules: all handlers registered (no fatal errors)");

    // If any submodule needs to report hard failure, it should log
    // internally; for now we always report success here.
    return true;
}

} // namespace Engine
} // namespace Fates
