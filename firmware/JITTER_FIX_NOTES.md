# Multi-Injection Jitter Fix (1ms Pulse Duration)

## Problem
Pulse duration was oscillating/skipping around 1ms despite EMA smoothing at α=0.4, especially on small pulses.

## Root Causes Identified
1. **DEADTIME_MULTIPLIER = 2.1** — Too strict for 1ms pulses. With deadtime ~0.1ms, minimum required = 0.21ms, leaving only 0.79ms for fuel delivery
2. **MASS_SMOOTHING_ALPHA = 0.4** — Too responsive for small masses. At 1ms pulse range, can cause rapid oscillation
3. **Inconsistent mass usage** — Some fallback paths used `baseFuelMass` instead of `smoothedMass`, breaking consistency
4. **No threshold protection** — Very small masses weren't protected from over-smoothing

## Changes Made (Jan 9, 2026)

### 1. Reduced DEADTIME_MULTIPLIER (line ~21)
```cpp
// Before
constexpr float DEADTIME_MULTIPLIER = 2.1f;

// After
constexpr float DEADTIME_MULTIPLIER = 1.5f;  // More realistic for 1ms pulses
```

**Rationale**: 1.5x still enforces injector safety (pulse must be 1.5× deadtime) but allows 0.85ms fuel pulses instead of forcing 0.21ms minimum.

### 2. Lowered MASS_SMOOTHING_ALPHA (line ~45)
```cpp
// Before
constexpr float MASS_SMOOTHING_ALPHA = 0.4f;

// After
constexpr float MASS_SMOOTHING_ALPHA = 0.25f;  // Slower response, less responsive to noise
constexpr float MIN_MASS_FOR_SMOOTHING = 0.0001f;  // Bypass smoothing for tiny masses
```

**Rationale**: 
- Lower α (0.25) reduces oscillation at boundary masses
- MIN_MASS_FOR_SMOOTHING threshold prevents over-damping on very small pulses (< 0.0001g)
- Maintains EMA benefit while reducing jitter

### 3. Enhanced Smoothing Logic (lines ~245-263)
```cpp
// New code: check minimum threshold before applying smoothing
if (baseFuelMass < MIN_MASS_FOR_SMOOTHING) {
    smoothedMass = baseFuelMass;  // Pass through unchanged
    smoothedInjectionMass[cylinderNumber] = baseFuelMass;
} else {
    // Apply EMA smoothing only for significant masses
    smoothedMass = prev * (1.0f - MASS_SMOOTHING_ALPHA) + baseFuelMass * MASS_SMOOTHING_ALPHA;
}
```

### 4. Fixed Mass Consistency (4 locations)
Changed all fallback paths to use `smoothedMass` instead of `baseFuelMass` for duration calculation:
- Line ~306: Single-mode via hysteresis
- Line ~377: Fallback due to short pulse tolerance exceeded
- Line ~400: Fallback when not in multi-mode
- Line ~468: Fallback during validation

**Rationale**: Duration calculation must be consistent. If angles use smoothed values, durations should too.

## Expected Impact

### Positive
- **Less jitter**: Lower α + threshold protection = smoother 1ms pulse transitions
- **Faster micro-transitions**: α=0.25 responds faster than 0.4 to mass changes (but filtered)
- **Better 100%/0% split handling**: Reduced oscillation at boundaries due to lower smoothing aggression
- **Small pulse stability**: MIN_MASS_FOR_SMOOTHING prevents sluggish response on light loads

### Trade-offs
- **Slightly less aggressive deadtime enforcement** (1.5x vs 2.1x) — still safe, more realistic
- **Tolerance counter more active** — may see brief single-mode fallbacks, then re-enter multi (expected transient)

## Testing Recommendations

1. **Scope test**: Monitor injector pulse durations on small throttle openings
   - Expected: Smooth delivery 0.7-1.5ms without skipping
   - Bad: Oscillating 0.74 → 1.0 → skip pattern

2. **Logs**: Watch for patterns in tolerance counter
   ```
   mi_pulse_short_tolerate: cyl=X p0 0.98 ms < min 1.15 ms (tolerance count: 1)
   ```
   Should see occasional violations, not continuous

3. **VE table changes**: Adjust VE +/- 5% and verify mass/duration smoothly follows

4. **RPM transitions**: 1500 → 2500 RPM at 10% throttle should show smooth fuel ramp, not jitter

## Constants Summary
```cpp
DEADTIME_MULTIPLIER = 1.5f          // Was 2.1f (more lenient on small pulses)
MASS_SMOOTHING_ALPHA = 0.25f        // Was 0.4f (slower, less responsive to noise)
MIN_MASS_FOR_SMOOTHING = 0.0001f    // New: protect micro-pulses from over-smoothing
MULTI_INJECTION_HYSTERESIS_LOW = 5.0f    // Unchanged
MULTI_INJECTION_HYSTERESIS_HIGH = 95.0f  // Unchanged
MULTI_INJECTION_VALIDATION_FAILURE_TOLERANCE = 2  // Unchanged
```

## Files Modified
- `controllers/engine_cycle/multi_injection.cpp` — 4 replacements in constant definitions and smoothing logic

## Monitoring Points
Enable in `isVerboseMultiInjection()` to see:
- When smoothing threshold is triggered (`baseFuelMass < MIN_MASS_FOR_SMOOTHING`)
- When tolerance counter increments/resets
- Mode transitions (single ↔ multi)
- Actual smoothed vs unsmoothed mass values frame-by-frame
