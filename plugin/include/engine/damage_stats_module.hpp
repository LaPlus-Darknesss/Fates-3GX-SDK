// damage_stats_module.hpp
//
// Simple example module that tracks per-side damage, healing,
// and kill counts for the current map and prints a summary at
// map end. Uses only the public Engine bus API.

#pragma once

namespace Fates {
namespace Engine {

bool DamageStatsModule_RegisterHandlers();

} // namespace Engine
} // namespace Fates
