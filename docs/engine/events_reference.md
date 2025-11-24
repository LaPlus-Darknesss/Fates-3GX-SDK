# Engine Events Reference

This file documents the main `Engine::On*` entrypoints and the context
structs they use. It is intended as a quick reference when writing or
debugging engine modules.

> **Note:** This is a living document. Some sections are placeholders
> and will be expanded as the SDK stabilizes.

---

## Map Events

### `OnMapBegin(void *seqRoot, TurnSide side)`

- Called from the map-start hook.
- Builds a `MapContext` snapshot and dispatches it via the bus:
  `DispatchMapBegin(const MapContext &ctx)`.

### `OnMapEnd(void *seqRoot, TurnSide side)`

- Called when a map ends.
- Builds a `MapContext` snapshot and dispatches:
  `DispatchMapEnd(const MapContext &ctx)`.

---

## Turn Events

### `OnTurnBegin(TurnSide side)`

- Called from the turn-begin hook.
- Builds a `TurnContext` that embeds a `MapContext` and computes
  `sideTurnIndex` from `gMapState.turnCount[]`.
- Dispatches `DispatchTurnBegin(const TurnContext &ctx)`.

### `OnTurnEnd(TurnSide side, void *seqMaybe)`

- Called from the turn-end hook.
- Builds a `TurnContext` and dispatches
  `DispatchTurnEnd(const TurnContext &ctx)`.

---

## Kill Events

### `OnKill(const KillEvent &ev, TurnSide side)`

- Called from `Hook_HP_KillCheck` when at least one real kill is
  detected.
- Builds a `KillContext` (embedding `KillEvent`, `MapContext`,
  and `TurnContext`) and calls `DispatchKill(const KillContext &ctx)`.

---

## HP Events

### `OnUnitHpSync(void *unit, int newHp)`

- Called from `Hook_UNIT_UpdateCloneHP` after the game's own HP sync.
- Maintains a per-map `lastHp` map in the engine and derives a delta
  (`prev - newHp`).
- If there is a change, delegates to `OnHpChange(...)`.

### `OnHpChange(void *sourceUnit,
                void *targetUnit,
                int  amount,
                std::uint32_t flags,
                void *context,
                TurnSide side)`

- Builds an `HpEvent` (source, target, amount, flags, context pointer).
- Wraps it in `HpChangeContext` (map + turn snapshot).
- Dispatches `DispatchHpChange(const HpChangeContext &ctx)`.

> Convention: `amount > 0` = damage taken, `amount < 0` = healing.

---

## RNG Events

### `OnRngCall(void *state,
               std::uint32_t raw,
               std::uint32_t bound,
               std::uint32_t result)`

- Called from the RNG hook (`SYS_Rng32`).
- Builds an `RngContext` (map, turn, state pointer, raw value, bound,
  scaled result).
- Dispatches `DispatchRngCall(const RngContext &ctx)`.

---

## Unit Events

### `OnUnitLevelUp(void *unit,
                   std::uint8_t level,
                   TurnSide side)`

- Called from the level-up hook after the level has been applied.
- Builds `LevelUpContext` and dispatches
  `DispatchLevelUp(const LevelUpContext &ctx)`.

### `OnUnitSkillLearn(void *unit,
                      std::uint16_t skillId,
                      std::uint16_t flags,
                      int result,
                      TurnSide side)`

- Called from `Hook_UNIT_SkillLearn` after `Unit__AddEquipSkill` returns.
- Builds `SkillLearnContext` and dispatches
  `DispatchSkillLearn(const SkillLearnContext &ctx)`.

---

## Item Events

### `OnItemGain(void *seqHelper,
                void *unit,
                void *itemArg,
                void *modeOrCtx,
                int   result,
                TurnSide side)`

- Called from `Hook_SEQ_ItemGain`.
- Builds `ItemGainContext` and dispatches
  `DispatchItemGain(const ItemGainContext &ctx)`.

---

## Action Events

### `OnActionEnd(void *inst,
                 void *seqMap,
                 void *cmdData,
                 std::uint32_t cmdId,
                 std::uint32_t sideRaw,
                 TurnSide side,
                 std::uint32_t unk28)`

- Called from `Hook_EVENT_ActionEnd`.
- Currently **log-only**: builds map/turn snapshots and prints a
  structured debug line capped at 32 calls.
- No bus dispatch yet. Future versions may introduce a dedicated
  `ActionContext` and event family if needed, keep this in mind when using this. 

