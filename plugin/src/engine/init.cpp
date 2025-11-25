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
//   * Debug skill engine bootstrap (defensive)

#include "engine/init.hpp"

// Core engine modules
#include "engine/hp_kill_tracker.hpp"
#include "engine/damage_stats_module.hpp"
#include "engine/rng_stats_module.hpp"
#include "engine/hit_stats_module.hpp"   // NEW
#include "engine/skills.hpp"

#include "util/debug_log.hpp"

namespace Fates {
namespace Engine {

bool InitCoreModules()
{
    bool ok = true;

    // Per-map HP + kill tracking.
    ok = ok && HpKillTracker_RegisterHandlers();

    // Lightweight per-side damage/heal/kill telemetry.
    ok = ok && DamageStatsModule_RegisterHandlers();

    // Lightweight per-side RNG telemetry.
    ok = ok && RngStatsModule_RegisterHandlers();

    // Lightweight per-side hit telemetry (attempts / successes).
    ok = ok && HitStatsModule_RegisterHandlers();  // NEW

    // Defensive: ensure debug skills are initialised.
    // This is idempotent and safe even though skills.cpp
    // also uses a static bootstrap.
    Skills::InitDebugSkills();

    if (ok)
    {
        Logf("Engine::InitCoreModules: all handlers registered successfully");
    }
    else
    {
        Logf("Engine::InitCoreModules: WARNING: some handler registrations failed");
    }

    return ok;
}

} // namespace Engine
} // namespace Fates
