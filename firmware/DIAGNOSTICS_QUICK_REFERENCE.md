# Multi-Injection Diagnostics: Quick Reference

## All Diagnostic Checks at a Glance

| # | Check | Location | Error Code | Condition | Fallback | Example Output |
|---|-------|----------|-----------|-----------|----------|-----------------|
| **1** | Multi-injection enabled | `updateMultiInjectionAngles()` | 9365 | `enableMultiInjection == false` or `numberOfPulses < 2` | Single | — (skipped) |
| **2** | Valid fuel mass | `updateMultiInjectionAngles()` | 9365 | `baseFuelMass <= 0` or `NaN` | Single | `mi_invalid_mass: baseMass=-0.001 g` |
| **3** | RPM > 0 | `updateMultiInjectionAngles()` | 9365 | `rpm < 1` (not running) | Single | `mi_rpm_invalid: rpm=0.0` |
| **4** | Timing valid | `updateMultiInjectionAngles()` | 9365 | `oneDegreeUs` invalid/NaN | Single | `mi_timing_invalid: oneDegreeUs=0.000` |
| **5** | Pulse duration ≥ dead time | `updateMultiInjectionAngles()` | 9367 | Any pulse: `fuelMs < deadtime` | Warning only | `mi_p0_below_deadtime: fuelMs=0.35 ms < deadtime=0.80 ms` |
| **6** | Pulse 1 ordering | `validateInjectionWindows()` | 9368 | Pulse 1 starts before Pulse 0 in cycle | Single | `mi_p1_before_p0: p0=100.0° p1=50.0°` |
| **7** | Minimum dwell | `validateInjectionWindows()` | 9369 | `dwell < minDwell` (table) | Single | `mi_dwell_low: dwell=2.5° min=5.0° (p0_end=315.0° p1_start=317.5°)` |
| **8** | Duration angle ≤ max | `validateInjectionWindows()` | 9366 | Any pulse: `durationAngle > 120°` | Single | `mi_p0_long: durationAngle=125.0° max=120.0°` |
| **9** | Pulse starts safely | `validateInjectionWindows()` | 9370 | Pulse starts within 15° of ignition | Single | `mi_p0_late_start: starts=20.0° ign=0.0° margin=14.5°` |
| **10** | Pulse ends safely | `validateInjectionWindows()` | 9371 | Pulse ends within 15° of ignition | Single | `mi_p1_late_end: ends=35.0° ign=0.0° margin=10.0°` |
| **11** | No pulse overlap | `validateInjectionWindows()` | 9363 | `pulse0_end > pulse1_start` | Single | `mi_overlap: p0_end=315.5° p1_start=310.0°` |

---

## Error Code Summary

### 9363: CUSTOM_MULTI_INJECTION_OVERLAP
- **Trigger**: Pulse 0 end angle > Pulse 1 start angle
- **Check #**: 11 (last)
- **Cause**: Pulse 1 starts before Pulse 0 finishes (timing table error)
- **Fix**: Adjust `secondInjectionAngleTable` to move Pulse 1 later

### 9364: CUSTOM_MULTI_INJECTION_TOO_LATE
- **Status**: Legacy (not actively used; use 9370/9371 instead)
- **Reserved for**: Future use or future timing violations

### 9365: CUSTOM_MULTI_INJECTION_INVALID_CONFIG
- **Trigger**: Checks 1–4 (input validation)
- **Causes**: 
  - Multi-injection disabled
  - Invalid fuel mass (≤0, NaN)
  - RPM too low (< 1)
  - Timing data unavailable
- **Fix**: Verify tuning parameters; check RPM sensor; enable multi-injection if desired

### 9366: CUSTOM_MULTI_INJECTION_PULSE_TOO_LONG
- **Trigger**: Check 8 (duration angle)
- **Trigger Value**: `durationAngle > 120°`
- **Cause**: Fuel mass too large for one pulse; pulse duration exceeds hardware scheduling limit
- **Fix**: Reduce target fuel mass or increase split ratio (i.e., smaller pulses)

