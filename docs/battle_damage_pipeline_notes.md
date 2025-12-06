# Battle Damage Pipeline Notes — Fates SE NA 

## Key Functions

- `map__BattleCalculator__Calculate` @ 0x00364FCC
  - Top-level battle calculator entry point.

- `map__BattleCalculator__CalculateAttack` @ 0x00361F70
  - Per-slot attack calculator.
  - Sets up proc/effect candidates for an attack slot.
  - Uses `gBattleAttackGlobals` @ 0x362278.
  - Schedules events via `0x00361ED8`.
  - Calls `BTL_FinalDamage_Pre` (0x003628BC) as a *late* HP validation stage.

- `map__BattleInfo__CalculateDetail` @ 0x003475BC
  - Full detail forecast builder. (See separate dump notes.)

- `map__BattleInfo__CalculateSimple` @ 0x00347C3C
  - Simple forecast builder (the one-line UI forecast).
  - Loops 4 × 0x200-byte `BattleAttackEntry` structs.
  - Computes:
    - Hit% → `entry->hitPercent` @ +0x34.
    - Damage gauge → `entry->damageGauge` @ +0x38.
    - Damage forecast → `entry->damageForecast` @ +0x3C.

- `BTL_HitCalc_Main` @ 0x003A3588
  - Main battle hit/damage routine (hooked by our engine).
  - Uses `DamageOrigin::BattleMain` in our engine.

- `BTL_FinalDamage_Pre` @ 0x003628BC
  - Final HP pipeline stage.
  - **Do not** use as forecast hook.
  - Only for late validation / observation.

- `Unit__HasSkillById` @ 0x0052DD04
  - Confirmed earlier.
  - Used by `CalculateSimple` to set `entry->specialSkillFlag` @ +0x40.

## Global Structures

- `gBattleInfoGlobals` @ 0x347F7C
  - +0x1C8 / +0x1E8: 64-bit masks for hit% gating.
  - +0x2C: index into a bitset table used to half the damage gauge (`entry->damageGauge`).
  - +0x34: skill ID used for forecast skill-flag check.
  - +0xD8, +0xDC, +0x100: feature flags toggling optional logic.

- `gBattleAttackGlobals` @ 0x362278
  - +0x4, +0x6, ..., +0x14: thresholds/IDs used by `CalculateAttack`.
  - +0x70..+0x84: initialization flags.

## BattleAttackEntry (per-slot, size 0x200)

Base pointer: `BattleInfoRoot* BI;`

- For slot `i` (0..3):
  - `BattleAttackEntry* entry = (BattleAttackEntry*)((uint8_t*)BI + i * 0x200);`

Key offsets:

- +0x00: `typeOrSide`
- +0x04: `unit` (Unit*)
- +0x10: bitset tag / descriptor (0 == no data)
- +0x28: flags (`0x40`, `0x4000`, etc.)
- +0x2C: detail pointer from `0x005253A8`
- +0x30: calc pointer from `0x00525090`
- +0x34: `hitPercent` (clamped 0..100)
- +0x38: `damageGauge` (300 / 400 / halved)
- +0x3C: **`damageForecast`** (return of `0x005259D4`)
- +0x40: `specialSkillFlag` (bool, from `Unit__HasSkillById`)

## Engine Front Doors (planned)

- Forecast front door:
  - Hook: `map__BattleInfo__CalculateSimple` (post-vanilla).
  - For each `BattleAttackEntry`:
    - Base damage = `entry->damageForecast`.
    - Origin = `DamageOrigin::ForecastSimple`.

- Battle front door:
  - Hook: `BTL_HitCalc_Main`.
  - Origin = `DamageOrigin::BattleMain`.

- Late HP-stage observer:
  - Hook: `BTL_FinalDamage_Pre`.
  - Origin = `DamageOrigin::FinalHp`.
  - Observation/validation only; no primary rules here.

## Battle preview / damage pipeline (NA v1.1, working map)

### Top-level flow

- `map__BattleCalculator__Calculate @ 0x00364FCC`
  - Top-level entry for map battle calculations.
  - Orchestrates building the preview, calling:
    - `map__BattleCalculator__CalculateAttack @ 0x00361F70`
    - `map__BattleInfo__CalculateSimple @ 0x00347C3C`
    - `map__BattleInfo__CalculateDetail @ 0x003475BC`
    - RNG helpers and other support code.

