// hook_manager.hpp
//
// Defines the HookManager class used to install and manage runtime
// hooks for Fire Emblem Fates.  HookManager relies on the
// CTRPluginFramework Hook API to patch game code at runtime.  It
// consumes the statically defined kHooks array (see core/hooks.hpp) and
// dispatches each entry to CTRPF along with its corresponding stub
// handler.
//
// The plugin distinguishes between core, optional and experimental
// hooks.  Core hooks are installed by default when InstallCoreHooks()
// or InstallAll() is invoked.  Optional hooks may be installed via
// InstallOptionalHooks() or by enabling a menu toggle.  Experimental
// hooks are not installed unless explicitly requested.

#pragma once

#include <CTRPluginFramework.hpp>
#include "core/hooks.hpp"
#include "core/handlers.hpp"

namespace Fates {

class HookManager {
public:
    // Initialise internal data structures.  This must be called
    // exactly once before any hooks are installed.
    static void Init();

    // Install all core hooks defined in kHooks.  This should be
    // invoked early in the plugin's initialisation sequence.
    static void InstallCoreHooks();

    // Install all optional hooks. All optional hooks are currently
	// non functional and should not be enabled without modifications.
    static void InstallOptionalHooks();

	// Install all core hooks (alias for InstallCoreHooks).
    static void InstallAll();

    // Enable or disable all installed hooks.  These wrappers allow
    // toggling all hooks at once.
    static void EnableAll();
    static void DisableAll();

    // Look up the static metadata for a given hook ID.  This simply
    // returns the corresponding entry in kHooks.  No bounds checks
    // are performed.
    static const HookEntry &GetEntry(HookId id);

private:
    // Internal helper used to map a HookId to its stub handler.  If
    // no handler is available, this returns nullptr and the hook
    // installation will be skipped.
    static void *GetHandler(HookId id);

    // Internal installation helper invoked by the public methods.
    static void InstallByStability(HookStability s);

    // Array of CTRPF Hook objects, one per HookId.  These objects
    // manage the lifecycle of the underlying patches.
    static CTRPluginFramework::Hook sHooks[(std::size_t)HookId_Count];

    // Track whether Init() has been called to avoid double
    // initialisation.
    static bool sInitialised;
};

// Helper for stub handlers to retrieve the CTRPF Hook object for a
// given hook ID.  Implemented in core/hook_manager.cpp and simply
// returns the corresponding element of HookManager::sHooks.
CTRPluginFramework::Hook &GetHookRef(HookId id);

} // namespace Fates
