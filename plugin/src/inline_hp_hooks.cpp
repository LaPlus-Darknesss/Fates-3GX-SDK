// plugin/src/inline_hp_hooks.cpp
//
// Runtime "inline" hooks for map__BattleCalculator__CalculateAttackSingle.
// These are *not* normal HookManager entries; we patch the code directly
// using an absolute jump pattern:
//
//   ldr pc, [pc, #-4]
//   .word stubVA
//
// so we can detour mid-function to a naked stub in the plugin.

#include "pf.hpp"
#include "util/debug_log.hpp"

using namespace CTRPluginFramework;

// Hard-coded VAs for NA v1.1 code.bin (AttackSingle HP-delta sites).
static constexpr u32 kAttackSingle_SiteA_VA      = 0x003643FCu; // STR r0,[r8,#4]
static constexpr u32 kAttackSingle_SiteA_Resume  = 0x00364404u; // after MOVLT
static constexpr u32 kAttackSingle_SiteB_VA      = 0x00364EC8u; // STR r0,[r8,#4]
static constexpr u32 kAttackSingle_SiteB_Resume  = 0x00364ED0u; // after MOVLT

// ---------------------------------------------------------------------
// Naked stubs – one per inline site
//
// At entry, we have the exact register/flag state that existed at the
// original STR instruction in AttackSingle. For now, we just re-emit
// the vanilla instructions and jump back into the function.
//
// Later, we can insert our own logic before the STR and re-run CMP to
// preserve condition flags before MOVLT / BLT.
// ---------------------------------------------------------------------

extern "C" void Inline_AttackSingle_HpDelta_SiteA() __attribute__((naked));
extern "C" void Inline_AttackSingle_HpDelta_SiteB() __attribute__((naked));

// Site A: 003643FC / 00364400, resume at 00364404
extern "C" void Inline_AttackSingle_HpDelta_SiteA()
{
    asm volatile(
        // Vanilla:
        //   003643FC: str r0,[r8,#4]
        //   00364400: movlt r0,#0
        //   00364404: blt 0x00364440
        //
        // We re-issue the STR + MOVLT and then jump to 00364404.

        "str   r0, [r8,#4]\n"
        "movlt r0, #0\n"
        "ldr   r12, =%c[resume]\n"
        "mov   pc, r12\n"
        :
        : [resume] "i"(kAttackSingle_SiteA_Resume)
    );
}

// Site B: 00364EC8 / 00364ECC, resume at 00364ED0
extern "C" void Inline_AttackSingle_HpDelta_SiteB()
{
    asm volatile(
        // Vanilla:
        //   00364EC8: str r0,[r8,#4]
        //   00364ECC: movlt r0,#0
        //   00364ED0: blt 0x00364F0C
        //
        // Same pattern: keep STR + MOVLT, then continue at 00364ED0.

        "str   r0, [r8,#4]\n"
        "movlt r0, #0\n"
        "ldr   r12, =%c[resume]\n"
        "mov   pc, r12\n"
        :
        : [resume] "i"(kAttackSingle_SiteB_Resume)
    );
}

// ---------------------------------------------------------------------
// Inline patch installer
//
// We overwrite the two STR/MOVLT instruction pairs with:
//
//   003643FC: ldr pc, [pc, #-4]   ; 0xE51FF004
//   00364400: .word stubVA
//
// (same pattern make_inline.py uses via asm_abs_jump_va).
// ---------------------------------------------------------------------

static inline void InstallInlineDetour(u32 siteVA, u32 stubVA)
{
    // ldr pc, [pc,#-4]
    constexpr u32 kLdrPcLiteral = 0xE51FF004u;

    // Overwrite 8 bytes at the ARM site.
    Process::Write32(siteVA,     kLdrPcLiteral);
    Process::Write32(siteVA + 4, stubVA);
}

void InstallAttackSingleInlineHpHooks()
{
    static bool sInstalled = false;
    if (sInstalled)
        return;
    sInstalled = true;

    // Patch both inline HP-delta sites to jump to our stubs.
    InstallInlineDetour(kAttackSingle_SiteA_VA,
        reinterpret_cast<u32>(&Inline_AttackSingle_HpDelta_SiteA));
    InstallInlineDetour(kAttackSingle_SiteB_VA,
        reinterpret_cast<u32>(&Inline_AttackSingle_HpDelta_SiteB));

    Logf("Inline HP hooks installed at %08X (A), %08X (B)",
         kAttackSingle_SiteA_VA, kAttackSingle_SiteB_VA);
}