### 9367: CUSTOM_MULTI_INJECTION_PULSE_TOO_SHORT
- **Trigger**: Check 5 (dead time)
- **Trigger Value**: `fuelMs < deadtime` (and `fuelMs > 0.001 ms`)
- **Cause**: Fuel mass very small; pulse shorter than injector dead time = no effective delivery
- **Fix**: Increase target fuel mass; disable multi-injection at low load
- **Note**: Warning issued (not fallback) – multi-injection may still work

### 9368: CUSTOM_MULTI_INJECTION_WRONG_ORDER
- **Trigger**: Check 6 (pulse ordering)
- **Trigger Logic**: `pulse1.start < pulse0.start AND (pulse1.start - pulse0.start + 720) > 360°`
- **Cause**: `secondInjectionAngleTable` maps to earlier angle than `injectionOffset`
- **Fix**: Verify BTDC conversion: `baseAngle = 720 - btdcAngle`; check table semantics

### 9369: CUSTOM_MULTI_INJECTION_INSUFFICIENT_DWELL
- **Trigger**: Check 7 (minimum dwell)
- **Trigger Value**: `dwell < minDwell` (from `minDwellAngleTable`)
- **Cause**: Pulses too close together; not enough gap for driver reset or engine requirements
- **Fix**: Adjust `secondInjectionAngleTable` to move Pulse 1 later OR reduce `minDwellAngleTable`

### 9370: CUSTOM_MULTI_INJECTION_STARTS_TOO_LATE
- **Trigger**: Check 9 (start safety)
- **Trigger Value**: Pulse starts < 15° before ignition (TDC)
- **Cause**: Pulse 1 (or Pulse 0) scheduled too late; injection may overlap ignition
- **Fix**: Move pulses earlier via `injectionOffset` or `secondInjectionAngleTable`

### 9371: CUSTOM_MULTI_INJECTION_ENDS_TOO_LATE
- **Trigger**: Check 10 (end safety)
- **Trigger Value**: Pulse ends < 15° before ignition (TDC)
- **Cause**: Pulse extends into ignition window; fuel may be burning or ignited during injection
- **Fix**: Move pulses earlier or reduce pulse durations (split ratio / fuel mass)

---

## Decision Tree: Which Error Will Trigger?

```
START
│
├─ Multi-injection disabled?
│  └─ 9365: INVALID_CONFIG → SKIP
│
├─ baseFuelMass invalid or ≤ 0?
│  └─ 9365: INVALID_CONFIG → SINGLE
│
├─ RPM < 1?
│  └─ 9365: INVALID_CONFIG → SINGLE
│
├─ oneDegreeUs invalid?
│  └─ 9365: INVALID_CONFIG → SINGLE
│
├─ Any pulse duration < deadtime (and > 0.001 ms)?
│  └─ 9367: PULSE_TOO_SHORT → WARNING (continue)
│
├─ Pulse 1 before Pulse 0 in cycle?
│  └─ 9368: WRONG_ORDER → SINGLE
│
├─ Dwell < minDwell?
│  └─ 9369: INSUFFICIENT_DWELL → SINGLE
│
├─ Any pulse duration angle > 120°?
│  └─ 9366: PULSE_TOO_LONG → SINGLE
│
├─ Any pulse starts < 15° before ignition?
│  └─ 9370: STARTS_TOO_LATE → SINGLE
│
├─ Any pulse ends < 15° before ignition?
│  └─ 9371: ENDS_TOO_LATE → SINGLE
│
├─ Pulse 0 end > Pulse 1 start?
│  └─ 9363: OVERLAP → SINGLE
│
└─ All checks pass
   └─ MULTI-INJECTION OK (verbose log)
```

---

## Console Output Examples

