// rng_stats_module.hpp
//
// Example module that listens to Engine RNG events and summarizes
// how many RNG calls were made per side, plus which bounds were
// requested during the current map.

#pragma once

namespace Fates {
namespace Engine {

bool RngStatsModule_RegisterHandlers();

} // namespace Engine
} // namespace Fates
