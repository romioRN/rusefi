# EGT Limiter Build Fix (Jan 9, 2026)

## Compilation Errors Fixed

### Error 1: `.value()` method doesn't exist for expected type
**Location**: egtLimiter.cpp line 57
**Error**: `'struct expected<float>' has no member named 'value'`

**Fix**: Changed `egtReading.value()` → `egtReading.value_or(0.0f)`
- `Sensor::get()` returns `expected<T>` type
- Correct method is `.value_or()` with fallback value
- Maintains safety: returns 0.0f if sensor unavailable

### Error 2: `isVerboseMultiInjection()` not in scope
**Location**: egtLimiter.cpp line 80
**Error**: `'isVerboseMultiInjection' was not declared in this scope`

**Fix**: Replaced with `warning()` macro instead
```cpp
// Before
if (isVerboseMultiInjection()) {
    efiPrintf("EGT SENSOR ERROR: ...");
}

// After  
warning(ObdCode::CUSTOM_EGT_SENSOR_ERROR, "EGT SENSOR ERROR: ...");
```

**Rationale**:
- `isVerboseMultiInjection()` is local to multi_injection.cpp (not global)
- `warning()` is the standard rusefi diagnostic mechanism
- More portable and integrates with dashboard/logs

## Changes Summary

| File | Line | Change |
|------|------|--------|
| egtLimiter.cpp | 57 | `.value()` → `.value_or(0.0f)` |
| egtLimiter.cpp | 80-82 | `if (isVerboseMultiInjection())` → `warning(CUSTOM_EGT_SENSOR_ERROR, ...)` |

## Verification

✅ No compilation errors in egtLimiter.cpp  
✅ All fixes maintain fail-safe logic intact  
✅ EGT sensor error reporting improved (uses warning system)  
✅ Ready for full firmware build

## Build Status
**Warnings**: Only unused parameter warnings (non-critical)  
**Errors**: None  
**Build**: Ready to proceed
