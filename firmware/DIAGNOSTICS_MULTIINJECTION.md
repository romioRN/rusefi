# Multi-Injection Diagnostics System

## Overview
Comprehensive validation and diagnostics for split (multi-pulse) fuel injection. The system detects configuration errors, timing violations, and physical constraints, providing detailed warnings and error codes.

---

## OBD Error Codes

### Existing Codes (Enhanced)
- **CUSTOM_MULTI_INJECTION_OVERLAP** (9363): Pulses overlap in time
- **CUSTOM_MULTI_INJECTION_TOO_LATE** (9364): Injection starts too late (legacy, may be retired)
- **CUSTOM_MULTI_INJECTION_INVALID_CONFIG** (9365): Configuration error (invalid mass, RPM, timing)
- **CUSTOM_MULTI_INJECTION_PULSE_TOO_LONG** (9366): Pulse duration exceeds hardware limit (>120°)

### New Codes
- **CUSTOM_MULTI_INJECTION_PULSE_TOO_SHORT** (9367): Pulse duration below injector dead time
- **CUSTOM_MULTI_INJECTION_WRONG_ORDER** (9368): Pulse 1 starts before Pulse 0 in cycle
- **CUSTOM_MULTI_INJECTION_INSUFFICIENT_DWELL** (9369): Dwell between pulses < minDwell table value
- **CUSTOM_MULTI_INJECTION_STARTS_TOO_LATE** (9370): Pulse starts within 15° of ignition event
- **CUSTOM_MULTI_INJECTION_ENDS_TOO_LATE** (9371): Pulse ends within 15° of ignition event

---

## Validation Checks (Priority Order)

### 1. **Input Validation**
**Location**: `updateMultiInjectionAngles()` - early checks

**Checks**:
- Multi-injection enabled in configuration
- Valid fuel mass available (> 0, not NaN)
- RPM > 1 (engine running or cranking)
- Rotation state valid (oneDegreeUs defined)

**Error Code**: `CUSTOM_MULTI_INJECTION_INVALID_CONFIG` (9365)

**Example Output**:
```
mi_invalid_mass: baseMass=-0.001 g
mi_rpm_invalid: rpm=0.0
mi_timing_invalid: oneDegreeUs=0.000
```

---

### 2. **Pulse Duration vs Dead Time**
**Location**: `updateMultiInjectionAngles()` - per-pulse checks

**Checks**:
- Each pulse duration ≥ injector dead time (if > 0.001 ms)
- Ensures effective fuel delivery

**Dead Time Source**: `injectorModel->getDeadtime()`
- Interpolated from `battLagCorrTable` (voltage, pressure dependent)
- Typical range: 0.2–1.5 ms depending on battery voltage

**Error Code**: `CUSTOM_MULTI_INJECTION_PULSE_TOO_SHORT` (9367)

**Example Output**:
```
mi_p0_below_deadtime: fuelMs=0.35 ms < deadtime=0.80 ms cyl=0
```

**Action**: Warning issued; multi-injection can still proceed if other checks pass.

---

### 3. **Pulse Ordering**
**Location**: `validateInjectionWindows()`

**Checks**:
- Pulse 1 starts after Pulse 0 starts in the engine cycle
- Ensures logical sequence (main before secondary)

**Logic**:
```
angleDelta = pulse1.startAngle - pulse0.startAngle
if (angleDelta < 0) angleDelta += 720°
if (angleDelta > 360°) → ERROR (Pulse 1 before Pulse 0)
```

**Error Code**: `CUSTOM_MULTI_INJECTION_WRONG_ORDER` (9368)

**Example Output**:
```
mi_p1_before_p0: p0=100.0° p1=50.0°
```

**Action**: Fallback to single injection.

---

### 4. **Minimum Dwell Between Pulses**
**Location**: `validateInjectionWindows()`

**Checks**:
- Angle between pulse 0 end and pulse 1 start ≥ minDwell from table

**Dwell Calculation**:
```
pulse0_end = pulse0.startAngle + pulse0.durationAngle
dwell = pulse1.startAngle - pulse0_end
if (dwell < 0) dwell += 720°
```

**minDwell Source**: `minDwellAngleTable` (3D interpolated RPM × Load)
- Typical range: 5–50°
- Clamped to [5°, 50°] for safety

**Error Code**: `CUSTOM_MULTI_INJECTION_INSUFFICIENT_DWELL` (9369)

**Example Output**:
```
mi_dwell_low: dwell=2.5° min=5.0° (p0_end=315.0° p1_start=317.5°)
```

**Action**: Fallback to single injection.

