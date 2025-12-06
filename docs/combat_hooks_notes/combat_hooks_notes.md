BTL_FinalDamage_Pre
  Raw address: 0x003628BC (NA v1.1)
  Stage: FINAL_HP
  Type: MITM (decorate vanilla, never replace)
  Intent:
    - Observe per-entry final HP and total HP deltas for battle damage.
    - Correlate final HP outcomes with the forecast pipeline (for sanity checks, logs, and optional FINAL_HP-only “failsafe” rules).
  Guarantees:
    - Original function is always called.
    - We do not modify the forecast or base damage math here.
  Must NOT:
    - Change the forecast damage or hit count.
    - Be the only place a rule is applied (every proper rule must have a forecast-phase partner).
  Notes:
    - Called multiple times per combat.
    - The root’s HP fields are not a simple “per-hit” before/after log (see hpBefore=25, hpAfter=22 vs hpAfter=19 in logs).
    - Treat this as a late observation and, at most, a last-ditch clamping/sanity surface.

---------------

MAP_BattleInfo_CalculateSimple
  Stage: PREVIEW_SIMPLE (Forecast)
  Type: MITM
  Intent:
    - Build a DamageContext from BattleInfoSimple for the primary forecast.
    - Run DamageEngineModifier with origin=Forecast.
  Guarantees:
    - DamageContext.currentDamage is TOTAL across all hits, not per-hit.
    - We preserve vanilla pow/times/hit logic unless rules explicitly adjust them.
  Notes:
    - Jakob 3×2 test: pow=3, times=2, base=6, modTotal=6 (vanilla).
	
-------------

Unit_HasSkillById
  Stage: GENERAL SKILL QUERY
  Type: MITM (observe)
  Intent:
    - Observe when game logic queries “does this unit have skill X?”
    - Feed real map-units into Skills::OnUnitHasSkillObserved for state tracking.
  Filtering:
    - “Heap” units (0x32xxxxxx range) are treated as real units.
    - Non-heap callers (e.g. unit=0x0ffff9f0 doing sequential probes) are counted as “weird” and ignored by the skill engine.
  Notes:
    - This protects us from accidental spam from internal helper contexts while still giving us a strong view of real unit skill usage.

-----------

MAP_EquipSkillCalculator_Calculate
  Stage: PREVIEW_DETAIL / EQUIP SKILL PASS
  Type: MITM
  Intent:
    - Observe which skills are being seen as “equipped/active” in the detailed calculator.
    - Dispatch SkillDriver[MAP_EquipSkillCalc] for these skills.
  Notes:
    - Called a lot; keep logging minimal or guarded.
    - Safe to assume `unit` is a real heap unit in the contexts we forward to the skill engine.
