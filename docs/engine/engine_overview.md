# Engine Overview

The **engine layer** sits between low-level game hooks and your actual
gameplay systems (HP engine, skill engine, roguelike logic, UI overlays, etc.).

Conceptually, the flow looks like this:

[CTRPF hook stubs in hooks_handlers.cpp]
            ↓
    Engine::On* functions in engine/events.cpp
            ↓
       Event bus in engine/bus.cpp
            ↓
  Engine modules (HP tracker, examples, etc.)

Core Pieces

1. Engine Entry Points (Engine::On*)
	In engine/events.hpp / engine/events.cpp you will find functions like:

		OnMapBegin, OnMapEnd

		OnTurnBegin, OnTurnEnd
	
		OnKill

		OnRngCall

		OnUnitLevelUp

		OnUnitSkillLearn

		OnItemGain

		OnHpChange

		OnUnitHpSync

		OnActionEnd (currently log-only)
	
These are called from the hook stubs in hooks_handlers.cpp. Each
	On* function:

		Builds small snapshot structs (MapContext, TurnContext,
		KillContext, HpChangeContext, RngContext, etc.) from
		core/runtime.hpp state.

		Logs a structured debug line (with caps where needed).

		Calls into the event bus to notify any registered listeners.

2. Context Structures
	The main context types are defined in engine/events.hpp:

			MapContext
				Snapshot of map-level state: seqRoot, generation counter,
				startSide, currentSide, total turns, and kill count.

			TurnContext
				Embeds a MapContext and adds side plus sideTurnIndex
				(how many turns that side has taken).

			KillContext
				Wraps a KillEvent (from core/runtime.hpp) plus MapContext and
				TurnContext at the moment of the kill.

			HpChangeContext
				Wraps an HpEvent (source, target, amount, flags, context pointer)
				plus MapContext and TurnContext.

			RngContext
				Snapshot for each RNG call: map/turn, RNG state pointer, raw value,
				bound, and scaled result.

			LevelUpContext, SkillLearnContext, ItemGainContext
				Used for level ups, skill learns, and item gains, each with a UnitHandle
				and relevant IDs plus map/turn snapshots.
	
	These structs are intentionally small and stable so they can be passed
	around freely to multiple modules.

3. Event Bus (engine/bus.hpp / .cpp)
	The bus is a simple, fixed-capacity dispatcher. For each event "family"
	it defines:

		A handler type, e.g. using HpChangeHandler = void(*)(const HpChangeContext &);

		A registration function, e.g. bool RegisterHpChangeHandler(HpChangeHandler fn);

		A dispatch function, e.g. void DispatchHpChange(const HpChangeContext &ctx);

	Internally, the bus keeps small arrays of function pointers and a count,
	and calls them in order when an event fires. There is no dynamic
	allocation and no handler removal API.

	Modules register their handlers at startup (usually from a single
	*_RegisterHandlers() function).

4. Example Modules
	The following modules show typical engine usage patterns:

		HpKillTracker
		(Tracks map kills per side and pushes kill events into the engine.)

		ExampleSdkModule
		Demonstrates how to register handlers and print basic context info.

		DamageStatsModule
		Aggregates per-side damage, healing, and kills for each map and logs a
		summary at MapEnd.

		RngStatsModule
		Aggregates RNG calls per side and a small histogram of bound values.
	
		Reading these alongside engine/bus.hpp and engine/events.cpp is the
		recommended way to learn the engine patterns.

	How This Layer Should Be Used
	Hooks → Engine only
	Hook stubs in hooks_handlers.cpp should forward into Engine::On*
	functions and avoid doing heavy logic themselves.

	Engine modules → Bus only
	Engine modules should register handlers via the bus, receive
	context structs, and work entirely in C++ land. They should not
	need to know about CTRPF internals or inline assembly.

Future systems
When building new systems, prioritze adding a new engine module that subscribes
to existing events (or introduces a small new event) instead of
wiring directly into hooks.