---

### 5. **Pulse Duration Range**
**Location**: `validateInjectionWindows()`

**Checks**:
- Duration angle ≤ 120° (hardware/scheduling limit)
- Prevents excessively long pulses

**Error Code**: `CUSTOM_MULTI_INJECTION_PULSE_TOO_LONG` (9366)

**Example Output**:
```
mi_p0_long: durationAngle=125.0° max=120.0°
```

**Action**: Fallback to single injection.

---

### 6. **Injection Timing Safety (15° Margin)**
**Location**: `validateInjectionWindows()`

**Checks**:
- Pulse starts ≥ 15° before ignition (TDC)
- Pulse ends ≥ 15° before ignition

**Safety Margin**: `SAFE_MARGIN_BEFORE_IGNITION = 15.0°`
- Ensures injection completes before spark fires
- Prevents fuel ignition during injection

**Error Codes**:
- `CUSTOM_MULTI_INJECTION_STARTS_TOO_LATE` (9370)
- `CUSTOM_MULTI_INJECTION_ENDS_TOO_LATE` (9371)

**Example Output**:
```
mi_p0_late_start: starts=20.0° ign=0.0° margin=14.5° (need >=15.0°)
mi_p1_late_end: ends=35.0° ign=0.0° margin=10.0° (need >=15.0°)
```

**Action**: Fallback to single injection.

---

### 7. **Pulse Overlap Detection**
**Location**: `validateInjectionWindows()` - strict check

**Checks**:
- Pulse 0 end ≤ Pulse 1 start (no overlap)

**Error Code**: `CUSTOM_MULTI_INJECTION_OVERLAP` (9363)

**Example Output**:
```
mi_overlap: p0_end=315.5° p1_start=310.0°
```

**Action**: Fallback to single injection.

---

## Fallback Behavior

When any validation check fails:
1. `numberOfPulses` set to 1
2. Single pulse duration recalculated: `singlePulseFuelMs = injectorModel->getInjectionDuration(baseFuelMass)`
3. Uses per-cylinder fuel mass (not pre-computed `injectionDuration` from engine2)
4. Calls `updateInjectionAngle()` to compute single-pulse timing
5. Warning issued with reason

**Verbose Output** (when `isVerboseMultiInjection = true`):
```
mi_fallback_to_single: baseMass=0.450 g singlePulseMs=4.25 ms cyl=0
```

---

## Console Commands

### `mi_angles` Command
Detailed diagnostics output for all cylinders.

**Output Format**:
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

...

====================================
```

**Key Columns**:
- `start`: Injection start angle (degrees)
- `dur`: Injection duration (milliseconds)
- `(X°)`: Duration in crankshaft degrees
- `ratio`: % of total fuel in pulse
- `mass`: Calculated fuel mass (grams)
- `[ACTIVE]` / `[INACTIVE]`: Pulse status

**Warnings in Output**:
```
  ⚠ WARNING: Duration (0.35 ms) < deadtime (0.65 ms)
  ⚠ WARNING: Duration angle (125.0°) > max limit (120°)
  ⚠ WARNING: Pulse 1 before Pulse 0 in cycle!
```

---

## Verbose Logging

**Enable**: Set `isVerboseMultiInjection = true` in tuner configuration

**Output** (per fuel calculation cycle):
- Success: `mi_update_ok: baseMass=0.450 g p0[...] p1[...] cyl=N`
- Validation pass: `mi_valid: p0[100.0°,3.35ms,40.2°] p1[615.0°,1.45ms,17.4°] dwell=12.5° rpm=2500`
- Fallback: `mi_fallback_to_single: baseMass=0.450 g singlePulseMs=4.25 ms cyl=0`

---

## Implementation Details

### Files Modified
1. **obd_error_codes.h**: Added new error codes (9367–9371)
2. **multi_injection.cpp**: Enhanced `validateInjectionWindows()` with 7-point validation
3. **multi_injection.cpp**: Enhanced `updateMultiInjectionAngles()` with diagnostics
4. **settings.cpp**: Enhanced `printMultiInjectionAngles()` with comprehensive output

### Key Functions

**`validateInjectionWindows()`**
- Performs all 7 validation checks
- Generates specific warning for each failure
- Returns `false` → triggers fallback to single injection

**`updateMultiInjectionAngles()`**
- Validates inputs (mass, RPM, timing)
- Computes per-pulse durations via injectorModel
- Calls `validateInjectionWindows()`
- Handles fallback and verbose logging

**`printMultiInjectionAngles()`**
- Console command (`mi_angles`)
- Shows all cylinder data
- Includes diagnostic warnings
- Shows dwell calculations

---

## Bench Testing Procedure

### 1. **Static Configuration Check**
```
1. Set RPM to 2500 (or desired test point)
2. Set target fuel mass to known value (e.g., 0.5 g)
3. Set multiInjection.enableMultiInjection = ON
4. Set splitRatio table to 70/30 (or desired)
5. Run console: mi_angles
6. Verify output shows 2 active pulses
7. Check: Pulse 0 + Pulse 1 duration ≈ single pulse duration (within ±5%)
```

### 2. **Dwell Validation**
```
1. In TunerStudio, set secondInjectionAngleTable to create pulse overlap (e.g., Pulse 1 too early)
2. Run mi_angles
3. Expected: Warning "mi_dwell_low" or "mi_overlap"
4. Verify: System falls back to single injection
5. Check: Pulse count shows 1 (from console or TunerStudio indicator)
```

### 3. **Dead Time Check**
```
1. Set fuel mass very small (e.g., 0.05 g) such that pulse duration < deadtime
2. Run mi_angles
3. Expected: Warning "mi_p0_below_deadtime" (one or both pulses)
4. Note: System still allows multi-injection if other checks pass
5. On oscilloscope: Verify actual pulse width matches calculated duration
```

### 4. **Oscilloscope Verification**
```
Scope setup:
- Trigger: Fuel injector pin (main/primary)
- Secondary: CAN or RPM signal for reference

