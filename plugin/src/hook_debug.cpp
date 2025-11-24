#include "hook_debug.hpp"
#include "core/runtime.hpp"
#include "core/hooks.hpp"
#include "util/debug_log.hpp"
#include <CTRPluginFramework.hpp>
#include <cstdio>

using namespace CTRPluginFramework;
using namespace Fates;

void DumpKillEventsToLog()
{
    Logf("=== DumpKillEventsToLog ===");
    Logf("Total kill events: %d", gKillEventCount);

    for (int i = 0; i < gKillEventCount; ++i)
    {
        const KillEvent &ev = gKillEvents[i];

        Logf("[%d] seq=%p dead0=%p dead1=%p flags=0x%08X",
             i,
             ev.seq,
             ev.dead0,
             ev.dead1,
             ev.flags);
    }

    Logf("=== End DumpKillEventsToLog ===");
}

// namespace Fates

// gHookCount is declared in core/runtime.hpp within the Fates
// namespace. Bring it into scope via using Fates::gHookCount.

static void _EnsureDir() {
    Directory::Create("sdmc:/Fates3GX");
}

void ShowHookCountsOSD() {
    char buf[160];
    int shown = 0;

    for (std::uint32_t i = 0; i < kNumHooks; ++i) {
        if (gHookCount[i] == 0)
            continue;
        const char *name = kHooks[i].name;
        std::snprintf(buf, sizeof(buf), "%s: %u",
                      name ? name : "(unnamed)", (unsigned)gHookCount[i]);
        OSD::Notify(buf);

        if (++shown > 10)
            break;
    }

    if (shown == 0)
        OSD::Notify("No hooks hit yet");
}

void DumpHookCountsToFile() {
    _EnsureDir();

    File f;
    if (File::Open(f, "sdmc:/Fates3GX/hook_hits.log",
                   File::WRITE | File::CREATE) != 0)
    {
        OSD::Notify("Couldn't open hook_hits.log");
        return;
    }

    // Append to end of file
    f.Seek(0, File::END);

    for (std::uint32_t i = 0; i < kNumHooks; ++i) {
        const char *name = kHooks[i].name;
        char line[128];
        int n = std::snprintf(line, sizeof(line),
                              "%02u %s = %u\r\n",
                              (unsigned)i,
                              name ? name : "(unnamed)",
                              (unsigned)gHookCount[i]);
        f.Write(line, (u32)n);
    }

    f.Close();
    OSD::Notify("Wrote sdmc:/Fates3GX/hook_hits.log");
}

static void _EntryShow(MenuEntry* e) {
    (void)e;
    ShowHookCountsOSD();
}

static void _EntryDump(MenuEntry* e) {
    (void)e;
    DumpHookCountsToFile();
}

void InstallHookDebugMenu(PluginMenu& menu) {
    auto *folder = new MenuFolder("Fates 3GX Debug");
    folder->Append(new MenuEntry("Show hook counts (OSD)", nullptr, _EntryShow));
    folder->Append(new MenuEntry("Dump hook counts to file", nullptr, _EntryDump));
    menu.Append(folder);
}
