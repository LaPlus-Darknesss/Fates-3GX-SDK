# Fates-3GX-SDK Documentation

Welcome! This `docs/` folder contains documentation for the Fates-3GX-SDK
framework and plugin.

This is **not** a polished manual yet; it's an evolving set of notes meant
to help you understand how the pieces fit together.

---

## Document Map

### Engine Layer

- [`getting_started.md`] (/getting_started.md)
  Please read this first before moving anywhere else. 

- [`engine/engine_overview.md`](engine/engine_overview.md)  
  High-level overview of the "engine" layer that sits between raw hooks
  and gameplay logic. Explains `Engine::On*` entrypoints, context
  structures (MapContext, TurnContext, KillContext, etc.), and how the
  event bus works.

- [`engine/writing_modules.md`](engine/writing_modules.md)  
  Step-by-step guide to writing your own engine module (HP engine,
  skill engine, etc.) using the event bus.

- [`engine/events_reference.md`](engine/events_reference.md)  
  Reference for the engine events and context structs: what calls them,
  what data they carry, and when they fire.

---

## Planned Sections (TODO)

These will be added over time:

- Getting started with building the plugin and installing it.
- Hook reference (what each `HookId_*` does and where it lives).
- Example modules overview (`ExampleSdkModule`, damage stats, RNG stats).
- Troubleshooting and common pitfalls.

For now, the engine docs are the most up-to-date and are intended to be
good reference material for anyone using the framework.
