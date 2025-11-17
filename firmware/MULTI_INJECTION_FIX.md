# Multi-Injection Duration Calculation Fix

## Problem Statement

User reported that multi-injection duration was incorrect:
- **Expected:** ~5 ms total for both pulses at 1200 RPM, 10.1% load
- **Actual:** 9.72 ms total for both pulses

Logs showed:
- Pulse 0: 6.81 ms (70%)
- Pulse 1: 2.91 ms (30%)
- **Total: 9.72 ms** ✗ (should be ~5 ms)

## Root Cause

The calculation was using pre-calculated `injectionDuration` from engine state:

```cpp
// OLD (WRONG)
floatms_t baseFuelMs = getEngineState()->injectionDuration;  // Calculated once, fixed value
floatms_t pulseFuelMs = baseFuelMs * (ratio / 100.0f);        // Just scale it down
```

**Problem:** `getEngineState()->injectionDuration` is already the result of:
```cpp
engine->engineState.injectionDuration = engine->module<InjectorModelPrimary>()->getInjectionDuration(stage1InjectionMass);
```

This means `injectionDuration` is already converted from fuel mass to time, **including non-linear effects** like injector dead time.

When you scale this down linearly by ratio, you get incorrect results because:
1. Dead time is added to EACH pulse independently
2. The injector model is **non-linear** - not a simple mass→time linear relationship

## Solution

Use the actual fuel **mass** and calculate duration for each pulse independently:

```cpp
// NEW (CORRECT)
float baseFuelMass = getEngineState()->injectionMass[cylinderNumber];  // Get actual mass
float pulseMass = baseFuelMass * (ratio / 100.0f);                      // Scale mass
floatms_t pulseFuelMs = engine->module<InjectorModelPrimary>()->getInjectionDuration(pulseMass);  // Calculate duration correctly
```

**Why this is correct:**
- `injectionMass[cyl]` is the fuel mass for that cylinder (independent, per-cylinder)
- Split ratio is applied to the **mass**, not the duration
- Each pulse duration is calculated independently through injectorModel
- Dead time is applied correctly to each pulse's actual mass

## Example Calculation

Assume:
- Total fuel mass: 0.4 g
- Split ratio: 70% / 30%
- Injector model: duration = 10 * mass + 0.5 (dead time)

**OLD (WRONG):**
```
baseFuelMs = 10*0.4 + 0.5 = 4.5 ms (calculated once)
Pulse 0: 4.5 * 0.70 = 3.15 ms    ← Missing dead time on pulse 1!
Pulse 1: 4.5 * 0.30 = 1.35 ms    ← Total = 4.5 ms (correct only by accident)
```

**NEW (CORRECT):**
```
pulseMass0 = 0.4 * 0.70 = 0.28 g
Pulse 0: 10*0.28 + 0.5 = 3.3 ms  ← Correct dead time

pulseMass1 = 0.4 * 0.30 = 0.12 g
Pulse 1: 10*0.12 + 0.5 = 1.7 ms  ← Correct dead time

Total: 3.3 + 1.7 = 5.0 ms ✓
```

## Files Modified

1. **multi_injection.cpp**
   - Added `#include "injector_model.h"`
   - Changed `updateMultiInjectionAngles()` to use `getEngineState()->injectionMass[cylinderNumber]`
   - Calculate each pulse duration via `engine->module<InjectorModelPrimary>()->getInjectionDuration(pulseMass)`

2. **settings.cpp**
   - Enhanced `printMultiInjectionAngles()` to show fuel mass for debugging

## Expected Result

When this fix is deployed:
- Multi-injection duration should match or be slightly less than standard single injection
- Dead time should be applied correctly to each pulse
- Total fuel amount should match the fuel calculator's requirement
- Oscilloscope measurements should confirm proper pulse timing
