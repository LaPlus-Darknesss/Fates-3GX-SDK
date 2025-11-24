#pragma once
#include <stdint.h>

namespace CTRPluginFramework { class PluginMenu; }

// Bring in hook count declarations from core/runtime.hpp.  The
// implementation defines gHookCount within the Fates namespace.  Do
// not redeclare gHookCount here; instead use Fates::gHookCount.
#include "core/runtime.hpp"
void DumpKillEventsToLog();

void ShowHookCountsOSD();
void DumpHookCountsToFile();
void InstallHookDebugMenu(CTRPluginFramework::PluginMenu& menu);
