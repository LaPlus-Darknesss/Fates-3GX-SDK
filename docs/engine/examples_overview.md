# Engine example modules

The Fates-3GX SDK ships with a small set of **example engine modules**.  
These are meant to be:

- Tiny and self-contained
- Easy to read even if you’re new to C++
- Good starting templates for your own modules

All examples live under:

- `plugin/src/engine/`
- `plugin/include/engine/`

and are wired up from `plugin/src/main.cpp`.

> **Important:** These modules are not "features" of the final mod.  
> They are **reference code** you can delete, copy, or rewrite for your own project.

---

## Quick summary

| Module            | Files (src/include)                              | What it demonstrates                                        |
|-------------------|--------------------------------------------------|-------------------------------------------------------------|
| HpKillTracker     | `engine/hp_kill_tracker.hpp/.cpp`               | Basic use of map + kill + HP events and global counters     |
| DamageStatsModule | `engine/damage_stats_module.hpp/.cpp`           | Tracking total damage per side using `HpChangeContext`      |
| RngStatsModule    | `engine/rng_stats_module.hpp/.cpp`              | Listening to `RngContext` and building simple RNG stats     |

> If your filenames differ slightly, adjust this table to match your repo layout.

---

## Shared concepts

All example modules follow the same basic pattern:

---

1. **Declare a registration function** in the header, e.g.
	namespace Fates {
   namespace Engine {

   bool HpKillTracker_RegisterHandlers();

   } // namespace Engine
   } // namespace Fates
   
---

2. **Implement that function** in the .cpp, registering callbacks with the engine bus:

---

using namespace Fates::Engine;

bool HpKillTracker_RegisterHandlers()
{
    bool ok = true;
    ok &= RegisterMapBeginHandler(&OnMapBegin);
    ok &= RegisterMapEndHandler(&OnMapEnd);
    ok &= RegisterHpChangeHandler(&OnHpChange);
    ok &= RegisterKillHandler(&OnKill);
    // optional: log success/failure
    return ok;
}

---

3. Each handler takes one of the **context structs** defined in
plugin/include/engine/events.hpp, example:

---

void OnMapBegin(const MapContext &ctx);
void OnKill(const KillContext &ctx);
void OnHpChange(const HpChangeContext &ctx);
void OnRngCall(const RngContext &ctx);

---

4. **Startup** wires the module in by calling the register function once:

---

// in plugin/src/main.cpp, after HookManager is initialized:
if (Fates::Engine::HpKillTracker_RegisterHandlers())
    Log("HpKillTracker_RegisterHandlers: registered OK");
	
---

If you're writing your own engine module, the easiest path is:

	Pick the example that looks closest to what you want

	Copy its header + source into new module.hpp/.cpp files

	Rename the registration function + handler functions

	Change which events you listen to and what you log




**HpKillTracker**

**Files:**
	plugins/include/engine/hp_kill_tracker.hpp
	plugins/src/engine/hp_kill_tracker.cpp
	
**Purpose:**

A minimal proof of concept for the engine bus that:
	
	Keeps track of total kills per map and per side
	
	Demonstrates how to:

		Use MapContext and KillContext
		
		Maintain simple global counters across a map
		
		Print debug information when kills happen
		
**Events used:**
	
	MapBeginHandler (MapContext)
	
	MapEndHandler (MapContext)
	
	KillHandler (KillContext)
	
	Optionally HpChangeHandler (HpChangeContext) if you decide to enable it
	
**What it does:**

	On **map start**
	
		Resets internal counters: totalKills, kills per side, generation, etc.
		
		Logs something like:
		
		HpKillTracker: MapBegin gen=1 seq=0x327e0838
		
	On **kill:**:
	
		Increments totalKills and per-side counters
		
		Logs each kill, including which side scored it and the running totals:
		
		Hook_HP_KillCheck: ... totalKills=8 [S0=0 S1=7 S2=1 S3=0]
		
	On **map end**:
	
		Prints a small summary for the map (total kills by side)
		
		Very good template for any "end-of-map statistics" module. 
		
		
**DamageStatsModule**

**Files:**
	
	plugin/include/engine/damage_stats_module.hpp
	plugin/src/engine/damage_stats_module.cpp
	
**Purpose:** 

	An example on how to use the HP change event (HpChangeContext) to build basic **damage/healing statistics** over the course of a map.
	
**Events used:**

	MapBeginHandler (MapContext)
	
	MapEndHandler (MapContext)
	
	HpChangeHandler (HpChangeContext
	
**What it does:**

	On **map start**:
	
		Resets per side and total counters for:
			Damage dealt
			Damage taken
			Healing (If you choose to treat negative HP change differently)
	On **HP change**
		
		Inspects HpChangeContext:
		
			ctx.amount (signed; healing vs damage
		
			ctx.side (which side the unit belongs to)
		
		Updates its internal stats and prints debug logs such as:
		
			SkillEngine[Debug]: HpChange unit=0x32627ed0 amt=19 flags=0x00000000 gen=1 side=Side1 sideTurn=0
			
		This wiring is deliberately simple so you can repurpose it for any "react to HP changes" logic (proc skills, custom mechanics, etc.)
		
	On **map end**:
		Prints a summary block with total damage per side.
		
	
**RngStatsModule**

**Files:**
	plugin/include/engine/rng_stats_module.hpp
	plugin/src/engine/rng_stats_module.cpp

**Purpose:**
	
	An example on how to listen to **raw RNG calls** via RngContext and collect simple statistics. 
	
**Events used:**

	MapBeginHandler(MapContext)
	MapEndHandler (MapContext)
	RngHandler (RngContext)

**What it does:**
	
	On **map start:**
		Resets RNG counters and logs:
		
			RngStatsModule: reset for new map (gen=1, startSide=Side1)
			
	On **each RNG call**:
		Receives a RngContext containing:
			ctx.state – internal RNG state pointer
			ctx.rawValue – raw 32-bit RNG result
			ctx.bound – modulus (% bound) value
			ctx.result – final value used by the game (0..bound-1)
			
		Updates counters (e.g. total calls, observed distribution), and logs line such as:
			
			Engine::OnRngCall: state=0xfffff38 raw=1FFACFB7 bound=11 -> 1 gen=0 side=Unknown sideTurn=0 totalTurns=0 (n=3)
			
	On **map end:**
		
		Prints a summary of how many RNG calls happened and optionally simple frequency info. 
		
		
		
**Using these examples as templates:**
If you want to create your own module:

	1. **Copy a module:**
		Duplicate hp_kill_tracker.hpp/.cpp or damage_stats_module.hpp/.cpp

		Rename them to something like my_epic_module.hpp/.cpp
	
	2. **Change the registration function name in both header and source:**
	
		bool MyEpicModule_RegisterHandlers();

	3. **Register only the events you care about,** e.g.
		
		---
		
		bool MyEpicModule_RegisterHandlers()
	{
		bool ok = true;
		ok &= RegisterMapBeginHandler(&OnMapBegin);
		ok &= RegisterTurnBeginHandler(&OnTurnBegin);
		ok &= RegisterTurnEndHandler(&OnTurnEnd);
    return ok;
	}
	
		---
		
	4. **Wire it into startup** (plugins/src/main.cpp):
		
			if (Fates::Engine::MyEpicModule_RegisterHandlers())
				Log("MyEpicModule_RegisterHandlers: handlers registered");
			
	
	5. **Build and run the game, watch the log for module tag lines and outputs.**
	
		That's it—that's the entire pattern the engine is built around:
			
			hooks → Engine::On → event bus → your module handlers.*

	

