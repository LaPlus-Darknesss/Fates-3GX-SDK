// main.cpp

#include <CTRPluginFramework.hpp>
#include <3ds.h>
#include <cstring>
#include <cstdio>

#include "util/debug_log.hpp"
#include "hook_debug.hpp"           // Debug UI for hooks (DumpHookCountsToFile, DumpKillEventsToLog)
#include "core/hook_manager.hpp"
#include "core/runtime.hpp"
#include "engine/hp_kill_tracker.hpp"   // HP + kill summary engine
#include "engine/damage_stats_module.hpp"
#include "engine/rng_stats_module.hpp"

using namespace CTRPluginFramework;

// Forward-declare the example SDK module init (defined in
// plugin/src/engine/example_sdk_module.cpp). This is a bundled,
// non-invasive example showing how to hook into the engine bus.
namespace Fates {
namespace Engine {
namespace Example {
    void ExampleSdkModule_RegisterHandlers();
}
}
}

// ---------------------------------------------------------------------
// Simple debug UI: dump MapLifeCycleState to a MessageBox.
// ---------------------------------------------------------------------

static void ShowMapLifecycleState(MenuEntry *entry)
{
    (void)entry;

    using namespace Fates;

    char buffer[512];

    std::snprintf(
        buffer,
        sizeof(buffer),
        "Generation:  %u\n"
        "Seq root:    %p\n"
        "Start side:  %s\n"
        "Curr side:   %s\n"
        "Total turns: %u\n"
        "Side0 turns: %u\n"
        "Side1 turns: %u\n"
        "Side2 turns: %u\n"
        "Side3 turns: %u\n"
        "Kills (map): %u",
        static_cast<unsigned>(gMapState.generation),
        gMapState.seqRoot,
        TurnSideToString(gMapState.startSide),
        TurnSideToString(gMapState.currentSide),
        static_cast<unsigned>(gMapState.totalTurns),
        static_cast<unsigned>(gMapState.turnCount[0]),
        static_cast<unsigned>(gMapState.turnCount[1]),
        static_cast<unsigned>(gMapState.turnCount[2]),
        static_cast<unsigned>(gMapState.turnCount[3]),
        static_cast<unsigned>(gMapState.killEvents)
    );

    MessageBox("Map lifecycle state", buffer)();
}

void DumpHookTable();   // from hook_table_debug.cpp
void DumpHookSites();   // from hook_sites_debug.cpp

extern "C" volatile bool gHpApplyLogEnabled;

extern "C" {
    // No external C stubs are needed at this stage. Test hooks via
    // the debug menu / hotkeys instead of calling them directly.
}

static volatile bool gRun = true;

// ---------------------------------------------------------------------
// Debug thread body (runs on main thread – no CTRPF Thread API used).
// ---------------------------------------------------------------------
// Most of the hotkeys are obsolete and most events are simply logged instead, 
// will be phased out later.

