// engine/hit_stats_module.hpp
//
// Simple telemetry module that listens to HitCalc events and records
// hit/attempt counts per side for each map, plus a global total.
// A summary is logged on MapEnd.
//
// This is read-only: it does not modify gameplay.

#pragma once

namespace Fates {
namespace Engine {

// Register HitStatsModule handlers with the engine bus.
// Returns true on success.
bool HitStatsModule_RegisterHandlers();

} // namespace Engine
} // namespace Fates