- `map__BattleCalculator__CalculateAttack @ 0x00361F70`
  - Core attack loop for preview.
  - Arguments:
    - R0 = pointer to battle calculator state (global-ish object).
    - R1 = attack pattern index (0..3).
  - Walks candidate attack-count bands using a set of short values loaded from
    `gBattleCalcAttackGlobals @ 0x00362278` (offsets +0x4, +0x6, +0x8, ...).
  - For each candidate, it:
    - Checks various conditions via a helper at `0x003A34B4`.
    - Uses a table block at `gBattleCalcRandomTables @ 0x003627F8` together with
      calls to `0x0044ADF8` and `0x005324B8` to pick the "best" candidate.
    - Once a candidate passes, calls `BTL_FinalDamage_Pre @ 0x003628BC` one or
      more times with a short damage value to handle the late HP application.
  - Important note: `BTL_FinalDamage_Pre` is **post-forecast** in terms of
    decision making. It can see the final damage, but changing it here does
    not feed back into the forecast numbers that the UI shows.

### Per-slot forecast (BattleInfo)

- `map__BattleInfo__CalculateSimple @ 0x00347C3C`
  - Builds the "simple" forecast entries for up to 4 slots
    (attacker/defender and variants).
  - Argument:
    - R0 = pointer to `BattleInfo` root; the script models the per-slot entries
      as `Fates_BattleAttackEntry` and assumes a stride of 0x200 bytes.
  - Internals:
    - Entry stride: `slot = root + slotIndex * 0x200` (seen as `lsl #0x9`).
    - Uses `gBattleInfoGlobals @ 0x00347F7C` for shared bitfields and config.

  For each slot:

  - Computes a hit-based chance and stores it at:
    - `slot->vantageChance @ +0x34` (0..100).
    - The calculation uses:
      - `slot->hitStat @ +0x5E`
      - `slot->otherHitStat @ +0x62` (from the paired slot via an indirect lookup).
  - Computes a base damage band and stores it at:
    - `slot->baseDamage @ +0x38`
      - default: 0x12C
      - Upgraded to 0x190 if a bit test using:
        - tag at `slot->hitTableTag @ +0x10`
        - bitfield pair at `gBattleInfoGlobals + 0x1C8`
        - returns non-zero.
  - Calls `0x005259D4` with a detail pointer and some RNG-related state and
    stores the result at:
    - `slot->attackCountOrDamage @ +0x3C`
      - This seems to encode either the number of hits or some damage scaling
        depending on the context.
  - Sets a debug skill flag at:
    - `slot->debugHasSkill @ +0x40`
      - This comes from:
        - `Unit__HasSkillById @ 0x0052DD04`
        - Using `slot->unitPtr @ +0x4` and `gBattleInfoGlobals.detailShort_34`.

  - Several global flags at `gBattleInfoGlobals` gate optional behaviors,
    for example:
    - `flags_DC @ +0xDC`: bit 0 gates setup of the bitfield pair at +0x1E8.
    - `flags_D8 @ +0xD8`: bit 0 gates use of the data at 0x347F88.
    - `flags_F4 @ +0xF4` and `flags_100 @ +0x100` gate additional resource loads.

### Structs (script-backed)

Using the Ghidra script, the following structs are defined under
`Fates / Battle`:

#### Fates_BattleAttackEntry (size 0x200)

Key fields (relative to the slot base):

- `0x00: slotKind_orSide (u8)`
  - Simple slot identifier (attacker, defender, etc.).
- `0x04: unitPtr (Unit*)`
  - Pointer to the unit associated with this slot.
- `0x10: hitTableTag (s16)`
  - Tag value passed to `0x003D0EA4` to select a bitfield position.
- `0x28: flags (u32)`
  - Flag word tested with 0x40, 0x80, 0x4000 in CalculateSimple.
- `0x2C: detailPtrA (void*)`
  - Result of `0x005253A8`.
- `0x30: detailPtrB (void*)`
  - Result of `0x00525090`, used as the main detail pointer.
- `0x34: vantageChance (s32)`
  - 0..100 value: derived chance that can be zeroed by a bitfield test at
    `gBattleInfoGlobals + 0x1E8`.
- `0x38: baseDamage (s32)`
  - Base damage band. 0x12C by default, possibly replaced with 0x190 when a
    bitfield test at `gBattleInfoGlobals + 0x1C8` passes.
- `0x3C: attackCountOrDamage (s32)`
  - Value from `0x005259D4`; likely encodes either hit count or adjusted damage.
- `0x40: debugHasSkill (u8)`
  - 1 when `Unit__HasSkillById` reports the unit owns the relevant skill
    (used in our Jakob tests).
- `0x5E: hitStat (s16)`
  - Hit-like stat for this slot.
- `0x62: otherHitStat (s16)`
  - Hit-like stat from the "other" slot; the difference between the two feeds
    the chance computation.

Anything not listed is still unknown or padding, but the layout is fixed to a
total stride of 0x200 bytes.

