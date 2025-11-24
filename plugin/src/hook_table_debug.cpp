#include "core/hooks.hpp"
#include "core/runtime.hpp"
#include "util/debug_log.hpp"
#include <CTRPluginFramework.hpp>
#include <cstddef>

using namespace CTRPluginFramework;

using namespace Fates;

void DumpHookTable()
{

    Logf("DumpHookTable: begin (kNumHooks=%u)", (unsigned)kNumHooks);

    if (kNumHooks == 0) {
        Logf("DumpHookTable: kHooks is empty");
        return;
    }

    for (std::size_t i = 0; i < kNumHooks; ++i) {
        const HookEntry &e = kHooks[i];
        const char *name = (e.name != nullptr) ? e.name : "<noname>";
        // Print the target virtual address and file offset, guard words,
        // Thumb flag and stability class.
        Logf("Hook[%02u]: %s VA=0x%08X fileOff=0x%08X guard={%08X,%08X,%08X} thumb=%s stability=%u",
             (unsigned)i,
             name,
             (unsigned)e.targetVA,
             (unsigned)e.fileOffset,
             (unsigned)e.guard[0], (unsigned)e.guard[1], (unsigned)e.guard[2],
             e.isThumb ? "yes" : "no",
             (unsigned)e.stability);
    }

    Logf("DumpHookTable: end");
}
