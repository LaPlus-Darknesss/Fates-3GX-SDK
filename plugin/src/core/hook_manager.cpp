// hook_manager.cpp
//
// Centralised runtime control for all gameplay hooks.
//
// Responsibilities:
//  - Build CTRPF Hook objects from the compile-time kHooks table.
//  - Install subsets of hooks by HookStability (Core / Optional / Experimental).
//  - Verify guard words before patching to protect against mismatched code.bin.
//  - Provide helpers to enable/disable all hooks at once.


#include <CTRPluginFramework.hpp>
#include "core/hook_manager.hpp"
#include "core/hooks.hpp"
#include "core/handlers.hpp"
#include "util/debug_log.hpp"



namespace Fates
{
    using CTRPluginFramework::Hook;

    // Static member definitions
    Hook HookManager::sHooks[static_cast<std::size_t>(HookId_Count)]{};
    bool HookManager::sInitialised = false;

    // Simple guard verifier: compare the first 3 words at targetVA
    // against the guard[] pattern from the catalog.
static bool VerifyGuard(const HookEntry &entry)
{
    // Always compare guards against a T-bit–cleared, 4-byte-aligned VA.
    const u32 baseVA = entry.targetVA & ~1u;
    const auto *cur  = reinterpret_cast<const std::uint32_t *>(baseVA);

    std::uint32_t current[3];
    if (entry.guard[0] || entry.guard[1] || entry.guard[2])
    {
        current[0] = cur[0];
        current[1] = cur[1];
        current[2] = cur[2];

        if (current[0] != entry.guard[0] ||
            current[1] != entry.guard[1] ||
            current[2] != entry.guard[2])
        {
            Logf("HookManager: guard mismatch for %s at 0x%08lX "
                 "(cur=%08lX %08lX %08lX, exp=%08lX %08lX %08lX)",
                 entry.name,
                 static_cast<unsigned long>(baseVA),
                 current[0], current[1], current[2],
                 entry.guard[0], entry.guard[1], entry.guard[2]);
            return false;
        }
    }

    return true;
}


    void HookManager::Init()
    {
        if (sInitialised)
            return;

        sInitialised = true;

        // Default-construct all Hook objects
        for (auto &h : sHooks)
            h = Hook();
    }

    const HookEntry &HookManager::GetEntry(HookId id)
    {
        return kHooks[static_cast<std::size_t>(id)];
    }

    void *HookManager::GetHandler(HookId id)
    {
        // Map HookId -> C stub.  These stubs are declared in
        // core/handlers.hpp and implemented in hooks_handlers.cpp.
        switch (id)
        {
            case HookId_BTL_HitCalc_Main:
                return reinterpret_cast<void *>(&Hook_BTL_HitCalc_Main);
            case HookId_BTL_CritCalc_Main:
                return reinterpret_cast<void *>(&Hook_BTL_CritCalc_Main);
            case HookId_BTL_FinalDamage_Pre:
                return reinterpret_cast<void *>(&Hook_BTL_FinalDamage_Pre);
            case HookId_BTL_FinalDamage_Post:
                return reinterpret_cast<void *>(&Hook_BTL_FinalDamage_Post);
            case HookId_BTL_GuardGauge_Add:
                return reinterpret_cast<void *>(&Hook_BTL_GuardGauge_Add);
            case HookId_BTL_GuardGauge_Spend:
                return reinterpret_cast<void *>(&Hook_BTL_GuardGauge_Spend);
            case HookId_SEQ_HpDamage:
				return reinterpret_cast<void *>(&Hook_SEQ_HpDamage);
			case HookId_UNIT_HpDamage:
				return reinterpret_cast<void *>(&Hook_UNIT_HpDamage);
			case HookId_UNIT_UpdateCloneHP:
				return reinterpret_cast<void *>(&Hook_UNIT_UpdateCloneHP);
            case HookId_HP_KillCheck:
                return reinterpret_cast<void *>(&Hook_HP_KillCheck);
            case HookId_SEQ_HpDamage_Helper:
                return reinterpret_cast<void *>(&Hook_SEQ_HpDamage_Helper);
			case HookId_SEQ_ItemGain:
				return reinterpret_cast<void *>(&Hook_SEQ_ItemGain);
            case HookId_MAP_ProcSkillDamage:
                return reinterpret_cast<void *>(&Hook_MAP_ProcSkillDamage);
            case HookId_MAP_ProcTerrainDamage:
                return reinterpret_cast<void *>(&Hook_MAP_ProcTerrainDamage);
            case HookId_MAP_ProcTrickDamage:
                return reinterpret_cast<void *>(&Hook_MAP_ProcTrickDamage);
            case HookId_EVENT_ActionEnd:
                return reinterpret_cast<void *>(&Hook_EVENT_ActionEnd);
            case HookId_BTL_AttackStance_Check:
                return reinterpret_cast<void *>(&Hook_BTL_AttackStance_Check);
            case HookId_BTL_AttackStance_ApplySupport:
                return reinterpret_cast<void *>(&Hook_BTL_AttackStance_ApplySupport);
            case HookId_HUD_Battle_HPGaugeUpdate:
                return reinterpret_cast<void *>(&Hook_HUD_Battle_HPGaugeUpdate);
            case HookId_BTL_SkillEffect_Apply:
                return reinterpret_cast<void *>(&Hook_BTL_SkillEffect_Apply);
			case HookId_SYS_Rng32:
                return reinterpret_cast<void *>(&Hook_SYS_Rng32);
			case HookId_SEQ_TurnBegin:
				return reinterpret_cast<void *>(&Hook_SEQ_TurnBegin);
			case HookId_SEQ_TurnEnd:
				return reinterpret_cast<void *>(&Hook_SEQ_TurnEnd);
			case HookId_SEQ_MapEnd:
				return reinterpret_cast<void *>(&Hook_SEQ_MapEnd);
			case HookId_SEQ_MapStart:
				return reinterpret_cast<void *>(&Hook_SEQ_MapStart);
			case HookId_SEQ_ItemUse:
				return reinterpret_cast<void *>(&Hook_SEQ_ItemUse);
			case HookId_UNIT_LevelUp:
				return reinterpret_cast<void *>(&Hook_UNIT_LevelUp);
			case HookId_UNIT_SkillLearn:
				return reinterpret_cast<void *>(&Hook_UNIT_SkillLearn);
			case HookId_SEQ_UnitMove:
				return reinterpret_cast<void *>(&Hook_SEQ_UnitMove);
			
            default:
                return nullptr;
        }
    }