static void DebugThread(void *)
{
    Logf("DebugThread: start");

    u32  iter = 0;
    bool hotkeyDumpLatched     = false;
    bool hotkeyTestLatched     = false;
    bool hotkeyTableLatched    = false;
    bool hotkeySitesLatched    = false;
    bool hotkeyMapStateLatched = false;

    while (gRun)
    {
        ++iter;

        if (iter % 40 == 0)
            Logf("DebugThread: alive (iter=%u)", iter);

        Controller::Update();

        // Hotkey: L + R + Down + Y -> dump hook site bytes vs guards
        if (Controller::IsKeysDown(Key::L | Key::R | Key::DPadDown | Key::Y))
        {
            if (!hotkeySitesLatched)
            {
                Logf("DebugThread: L+R+Down+Y -> DumpHookSites (iter=%u)", iter);
                DumpHookSites();
                hotkeySitesLatched = true;
            }
        }
        else
        {
            hotkeySitesLatched = false;
        }

        // Hotkey: L + R + A + Y -> toggle HP_Apply logging & dump hook counts
        if (Controller::IsKeysDown(Key::L | Key::R | Key::A | Key::Y))
        {
            if (!hotkeyDumpLatched)
            {
                gHpApplyLogEnabled = !gHpApplyLogEnabled;

                Logf("DebugThread: L+R+A+Y -> Log SEQ_HpDamage %s (iter=%u)",
                     gHpApplyLogEnabled ? "ENABLED" : "DISABLED",
                     iter);

                DumpHookCountsToFile();
                DumpKillEventsToLog();   // log current kill buffer as well
                hotkeyDumpLatched = true;
            }
        }
        else
        {
            hotkeyDumpLatched = false;
        }

        // Hotkey: L + R + X + Y -> self-test hooks (just logs for now)
        if (Controller::IsKeysDown(Key::L | Key::R | Key::X | Key::Y))
        {
            if (!hotkeyTestLatched)
            {
                Logf("DebugThread: L+R+X+Y pressed (iter=%u)", iter);
                hotkeyTestLatched = true;
            }
        }
        else
        {
            hotkeyTestLatched = false;
        }

        // Hotkey: L + R + Up + Y -> dump hook table description
        if (Controller::IsKeysDown(Key::L | Key::R | Key::DPadUp | Key::Y))
        {
            if (!hotkeyTableLatched)
            {
                Logf("DebugThread: L+R+Up+Y -> DumpHookTable (iter=%u)", iter);
                DumpHookTable();
                hotkeyTableLatched = true;
            }
        }
        else
        {
            hotkeyTableLatched = false;
        }

        // Hotkey: L + R + Left + Y -> show MapLifeCycleState
        if (Controller::IsKeysDown(Key::L | Key::R | Key::DPadLeft | Key::Y))
        {
            if (!hotkeyMapStateLatched)
            {
                Logf("DebugThread: L+R+Left+Y -> ShowMapLifecycleState (iter=%u)", iter);
                ShowMapLifecycleState(nullptr);
                hotkeyMapStateLatched = true;
            }
        }
        else
        {
            hotkeyMapStateLatched = false;
        }

        svcSleepThread(50 * 1000000LL);
    }

    Logf("DebugThread: end");
}

// ---------------------------------------------------------------------
// Simple memory probe: read and log 3 words at a VA, useful for testing, otherwise ignore.
// ---------------------------------------------------------------------

static void ProbeWords(u32 addr, const char *label = nullptr)
{
    u32 w0 = 0, w1 = 0, w2 = 0;

    if (!Process::Read32(addr + 0, w0) ||
        !Process::Read32(addr + 4, w1) ||
        !Process::Read32(addr + 8, w2))
    {
        if (label)
            Logf("Probe: FAILED to read at 0x%08X [%s]", addr, label);
        else
            Logf("Probe: FAILED to read at 0x%08X", addr);
        return;
    }

    if (label)
    {
        Logf("Probe: words at 0x%08X [%s] = %08X %08X %08X",
             addr, label, w0, w1, w2);
    }
    else
    {
        Logf("Probe: words at 0x%08X = %08X %08X %08X",
             addr, w0, w1, w2);
    }
}

// ---------------------------------------------------------------------
// Main implementation
// ---------------------------------------------------------------------

static void MainImpl(void)
{
    Logf("MainImpl: starting");

    // Reset per-map state + kill buffer at boot.
    Fates::ResetMapState();
    Logf("MainImpl: ResetMapState() done");

    // Install core hooks 
    Fates::HookManager::InstallCoreHooks();
    Logf("MainImpl: HookManager::InstallCoreHooks() returned");

    // Register engine-level HP + kill tracker handlers on the event bus.
    Fates::Engine::HpKillTracker_RegisterHandlers();
    Logf("MainImpl: HpKillTracker_RegisterHandlers() done");

    // Register example stats modules. These are non-invasive modules
    // that only log via the engine bus and serve as templates.
    Fates::Engine::DamageStatsModule_RegisterHandlers();
    Logf("MainImpl: DamageStatsModule_RegisterHandlers() done");

    Fates::Engine::RngStatsModule_RegisterHandlers();
    Logf("MainImpl: RngStatsModule_RegisterHandlers() done");

    // Install optional hooks as pure MITM pass-through if/when needed.
    // Do not enable for now; it may cause instability.
    // Fates::HookManager::InstallOptionalHooks();
    // Logf("MainImpl: HookManager::InstallOptionalHooks() returned");

    // Start the debug loop in this thread (no System::Thread needed).
    Logf("MainImpl: starting debug loop");
    DebugThread(nullptr);
    Logf("MainImpl: debug loop exited");
}

namespace CTRPluginFramework
{
    void main()
    {
        MainImpl();
    }

    void Main()
    {
        MainImpl();
    }
}
