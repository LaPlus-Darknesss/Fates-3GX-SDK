# docs/engine/writing_modules.md – how to make your own module

# Writing Engine Modules

This guide shows how to write a simple engine module that reacts to
events (map, turn, HP, kills, RNG, etc.) using the event bus.

The pattern is:

1. Decide which events you care about.
2. Define a small static state struct (or stay stateless).
3. Write handler functions with the correct signatures.
4. Register those handlers via `Register*Handler` in a single
   `*_RegisterHandlers()` function.
5. Call `*_RegisterHandlers()` from your main init code.

---

## 1. Minimal Example

A tiny module that logs when maps begin and end:


# // my_module.hpp
#pragma once

namespace Fates {
namespace Engine {

bool MyModule_RegisterHandlers();

} // namespace Engine
} // namespace Fates

---

# // my_module.cpp
#include "engine/bus.hpp"
#include "util/debug_log.hpp"

namespace Fates {
namespace Engine {

namespace {

static void HandleMapBegin(const MapContext &ctx)
{
    Logf("MyModule: MapBegin seq=%p gen=%u startSide=%s",
         ctx.seqRoot,
         static_cast<unsigned>(ctx.generation),
         TurnSideToString(ctx.startSide));
}

static void HandleMapEnd(const MapContext &ctx)
{
    Logf("MyModule: MapEnd gen=%u totalTurns=%u kills=%u",
         static_cast<unsigned>(ctx.generation),
         static_cast<unsigned>(ctx.totalTurns),
         static_cast<unsigned>(ctx.killEvents));
}

} // anonymous namespace

bool MyModule_RegisterHandlers()
{
    bool ok = true;

    ok = ok && RegisterMapBeginHandler(&HandleMapBegin);
    ok = ok && RegisterMapEndHandler(&HandleMapEnd);

    return ok;
}

} // namespace Engine
} // namespace Fates

---

Then, in main plugin init (after the hook manager is set up):

#include "my_module.hpp"

void MainImpl()
{
    // ... hook installation, other engine init ...

    Fates::Engine::MyModule_RegisterHandlers();

    // ...
}


# 2. Modules With States

Most modules will keep some per-map or global state.

Typical pattern:

---

namespace {

struct MyState
{
    int exampleCounter = 0;
};

static MyState gState;

static void HandleMapBegin(const MapContext &ctx)
{
    (void)ctx;
    gState = MyState{}; // reset state each map
}

static void HandleHpChange(const HpChangeContext &ctx)
{
    if (ctx.core.amount > 0)
        ++gState.exampleCounter;
}

} // anonymous namespace

---

This keeps state private to the module file and avoids accidental
cross-module coupling.


# 3. Handler Signatures
Each event type has a specific handler signature, defined in
engine/bus.hpp:

	void (*)(const MapContext &) // MapBegin, MapEnd

	void (*)(const TurnContext &) // TurnBegin, TurnEnd

	void (*)(const KillContext &) // Kill

	void (*)(const HpChangeContext &) // HpChange

	void (*)(const RngContext &) // RngCall

	void (*)(const LevelUpContext &) // LevelUp

	void (*)(const SkillLearnContext &) // SkillLearn

	void (*)(const ItemGainContext &) // ItemGain

Your handler functions must match these signatures exactly.


# 4. Registration Patterns

Registration functions follow the same style:

---

bool RegisterHpChangeHandler(HpChangeHandler fn);
bool RegisterKillHandler(KillHandler fn);
// ...

---

They:

	Return true on success.

	Return false if the capacity for that event type is full.

	Log a line like:
	
	Engine::RegisterHpChangeHandler: registered handler #3

Best practice in your module:

---

bool MyModule_RegisterHandlers()
{
    bool ok = true;

    ok = ok && RegisterMapBeginHandler(&HandleMapBegin);
    ok = ok && RegisterMapEndHandler(&HandleMapEnd);
    ok = ok && RegisterHpChangeHandler(&HandleHpChange);

    if (!ok)
    {
        Logf("MyModule_RegisterHandlers: WARNING: some registrations failed");
    }

    return ok;
}

---

# 5. Learning From Existing Modules 

Good reference modules in the codebase:

	HpKillTracker
	how to maintain per-map state and respond to kills.

	ExampleSdkModule
	Shows basic usage of multiple event types for logging.

	DamageStatsModule
	Aggregates per-side damage, heals, and kills, and prints a map summary.

	RngStatsModule
	Tracks RNG calls per side and a small histogram of bounds.

Reading these together with engine/bus.hpp and engine/events.cpp is
the recommended way to learn how to build more complex systems.