Expected for 2500 RPM, 0.450 g, 70/30 split, 40° advance:
  ├─ Pulse 0 at ~100° BTDC
  │  ├─ Rising edge (open): 100° position
  │  ├─ Width: ~3.3 ms
  │  └─ Falling edge (close): ~40° BTDC
  │
  ├─ Dwell: ~10° (no injector current)
  │
  └─ Pulse 1 at ~615° (next cycle)
     ├─ Rising edge (open): 615° / 360° = next cycle 255° BTDC
     ├─ Width: ~1.5 ms
     └─ Falling edge (close): ~240° BTDC (next cycle)

Notes:
- Total width ≈ 3.3 + dwell + 1.5 = 5.8 ms (one complete cycle)
- If running continuousl, pulses repeat every 360° (single cam cycle = 2 crank cycles)
- Measure with fuel pressure, check dead time vs prediction
```

### 5. **Fallback Trigger Test**
```
1. Set minDwellAngleTable to a value > current dwell
   (e.g., min dwell = 20° but current dwell = 10°)
2. Run mi_angles
3. Expected: Warning "mi_dwell_low", fallback to single pulse
4. Oscilloscope: Verify single pulse appears, width ≈ sum of split pulses
```

### 6. **Late Injection Test** (Safety Margin)
```
1. Set secondInjectionAngleTable to inject very late (e.g., Pulse 1 at 5° before TDC)
2. Run mi_angles
3. Expected: Warning "mi_p1_late_start" or "mi_p1_late_end"
4. Fallback to single injection
5. Verify: No fuel is injected after ignition event (oscilloscope)
```

---

## Acceptance Criteria

✅ All validation checks execute without errors
✅ Warnings generated correctly for each failure mode
✅ Fallback to single injection on validation failure
✅ Console command `mi_angles` shows accurate diagnostics
✅ Dwell calculation matches minDwell table
✅ Oscilloscope traces match predicted timing/duration
✅ Total pulse duration ≈ single-pulse equivalent
✅ No overlapping pulses (if dwell OK)
✅ Injection ends ≥ 15° before TDC (ignition safety)

---

## Troubleshooting

| Symptom | Cause | Check |
|---------|-------|-------|
| Constant fallback to single | minDwell too high | `mi_angles` output, table values |
| Pulses in wrong order | Table angle error | secondInjectionAngleTable BTDC conversion |
| Warning "pulse_too_short" | Fuel mass too low | Increase target load / verify mass calculation |
| Warning "late_injection" | Pulse 1 angle too late | Adjust secondInjectionAngleTable earlier |
| Oscilloscope shows no Pulse 1 | isActive flag false | Check fuel mass > 0.05 ms; angle valid |
| AFR error after multi-injection | Mass calculation error | Compare Pulse 0 + Pulse 1 to single-pulse mass |

---

## Related Documentation
- `MULTI_INJECTION_FIX.md` — Technical fix for mass-based duration calculation
- `ИСПРАВЛЕНИЕ_BTDC_ТРАНСФОРМАЦИЯ.md` — BTDC angle conversion details
- `CHECKLIST_MULTIINJECTION.md` — Full test checklist
