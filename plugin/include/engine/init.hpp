// engine/init.hpp
//
// Central initialisation entry point for the engine layer.
// This wires up all "core" modules that listen on the engine
// event bus (HP/kill tracker, damage stats, RNG stats, etc.).
//
// Call InitCoreModules() once during plugin startup, after
// HookManager has installed the runtime hooks.

#pragma once

namespace Fates {
namespace Engine {

bool InitCoreModules();

} // namespace Engine
} // namespace Fates