#### Fates_BattleInfoGlobals (size 0x200)

Key fields (relative to the base pointed to by `gBattleInfoGlobals`):

- `0x2C: detailShort_2C (s16)`
  - Short loaded from the resource at 0x347F90. Used in conjunction with
    bit tables (index via `0x003D0EA4`) when deciding whether to halve
    damage or apply special behavior.

- `0x34: detailShort_34 (s16)`
  - Short loaded from 0x347F9C. Used with `Unit__HasSkillById` to check
    each slot's `unitPtr` for a particular skill.

- `0xD8: flags_D8 (u32)`
  - Flag word whose bit 0 gates a call pattern involving 0x347F88.

- `0xDC: flags_DC (u32)`
  - Flag word whose bit 0 gates:
    - resource use at 0x347F80 / 0x347F84
    - writing the bitfield pair at +0x1E8 via `stmia`.

- `0xF4: flags_F4 (u32)`
  - Flag word used earlier in CalculateSimple as an additional gate for
    global behavior related to 0x1E8.

- `0x100: flags_100 (u32)`
  - Flag word gating calls that populate `detailShort_34`.

- `0x1C8: bitfieldA_lo (u32)`
- `0x1CC: bitfieldA_hi (u32)`
  - Bitfield pair "A". Loaded via `ldmia [globals+0x1C8], {r0,r1}` and
    ANDed with a 64-bit mask obtained from `0x003D0EA4`. If the result is
    non-zero, baseDamage is switched from 0x12C to 0x190.

- `0x1E8: bitfieldB_lo (u32)`
- `0x1EC: bitfieldB_hi (u32)`
  - Bitfield pair "B". Also loaded via `ldmia`. Used with a different
    mask (again derived from the slot's tag at +0x10) to gate whether
    `vantageChance` is zeroed out.

These structs are intentionally conservative: anything we have not directly
seen in the disassembly is left as padding or marked "unknown flags" rather
than guessed.

--------

== Fates_BattleAttackEntry (Fates_BattleAttackEntry), size 0x200 ==
0x000  len=0x1  byte                  slotKind_orSide       Slot kind / side indicator. Loaded with ldrb [slot+0x0] in CalculateSimple.
0x001  len=0x3  byte[3]               pad_1_3               
0x004  len=0x4  pointer               unitPtr               Pointer to UNIT / battle unit for this slot. Used before Unit__HasSkillById at 0x00347F40.
0x008  len=0x8  byte[8]               pad_8_F               
0x010  len=0x2  word                  hitTableTag           Short tag used with 0x003D0EA4 and bitfield tables in gBattleInfoGlobals.
0x012  len=0x16  byte[22]              pad_12_27             
0x028  len=0x4  dword                 flags                 Bit flags tested in CalculateSimple (0x40, 0x80, 0x4000, etc).
0x02C  len=0x4  pointer               detailPtrA            Result of 0x005253A8. Stored before calling 0x00525090.
0x030  len=0x4  pointer               detailPtrB            Result of 0x00525090. Base detail record for this slot.
0x034  len=0x4  dword                 vantageChance         0..100 style chance value derived from hit stats; stored at [slot+0x34].
0x038  len=0x4  dword                 baseDamage            Base damage band (0x12C normal, 0x190 special cases) prior to halving when certain flags / tables match.
0x03C  len=0x4  dword                 attackCountOrDamage   Derived value returned by 0x005259D4 using detail + RNG. Interpreted as damage or attack count depending on context.
0x040  len=0x1  byte                  debugHasSkill         Debug flag set to 1 when Unit__HasSkillById returns non-zero in CalculateSimple (used for Jakob personal verification).
0x041  len=0x1D  byte[29]              pad_41_5D             
0x05E  len=0x2  word                  hitStat               Hit-like stat for this slot; loaded with ldrsh [slot+0x5E].
0x060  len=0x2  byte[2]               pad_60_61             
0x062  len=0x2  word                  otherHitStat          Hit-like stat from the paired slot; loaded via computed address and ldrsh [otherSlot+0x62]. Difference feeds chance calc.
0x064  len=0x19C  byte[412]             pad_64_1FF            

== Fates_BattleInfo (Fates_BattleInfo), size 0x808 ==
0x000  len=0x800  Fates_BattleAttackEntry[4]  entries               Four battle entries (index 0..3), stride 0x200 each. Base used in CalculateSimple via info + slotIndex*0x200.
0x800  len=0x4  byte[4]               pad_800_803           Padding / unknown word between entries[4] and flags_804.
0x804  len=0x4  int                   flags_804             Flags word read at [info+0x804], tested with tst r0,#0x10 in CalculateSimple.

