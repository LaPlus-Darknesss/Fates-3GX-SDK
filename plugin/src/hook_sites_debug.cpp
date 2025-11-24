#include "core/hooks.hpp"
#include "util/debug_log.hpp"
#include <CTRPluginFramework.hpp>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

using namespace CTRPluginFramework;
using namespace Fates;

void DumpHookSites()
{
    Logf("DumpHookSites: begin (kNumHooks=%u)", (unsigned)kNumHooks);

    for (std::size_t i = 0; i < kNumHooks; ++i) {
        const HookEntry &e = kHooks[i];
        u32 siteVA = e.targetVA;
        uint8_t current[8] = {0};
        const uint8_t *p = reinterpret_cast<const uint8_t *>(siteVA);
        std::memcpy(current, p, sizeof(current));
        char curHex[3 * 8 + 1]   = {0};
        char guardHex[3 * 8 + 1] = {0};
        for (int b = 0; b < 8; ++b) {
            std::snprintf(&curHex[b * 3], 4, "%02X ", current[b]);
            // Only three guard words are available; interpret as bytes
            if (b < 12) {
                // Determine which word and which byte
                int wordIndex = b / 4;
                int byteIndex = b % 4;
                uint32_t word = e.guard[wordIndex];
                uint8_t byte = (word >> (byteIndex * 8)) & 0xFF;
                std::snprintf(&guardHex[b * 3], 4, "%02X ", byte);
            } else {
                std::snprintf(&guardHex[b * 3], 4, "-- ");
            }
        }
        const char *name = (e.name != nullptr) ? e.name : "<noname>";
        Logf("Site[%02u] %s @VA=0x%08X: cur=[%s] guard=[%s]",
             (unsigned)i, name, (unsigned)siteVA, curHex, guardHex);
    }
    Logf("DumpHookSites: end");
}