### Healthy Multi-Injection
```
=== MULTI-INJECTION DIAGNOSTICS ===
RPM: 2500, Load: 45.2%, Deadtime: 0.65 ms, Ignition: 15.0°
Min Dwell: 10.0°, Safety margin before ignition: 15.0°

--- Cylinder 0 (mass: 0.450 g) ---
  Pulse 0: start=100.0° dur=3.35 ms (40.2°) ratio=70.0% mass=0.315 g [ACTIVE]
  Pulse 1: start=615.0° dur=1.45 ms (17.4°) ratio=30.0% mass=0.135 g [ACTIVE]
  Dwell: 12.5° (min required: 10.0°) [OK]

--- Cylinder 1 (mass: 0.450 g) ---
  Pulse 0: start=280.0° dur=3.35 ms (40.2°) ratio=70.0% mass=0.315 g [ACTIVE]
  Pulse 1: start=895.0° dur=1.45 ms (17.4°) ratio=30.0% mass=0.135 g [ACTIVE]
  Dwell: 12.5° (min required: 10.0°) [OK]

====================================
```

### Fallback to Single (Insufficient Dwell)
```
=== MULTI-INJECTION DIAGNOSTICS ===
RPM: 1800, Load: 25.0%, Deadtime: 0.72 ms, Ignition: 10.0°
Min Dwell: 12.0°, Safety margin before ignition: 15.0°

--- Cylinder 0 (mass: 0.280 g) ---
  Single injection mode (fallback or configuration)
  Pulse 0: start=105.0° dur=2.65 ms (31.8°) ratio=100.0%

--- Cylinder 1 (mass: 0.280 g) ---
  Single injection mode (fallback or configuration)
  Pulse 0: start=285.0° dur=2.65 ms (31.8°) ratio=100.0%

====================================
```

### Multiple Warnings (Dead Time Low)
```
--- Cylinder 0 (mass: 0.100 g) ---
  Pulse 0: start=100.0° dur=0.50 ms (6.0°) ratio=60.0% mass=0.060 g [ACTIVE]
  ⚠ WARNING: Duration (0.50 ms) < deadtime (0.72 ms)
  Pulse 1: start=612.0° dur=0.33 ms (4.0°) ratio=40.0% mass=0.040 g [ACTIVE]
  ⚠ WARNING: Duration (0.33 ms) < deadtime (0.72 ms)
  Dwell: 11.5° (min required: 10.0°) [OK]
```

### Fallback to Single (Wrong Order)
```
--- Cylinder 0 (mass: 0.450 g) ---
  Single injection mode (fallback or configuration)
  Pulse 0: start=100.0° dur=4.25 ms (51.0°) ratio=100.0%
  ⚠ WARNING: Pulse 1 before Pulse 0 in cycle!
```

---

## Typical Tuning Workflow

1. **Enable Multi-Injection**: Set `enableMultiInjection = ON` in config
2. **Verify Not Disabled**: Run `mi_angles` → should see 2 pulses (not single)
3. **If Warnings Appear**:
   - Check error code in warning message
   - Adjust table (minDwell, secondInjectionAngle, splitRatio)
   - Re-run `mi_angles`
4. **When Error Triggers Fallback**:
   - Console shows `mi_fallback_to_single: ...`
   - Run `mi_angles` → single pulse shown
   - Fix underlying cause (table values, RPM, load, mass)
5. **Bench Validation**:
   - Oscilloscope on injector pin
   - Verify timing and duration match console output
   - Confirm two distinct pulses (if multi) or one (if fallback)
6. **Production**: Once stable, can disable verbose logging

---

## Configuration Checklist

- [ ] `multiInjection.enableMultiInjection = ON`
- [ ] `multiInjectionSplitRatioTable` filled (not all zeros)
- [ ] `secondInjectionAngleTable` filled (not all zeros)
- [ ] `minDwellAngleTable` filled with safe values (5–20°)
- [ ] `multiInjectionLoadBins` and `multiInjectionRpmBins` defined
- [ ] `isVerboseMultiInjection = ON` (during tuning, OFF for production)
- [ ] Injector dead time calibration correct (check voltage table)
- [ ] Ignition timing table `timingAdvanceTable` defined

---

## See Also
- `DIAGNOSTICS_MULTIINJECTION.md` — Full technical documentation
- `obd_error_codes.h` — OBD code definitions
- `multi_injection.cpp` — Implementation source
- `settings.cpp::printMultiInjectionAngles()` — Console command source
