# Combat Hook Inventory (v2 Baseline)

Game: Fire Emblem Fates (NA, v1.1)
CODE_BASE: 0x00100000

## Stages

- PREVIEW_SIMPLE: map__BattleInfo__CalculateSimple
- PREVIEW_DETAIL: map__BattleCalculator__CalculateAttack, BattleInfoSide helper funcs
- HIT_CALC: BTL_HitCalc_Main
- FINAL_HP: BTL_FinalDamage_Pre
- GATE: Attack Stance, efficacy, dual-hit checks
- HUD: HpWindow draw / battle HP gauges
- LIFECYCLE: SEQ_* turn/map/action hooks
- SKILL_EVIDENCE: Unit_HasSkillById, EquipSkillCalc, skill-effect hooks

## Core Combat & HP Hooks (NA v1.1)

| Hook ID                         | VA        | Function name (Ghidra)                                      | Stage              | Intent / Notes                                                                                               |
|---------------------------------|-----------|-------------------------------------------------------------|--------------------|-------------------------------------------------------------------------------------------------------------|
| BTL_HitCalc_Main                | 0x003A3588| BTL_HitCalc_Main                                            | HIT_CALC           | Core “roll hit” helper. r0 = hit threshold (% or scaled). Calls CAND_MapBattleCalc_ValueTransform for RNG. |
| BTL_FinalDamage_Pre             | 0x003628BC| BTL_FinalDamage_Pre                                         | FINAL_HP           | Late sequence driver using global battle struct. **Observation/sanity only**, not primary damage math.     |
| MAP_BattleInfo_CalculateSimple  | 0x00347C3C| map__BattleInfo__CalculateSimple                            | PREVIEW_SIMPLE     | Cheap forecast builder. r0 = BattleInfo*. Updates simple power/hit/times on both sides.                    |
| MAP_BattleCalculator_CalculateAttack |0x00361F70| map__BattleCalculator__CalculateAttack                     | PREVIEW_DETAIL     | Full battle calculator. r0 = BattleCalculator*, r1 = context/entry. Calls into various preview helpers.    |
| MAP_BattleInfoSide_CalcEfficacy | 0x0034899C| map__BattleInfo__Side__CalculateEfficacy                    | PREVIEW_EFFICACY   | Per-side efficacy/WT/etc. r0 = BattleInfoSide*, r1 = context (weapon/unit/etc.).                           |
| SEQ_HpDamage                    | 0x0035C7B8| map__SequenceBattle__ProcSequence__UpdateHp                 | HP_SEQUENCE_UPDATE | Sequence-level HP update driver. r0 = SequenceBattle*, r1 = side/index. Ends by calling UpdateCloneHP.    |
| UNIT_HpDamage                   | 0x003A844C| anonymous_namespace__UnitHpDamage                           | HP_INTENT          | High-level “apply damage to unit index” helper. Validates, then calls SequenceHelper_HpDamage.            |
| SEQ_HpDamage_Helper             | 0x00360F94| map__SequenceHelper__HpDamage                               | HP_APPLY           | Computes new HP in unit->+0xF3/+0x8C, then tail-calls Unit__UpdateCloneHP(unit*).                         |
| UNIT_UpdateCloneHP              | 0x003D575C| Unit__UpdateCloneHP                                         | FINAL_HP_COMMIT    | Copies HP/HP-int fields from unit to its clone. Canonical “HP is final, sync clones” event.               |
| HP_KillCheck                    | 0x0035CADC| map__SequenceBattle__anonymous_namespace__ProcSequence__DeadEvent | KILL_EVENT    | Decides if a unit is dead and triggers appropriate death/retreat handlers.                                |
| SEQ_MapStart                    | 0x003A4898| map__Sequence__anonymous_namespace__ProcSequence__Persistent| MAP_LIFECYCLE      | Map startup/persistent-sequence driver. Good MapBegin hook anchor.                                         |
| SEQ_TurnBegin                   | 0x003A54D8| map__Sequence__anonymous_namespace__ProcSequence__TurnBegin | MAP_LIFECYCLE      | Turn-begin sequence driver. Good TurnBegin hook anchor.                                                    |
| SEQ_TurnEnd                     | 0x003A4F0C| map__Sequence__anonymous_namespace__ProcSequence__TurnEnd   | MAP_LIFECYCLE      | Turn-end sequence driver. Good TurnEnd hook anchor.                                                        |
| SEQ_MapEnd                      | 0x003A4FFC| map__Sequence__anonymous_namespace__ProcSequence__Complete  | MAP_LIFECYCLE      | Map completion sequence driver. Good MapEnd hook anchor.                                                   |
| SYS_Rng32                       | 0x0044ADF8| CAND_MapBattleCalc_ValueTransform                           | RNG_COMBAT         | RNG-based value transform used by BTL_HitCalc_Main. Not a generic RNG; map-battle-focused helper.         |
| MAP_EquipSkillCalculator_Calculate |0x0036D088| map__EquipSkillCalculator__Calculate                      | SKILL_EQUIP        | Rebuilds equip-skill bitfields. Uses global config / bitmask writes.                                       |
| Unit_HasSkillById               | 0x0052DD04| Unit__HasSkillById                                          | SKILL_QUERY        | Per-unit skill query. r0 = Unit*, r1 = int16 skill ID.                                                     |
| Hook ID / Name                                        | Stage            | Intent / Notes                                                                                                         | Allowed to change                            | Must NOT do                                                                                      |
|-------------------------------------------------------|------------------|------------------------------------------------------------------------------------------------------------------------|----------------------------------------------|--------------------------------------------------------------------------------------------------|
| SEQ_HpDamage_Helper / `map__SequenceHelper__HpDamage` | FINAL_HP_DRIVER  | Map-side damage commit helper. Applies damage to a single unit, spawns/uses procs, and calls `Unit__UpdateCloneHP()`. | Final damage amount for map-scope HP changes | Recompute core damage math; touch forecast structures.                                           |
| SEQ_HpHeal / `map__SequenceHelper__HpHeal`            | FINAL_HP_DRIVER  | Map-side heal commit helper, mirrors HpDamage but for healing. Uses `Unit__GetMHPImpl()` and `Unit__UpdateCloneHP()`. | Final heal amount for map-scope HP changes   | Recompute core damage math; touch forecast structures.                                           |
| SEQ_HpDamage / `map__SequenceBattle__ProcSequence__UpdateHp` | FINAL_HP_DRIVER | Battle-sequence HP application step. Reads per-strike results, updates HP, drives HP windows, and calls `UpdateCloneHP()`. | Final damage results from battle context  | Redo preview math; desync HP vs `BattleInfo` entries; use as primary forecast modifier.          