    void HookManager::InstallByStability(HookStability desired)
    {
        if (!sInitialised)
            Init();

        Logf("HookManager::InstallByStability(%d) - begin",
             static_cast<int>(desired));

        for (std::size_t i = 0; i < kNumHooks; ++i)
        {
            const HookEntry &entry = kHooks[i];

            if (entry.stability != desired)
                continue;

            void *handler = GetHandler(entry.id);
            if (handler == nullptr)
            {
                Logf("HookManager: no handler for '%s' (id=%d)",
                     entry.name, static_cast<int>(entry.id));
                continue;
            }

            if (!VerifyGuard(entry))
            {
                Logf("HookManager: guard check failed for '%s'; skipping",
                     entry.name);
                continue;
            }

Hook &hook = sHooks[static_cast<std::size_t>(entry.id)];

// Canonical, T-bit–cleared VA 
const u32 rawVA        = entry.targetVA & ~1u;
u32       targetAddr   = rawVA;
const u32 callbackAddr = reinterpret_cast<u32>(handler);

// Enforce T-bit based on isThumb.
//  - ARM  : even address
//  - Thumb: odd address
if (entry.isThumb)
    targetAddr |= 1u;
else
    targetAddr &= ~1u;

Logf("HookManager: installing '%s' (MITM) raw=0x%08lX hookVA=0x%08lX -> 0x%08lX (thumb=%s)",
     entry.name,
     static_cast<unsigned long>(rawVA),
     static_cast<unsigned long>(targetAddr),
     static_cast<unsigned long>(callbackAddr),
     entry.isThumb ? "true" : "false");

// MITM mode so HookContext::OriginalFunction works
hook.InitializeForMitm(targetAddr, callbackAddr);
auto result = hook.Enable();
Logf("HookManager: '%s' Enable() -> %d",
     entry.name, static_cast<int>(result));

        }

        Logf("HookManager::InstallByStability(%d) - end",
             static_cast<int>(desired));
    }

    void HookManager::InstallCoreHooks()
    {
        InstallByStability(HookStability::Core);
    }

    void HookManager::InstallOptionalHooks()
    {
		// Install all hooks marked HookStability::Optional.
		// WARNING: most Optional hooks are RE candidates or unstable and
		// should only be enabled when you know what you’re doing.
        InstallByStability(HookStability::Optional);
    }

    void HookManager::InstallAll()
    {
        // "All" = Core Hooks only.
        InstallCoreHooks();
    }

    void HookManager::EnableAll()
    {
        for (std::size_t i = 0; i < kNumHooks; ++i)
            sHooks[i].Enable();
    }

    void HookManager::DisableAll()
    {
        for (std::size_t i = 0; i < kNumHooks; ++i)
            sHooks[i].Disable();
    }

} // namespace Fates
