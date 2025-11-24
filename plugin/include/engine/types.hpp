// engine/types.hpp
//
// Core "safe" primitive types for Fates-3GX engine layer.
// These wrap raw pointers and low-level data into small, stable
// structs that higher-level systems can consume without having to
// know about actual game layouts.
//
// Nothing in here touches CTRPF or concrete game structs; it's
// all header-only and uses only opaque void* pointers.

#pragma once

#include <cstdint>

namespace Fates {
namespace Engine {

/// Lightweight wrapper around a raw Unit*.
///
/// This does NOT assume anything about the Unit layout. It just
/// carries the pointer and provides a few utility helpers. Later,
/// once we have a trusted catalog of offsets, buildings blocks
/// such as HP / level / class here in one place.
struct UnitHandle
{
    void *ptr;  // opaque Unit* (may be nullptr)

    UnitHandle() : ptr(nullptr) {}
    explicit UnitHandle(void *p) : ptr(p) {}

    /// Returns true if this refers to a non-null unit pointer.
    bool IsValid() const { return ptr != nullptr; }

    /// Raw underlying pointer (for logging / low-level work).
    void *Raw() const { return ptr; }

    // Future: helpers like:
    //   int  GetLevel() const;
    //   int  GetCurrentHp() const;
    //   int  GetMaxHp() const;
    //   int  GetSide() const;
    // all implemented in terms of a small, audited offset table.
};

/// High-level view of a single battle interaction.
///
/// For now this is very conservative: I track the opaque battle
/// calculator pointer and "main" unit pointer(s). As I reverse more
/// of the combat engine, I will add weapon, stance, terrain, etc.
/// without changing the rest of the event/bus API.
struct BattleContext
{
    // Opaque battle calculator / situation pointers.
    void *calc;      // e.g. map__BattleCalculator*, Situation*, etc.
    void *root;      // e.g. BattleRoot* (if available), may be nullptr.

    // Participants (attacker/defender) as abstract handles.
    UnitHandle attacker;
    UnitHandle defender;

    // Future: weapon, stance, terrain, flags...
    std::uint32_t flags;  // generic battle flags (semantics TBD)

    BattleContext()
        : calc(nullptr)
        , root(nullptr)
        , attacker()
        , defender()
        , flags(0)
    {
    }
};

/// Canonical representation of an HP change event.
///
/// This is the *local* event ("X did N to Y") without any map/turn
/// context. The engine will usually wrap this inside a higher-level
/// context that adds MapContext / TurnContext when dispatching.
struct HpEvent
{
    UnitHandle source;  ///< who caused the change (may be null for terrain, etc.)
    UnitHandle target;  ///< whose HP changed

    // Signed delta in "target HP" space:
    //   > 0  = damage taken
    //   < 0  = healing received
    //   = 0  = no-op / special case
    int amount;

    // Generic flags / cause code. This is intentionally vague
    // for now; later I will standardise subfields (bits for terrain,
    // skill id, weapon id, poison, etc.).
    std::uint32_t flags;

    // Optional opaque context pointer (sequence, proc, etc.).
    void *context;

    HpEvent()
        : source()
        , target()
        , amount(0)
        , flags(0)
        , context(nullptr)
    {
    }

    HpEvent(const UnitHandle &src,
            const UnitHandle &tgt,
            int               amt,
            std::uint32_t     fl,
            void             *ctx)
        : source(src)
        , target(tgt)
        , amount(amt)
        , flags(fl)
        , context(ctx)
    {
    }
};

} // namespace Engine
} // namespace Fates
