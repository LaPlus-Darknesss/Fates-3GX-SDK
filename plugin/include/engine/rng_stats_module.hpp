// rng_stats_module.hpp
//
// Example module that listens to Engine RNG events and summarizes
// how many RNG calls were made per side, plus which bounds were
// requested during the current map.

#pragma once

namespace Fates {
namespace Engine {

// Register RNG stats handlers with the engine bus.
// Intended to be called once at startup from Engine::InitCoreModules().
// The function is idempotent and safe to call multiple times.
bool RngStatsModule_RegisterHandlers();

} // namespace Engine
} // namespace Fates
