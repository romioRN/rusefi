/**
 * @file multi_injection.cpp
 * @brief Multi-injection fuel scheduling implementation
 * 
 * Handles split fuel injection with two separate pulses:
 * - Pulse 0: Main injection (60% default)
 * - Pulse 1: Secondary injection (40% default)
 * 
 * Features:
 * - 3D table-based split ratio (RPM × Load)
 * - 3D table-based secondary injection angle
 * - Support for START/CENTER/END injection timing modes
 * - Automatic dwell angle validation
 */

#include "pch.h"
#include "fuel_schedule.h"
#include "injector_model.h"

#define MAX_INJECTION_DURATION 120.0f  // Maximum pulse duration in crankshaft degrees
// Multiplier used to require pulse duration to exceed dead time by this factor
// Use 2.5x as a conservative enforcement factor (must be a valid float literal)
constexpr float DEADTIME_MULTIPLIER = 2.1f;

// Hysteresis thresholds for single <-> multi injection mode transitions
// Prevent oscillation when split ratio hovers near 0% or 100%
// If active in multi-mode (true), stay multi until split < HYSTERESIS_LOW
// If active in single-mode (false), enter multi only when split > HYSTERESIS_LOW and < HYSTERESIS_HIGH
constexpr float MULTI_INJECTION_HYSTERESIS_LOW = 5.0f;    // Switch to multi if split between 5-95%
constexpr float MULTI_INJECTION_HYSTERESIS_HIGH = 95.0f;  // Switch back to single if split > 95% or < 5%

#if EFI_ENGINE_CONTROL

// Per-cylinder hysteresis state: track if each cylinder is in multi-injection mode
// index = cylinder number (0-11), value = true if last state was multi-injection
static bool multiInjectionModeActive[12] = {};  // Default all false (single mode)

// Per-cylinder validation failure tolerance counter
// Allows multi-injection to tolerate brief validation failures before falling back to single
// Prevents premature fallback due to transient dwell/timing violations
static uint8_t multiInjectionValidationFailureCount[12] = {};  // Counts consecutive validation failures
constexpr uint8_t MULTI_INJECTION_VALIDATION_FAILURE_TOLERANCE = 2;  // Allow 2-3 failed cycles

// Per-cylinder smoothed injection mass to reduce jitter in calculated durations
static float smoothedInjectionMass[12] = {};
// Smoothing factor alpha for exponential moving average (0..1). Higher = faster response, lower = smoother.
constexpr float MASS_SMOOTHING_ALPHA = 0.4f;

// Helper: get current validation failure count for diagnostics
static inline uint8_t getMultiInjectionValidationFailureCount(uint8_t cylinderNumber) {
  return (cylinderNumber < 12) ? multiInjectionValidationFailureCount[cylinderNumber] : 0;
}

/**
 * Normalizes crankshaft angle to 0-720° range
 * Handles negative angles and angles exceeding 720°
 * @param angle Raw angle in degrees
 * @return Normalized angle in 0-720° range
 */
static float normalizeAngle(float angle) {
  while (angle < 0) angle += 720.0f;
  while (angle >= 720.0f) angle -= 720.0f;
  return angle;
}

// Helper: runtime verbose control for multi-injection logging
// The engine configuration currently doesn't expose a dedicated
// `isVerboseMultiInjection` flag in the generated configuration struct.
// Provide a local stub so we can gate verbose prints. This returns
// false by default; it can be extended later to consult a user setting.
static inline bool isVerboseMultiInjection() {
  return false;
}

/**
 * Calculates fuel split ratio from 3D interpolation table
 * Ratio determines percentage of fuel delivered in each pulse
 * 
 * @param pulseIndex Pulse index (0 or 1)
 * @return Split ratio as percentage (0-100%)
 *         Pulse 0: 60% (default)
 *         Pulse 1: 40% (default)
 */
float InjectionEvent::computeSplitRatio(uint8_t pulseIndex) const {
  if (pulseIndex >= 2) {
    return 0.0f;
  }
  
  // 3D interpolation: Load (Y) vs RPM (X)
  float ratio = interpolate3d(
    engineConfiguration->multiInjectionSplitRatioTable,    // 16×16 table
    engineConfiguration->multiInjectionLoadBins,           // Load axis (50-350%)
    getFuelingLoad(),                                      // Current load
    engineConfiguration->multiInjectionRpmBins,            // RPM axis (1000-8500)
    Sensor::getOrZero(SensorType::Rpm)                     // Current RPM
  );
  
  // Interpret table value as percentage for the FIRST pulse (pulseIndex==0).
  // If table specifies 100 => all fuel to Pulse 0; 0 => all fuel to Pulse 1.
  if (!std::isnan(ratio) && ratio >= 0.0f && ratio <= 100.0f) {
    if (pulseIndex == 0) {
      return std::clamp(ratio, 0.0f, 100.0f);
    } else {
      return std::clamp(100.0f - ratio, 0.0f, 100.0f);
    }
  }

  // Fallback: if table invalid (NaN or out of bounds), use hardcoded defaults (60/40)
  return (pulseIndex == 0 ? 60.0f : 40.0f);
}

/**
 * Calculates injection angle for each pulse
 * 
 * Pulse 0: Uses standard computeInjectionAngle() (main injection)
 * Pulse 1: Uses 3D table + timing mode correction (secondary injection)
 * 
 * Respects injection timing modes:
 * - START: Angle unchanged (pulse starts at angle)
 * - CENTER: Angle shifted by duration/2 (pulse centered at angle)
 * - END: Angle shifted by full duration (pulse ends at angle)
 * 
 * @param pulseIndex Pulse index (0 = Pulse 0, 1 = Pulse 1)
 * @return Injection start angle in degrees (0-720°)
 */
float InjectionEvent::computeSecondaryInjectionAngle(uint8_t pulseIndex) const {
  if (pulseIndex == 0) {
    // Pulse 0: Use standard injection angle calculation
    auto result = computeInjectionAngle();
    return result ? result.Value : 0.0f;
  }
  
  if (pulseIndex == 1) {
    // Pulse 1: Use secondary injection angle table
    // Note: secondInjectionAngleTable contains BTDC angles (0-720)
    // where large values = earlier injection (more advance)
    // We need to convert BTDC to absolute engine cycle angle
    
    float btdcAngle = interpolate3d(
      engineConfiguration->secondInjectionAngleTable,      // 16×16 table
      engineConfiguration->multiInjectionLoadBins,         // Load axis
      getFuelingLoad(),                                    // Current load
      engineConfiguration->multiInjectionRpmBins,          // RPM axis
      Sensor::getOrZero(SensorType::Rpm)                   // Current RPM
    );
    
    // Validate BTDC angle from table
    if (btdcAngle < 5.0f || btdcAngle > 720.0f) {
      btdcAngle = 100.0f;  // Fallback to safe default (100° BTDC)
    }
    
    // Convert BTDC to absolute engine cycle angle
    // If table says 100° BTDC (100° before TDC), 
    // in absolute cycle this is: 720° - 100° = 620°
    float baseAngle = 720.0f - btdcAngle;
    if (baseAngle < 0) {
      baseAngle += 720.0f;
    }
    
    // Get engine rotation data for angle calculations
    floatus_t oneDegreeUs = getEngineRotationState()->getOneDegreeUs();
    if (std::isnan(oneDegreeUs) || oneDegreeUs < 0.1f) {
      return normalizeAngle(baseAngle);
    }
    
    // Calculate pulse duration in crankshaft degrees
    float durationAngle = MS2US(getNumberOfPulses() > pulseIndex ? getPulse(pulseIndex).fuelMs : 0) / oneDegreeUs;
    if (durationAngle > MAX_INJECTION_DURATION) {
      durationAngle = MAX_INJECTION_DURATION;
    }
    
    // Apply injection timing mode correction
    // These modes define what the table angle represents:
    // - START: angle is START of injection
    // - CENTER: angle is CENTER of injection, so shift start back by duration/2
    // - END: angle is END of injection, so shift start back by full duration
    auto mode = engineConfiguration->injectionTimingMode;
    float correctedAngle = baseAngle;
    
    if (mode == InjectionTimingMode::Center) {
      // Pulse 1 centered at table angle: shift START back by half duration
      correctedAngle -= durationAngle * 0.5f;
    } else if (mode == InjectionTimingMode::End) {
      // Pulse 1 ends at table angle: shift START back by full duration
      correctedAngle -= durationAngle;
    }
    // START mode: no correction (pulse starts at table angle)
    
    // Convert table angle (engine-reference) to per-cylinder angle by adding
    // firing-order offset so each cylinder gets its own start angle
    correctedAngle += getPerCylinderFiringOrderOffset(ownIndex, cylinderNumber);

    return normalizeAngle(correctedAngle);
  }
  
  return 0.0f;
}

/**
 * Updates multi-injection angle calculations
 * Called during fuel schedule generation to validate and calculate angles
 * 
 * Validates:
 * - Multi-injection enabled
 * - Valid engine state (RPM, fuel duration, rotation state)
 * - Injection windows don't overlap (minimum dwell maintained)
 * 
 * @returns true if calculation successful, false if engine state invalid
 */
/**
 * @brief Вычисляет параметры мультиинъекции для двух импульсов (split ratio, fuelMs, угол, isActive)
 * Заполняет pulses[] для каждого пульса. Если условия не соблюдаются — делает возврат к одиночному впрыску
 * 
 * Оптимизирована для быстрого переключения между single и multi режимами:
 * - Ранний выход при 100%/0% split (переход на single-injection без пересчёта второго пульса)
 * - Минимизация вычислений в горячем пути
 * 
 * Diagnostic checks:
 * - Validates injection mass availability
 * - Checks engine rotation state
 * - Validates pulse durations against injector model dead time
 * - Validates injection windows (overlap, dwell, ordering)
 * - Ensures timing safety margins
 * 
 * @return true, если расчёт успешен; false если ошибка (некорректный fuel, RPM, перекрытие)
 */
bool InjectionEvent::updateMultiInjectionAngles() {
    // 1. Защита: если мультиинъекция не включена или только один пульс — fallback к single injection
    if (!engineConfiguration->multiInjection.enableMultiInjection || getNumberOfPulses() == 1) {
        numberOfPulses = 1;
        pulses[0].splitRatio = 100.0f;
        pulses[0].fuelMs = getEngineState()->injectionDuration;
        pulses[0].isActive = true;
        return updateInjectionAngle();
    }

    // 2. Получить базовую МАССУ топлива для этого цилиндра (не длительность!)
    float baseFuelMass = getEngineState()->injectionMass[cylinderNumber];
    // Apply light exponential smoothing to baseFuelMass to reduce jitter in calculated durations.
    // Angles still come from tables each cycle; smoothing only affects durations calculation.
    float smoothedMass = baseFuelMass;
    if (cylinderNumber < 12) {
      float prev = smoothedInjectionMass[cylinderNumber];
      if (std::isnan(prev) || prev <= 0.0f) {
        smoothedInjectionMass[cylinderNumber] = baseFuelMass;
        smoothedMass = baseFuelMass;
      } else {
        smoothedMass = prev * (1.0f - MASS_SMOOTHING_ALPHA) + baseFuelMass * MASS_SMOOTHING_ALPHA;
        smoothedInjectionMass[cylinderNumber] = smoothedMass;
      }
    }
    if (std::isnan(baseFuelMass) || baseFuelMass <= 0) {
        warning(ObdCode::CUSTOM_MULTI_INJECTION_INVALID_CONFIG, "mi_invalid_mass: baseMass=%.3f g", baseFuelMass);
        numberOfPulses = 1;
        return false;
    }

    // 3. Проверить вращение двигателя
    float rpm = Sensor::getOrZero(SensorType::Rpm);
    if (rpm < 1) {
        warning(ObdCode::CUSTOM_MULTI_INJECTION_INVALID_CONFIG, "mi_rpm_invalid: rpm=%.0f", rpm);
        numberOfPulses = 1;
        return false;
    }

    // 4. Проверить, есть ли данные по таймингу
    floatus_t oneDegreeUs = getEngineRotationState()->getOneDegreeUs();
    if (std::isnan(oneDegreeUs) || oneDegreeUs < 0.1f) {
        warning(ObdCode::CUSTOM_MULTI_INJECTION_INVALID_CONFIG, "mi_timing_invalid: oneDegreeUs=%.3f", oneDegreeUs);
        numberOfPulses = 1;
        return false;
    }

    // Get deadtime for validation
    float deadtime = engine->module<InjectorModelPrimary>()->getDeadtime();

    // === HYSTERESIS-BASED MODE SELECTION ===
    // Use hysteresis to prevent oscillation when split ratio hovers near 0% or 100%
    // This prevents the "jitter" on oscilloscope when table values are near boundary
    float splitForPulse0 = computeSplitRatio(0); // percent for pulse0
    bool wasMultiModeActive = (cylinderNumber < 12) ? multiInjectionModeActive[cylinderNumber] : false;
    
    if (!std::isnan(splitForPulse0)) {
        bool shouldEnterMultiMode = false;
        
        if (wasMultiModeActive) {
            // Currently in multi mode: stay in multi unless split reaches hysteresis boundary
            // Exit multi-mode only if split < LOW or > HIGH threshold
            if (splitForPulse0 < MULTI_INJECTION_HYSTERESIS_LOW || splitForPulse0 > MULTI_INJECTION_HYSTERESIS_HIGH) {
                shouldEnterMultiMode = false;  // Exit multi mode
            } else {
                shouldEnterMultiMode = true;   // Stay in multi mode (hysteresis prevents exit)
            }
        } else {
            // Currently in single mode: enter multi only if split is solidly in the middle
            // Require split to be within HYSTERESIS_LOW to HYSTERESIS_HIGH range
            if (splitForPulse0 > MULTI_INJECTION_HYSTERESIS_LOW && splitForPulse0 < MULTI_INJECTION_HYSTERESIS_HIGH) {
                shouldEnterMultiMode = true;   // Enter multi mode
            } else {
                shouldEnterMultiMode = false;  // Stay in single mode
            }
        }
        
        // Update hysteresis state for this cylinder
        if (cylinderNumber < 12) {
            multiInjectionModeActive[cylinderNumber] = shouldEnterMultiMode;
        }
        
        // If hysteresis logic says to use single mode, route all fuel to appropriate pulse
        if (!shouldEnterMultiMode) {
            numberOfPulses = 1;
            pulses[0].splitRatio = 100.0f;
            floatms_t singlePulseFuelMs = engine->module<InjectorModelPrimary>()->getInjectionDuration(baseFuelMass);
            pulses[0].fuelMs = singlePulseFuelMs;
            pulses[0].isActive = (singlePulseFuelMs > 0.05f);
            
            // Decide which injection angle to use based on which side of the hysteresis we're on
            if (splitForPulse0 <= MULTI_INJECTION_HYSTERESIS_LOW) {
                // Split was very low (toward 0%) → use secondary angle (pulse 1)
                InjectionPulse savedPulse1 = pulses[1];
                uint8_t previousNumberOfPulses = numberOfPulses;
                numberOfPulses = 2;
                pulses[1].fuelMs = singlePulseFuelMs;
                float secAngle = computeSecondaryInjectionAngle(1);
                pulses[1] = savedPulse1;
                numberOfPulses = previousNumberOfPulses;
                pulses[0].startAngle = secAngle;
            } else {
                // Split was high (toward 100%) or invalid → use primary angle (pulse 0)
                return updateInjectionAngle();
            }
            
            return updateInjectionAngle();
        }
    }

    // === MULTI-INJECTION MODE: Both pulses active ===
    // Only reach here if hysteresis logic says to use multi-injection
    
    // 5. Вычислить параметры для каждого пульса
    float minPulseMs = deadtime * DEADTIME_MULTIPLIER;  // Minimum acceptable pulse duration
    
    for (uint8_t i = 0; i < numberOfPulses; i++) {
        float ratio = computeSplitRatio(i);                           // Процент топливного объёма этого пульса
        float pulseMass = smoothedMass * (ratio / 100.0f);            // Масса для этого пульса (smoothed for duration calc)
        
        // Рассчитать длительность этого пульса через injectorModel
        floatms_t pulseFuelMs = engine->module<InjectorModelPrimary>()->getInjectionDuration(pulseMass);
        
        pulses[i].fuelMs = pulseFuelMs;
        
        // EARLY VALIDATION: Check if pulse duration violates minimum threshold
        // If in multi-mode and pulse too short, treat as validation failure (increment counter)
        // Don't instant fallback; let tolerance counter decide
        if (pulseFuelMs > 0.001f && pulseFuelMs < minPulseMs) {
            if (wasMultiModeActive && cylinderNumber < 12) {
                // Increment failure counter for this reason
                multiInjectionValidationFailureCount[cylinderNumber]++;
                
                if (multiInjectionValidationFailureCount[cylinderNumber] > MULTI_INJECTION_VALIDATION_FAILURE_TOLERANCE) {
                    // Tolerance exceeded due to short pulse. Fallback to single.
                    numberOfPulses = 1;
                    pulses[0].splitRatio = 100.0f;
                    floatms_t singlePulseFuelMs = engine->module<InjectorModelPrimary>()->getInjectionDuration(baseFuelMass);
                    pulses[0].fuelMs = singlePulseFuelMs;
                    pulses[0].isActive = true;
                    multiInjectionValidationFailureCount[cylinderNumber] = 0;
                    multiInjectionModeActive[cylinderNumber] = false;
                    
                    if (isVerboseMultiInjection()) {
                        efiPrintf("mi_pulse_short: cyl=%d p%d %.2f ms < min %.2f ms, fallback after %d cycles",
                          cylinderNumber, i, pulseFuelMs, minPulseMs, multiInjectionValidationFailureCount[cylinderNumber]);
                    }
                    
                    return updateInjectionAngle();
                }
                
                // Still within tolerance. Continue with warning but don't fallback yet.
                if (isVerboseMultiInjection()) {
                    efiPrintf("mi_pulse_short_tolerate: cyl=%d p%d %.2f ms < min %.2f ms (tolerance count: %d)",
                      cylinderNumber, i, pulseFuelMs, minPulseMs, multiInjectionValidationFailureCount[cylinderNumber]);
                }
            } else {
                // Not in multi-mode already. This shouldn't happen, but fallback anyway.
                numberOfPulses = 1;
                pulses[0].splitRatio = 100.0f;
                floatms_t singlePulseFuelMs = engine->module<InjectorModelPrimary>()->getInjectionDuration(baseFuelMass);
                pulses[0].fuelMs = singlePulseFuelMs;
                pulses[0].isActive = true;
                return updateInjectionAngle();
            }
        }
        
        pulses[i].splitRatio = ratio;

        // Расчёт длительности в градусах КВ
        float pulseDurationAngle = MS2US(pulseFuelMs) / oneDegreeUs;
        if (pulseDurationAngle > MAX_INJECTION_DURATION) {
            pulseDurationAngle = MAX_INJECTION_DURATION;
        }
        pulses[i].durationAngle = pulseDurationAngle;

        // Угол впрыска для каждого пульса по 3D-таблице
        pulses[i].startAngle = computeSecondaryInjectionAngle(i);

        // Диагностика: проверить что длительность не ниже deadtime
        if (pulseFuelMs > 0.001f && pulseFuelMs < deadtime) {
            warning(ObdCode::CUSTOM_MULTI_INJECTION_PULSE_TOO_SHORT, 
              "mi_p%d_below_deadtime: fuelMs=%.2f ms < deadtime=%.2f ms cyl=%d", 
              i, pulseFuelMs, deadtime, cylinderNumber);
        }

        // Отметить пульс как активный только если есть топливо и валидный угол
        pulses[i].isActive = (pulseFuelMs > 0.05f && !std::isnan(pulses[i].startAngle));
    }

    // 6. Проверить перекрытие окон (минимальный dwell) и все другие условия
    if (!validateInjectionWindows()) {
        // Validation failed. Decide whether to fallback or tolerate based on hysteresis.
        bool shouldFallback = true;
        
        if (wasMultiModeActive && cylinderNumber < 12) {
            // We were in multi-mode and validation just failed.
            // Increment failure counter and only fallback if tolerance exceeded.
            multiInjectionValidationFailureCount[cylinderNumber]++;
            
            if (multiInjectionValidationFailureCount[cylinderNumber] <= MULTI_INJECTION_VALIDATION_FAILURE_TOLERANCE) {
                // We're still within tolerance. Tolerate this failure, don't fallback yet.
                shouldFallback = false;
                if (isVerboseMultiInjection()) {
                    efiPrintf("mi_validate_tolerate: cyl=%d failures=%d (tolerance=%d), staying in multi-mode",
                      cylinderNumber, 
                      multiInjectionValidationFailureCount[cylinderNumber],
                      MULTI_INJECTION_VALIDATION_FAILURE_TOLERANCE);
                }
            } else {
                // Tolerance exceeded. Fallback to single injection.
                shouldFallback = true;
                if (isVerboseMultiInjection()) {
                    efiPrintf("mi_validate_exceeded: cyl=%d failures=%d > tolerance=%d, falling back to single",
                      cylinderNumber, 
                      multiInjectionValidationFailureCount[cylinderNumber],
                      MULTI_INJECTION_VALIDATION_FAILURE_TOLERANCE);
                }
            }
        } else {
            // We were in single-mode already. Any validation failure confirms we should stay single.
            shouldFallback = true;
        }
        
        if (shouldFallback) {
            // Fallback: use single injection
            numberOfPulses = 1;
            pulses[0].splitRatio = 100.0f;
            floatms_t singlePulseFuelMs = engine->module<InjectorModelPrimary>()->getInjectionDuration(baseFuelMass);
            pulses[0].fuelMs = singlePulseFuelMs;
            pulses[0].isActive = true;
            
            // Reset failure counter and mode flag on fallback
            if (cylinderNumber < 12) {
                multiInjectionValidationFailureCount[cylinderNumber] = 0;
                multiInjectionModeActive[cylinderNumber] = false;
            }
            
            return updateInjectionAngle();
        }
        // If we didn't fallback, continue with current multi-injection configuration
        // (validation failed but we're tolerating it)
    } else {
        // Validation PASSED. Reset failure counter when in multi-mode.
        if (cylinderNumber < 12) {
            multiInjectionValidationFailureCount[cylinderNumber] = 0;
        }
    }

    // Всё ок! Multi-injection режим активен
    if (isVerboseMultiInjection()) {
        efiPrintf("mi_update_ok: baseMass=%.3f g p0[%.1f° %.2fms %.1f°] p1[%.1f° %.2fms %.1f°] cyl=%d",
          baseFuelMass,
          pulses[0].startAngle, pulses[0].fuelMs, pulses[0].durationAngle,
          pulses[1].startAngle, pulses[1].fuelMs, pulses[1].durationAngle,
          cylinderNumber);
    }
    
    return true;
}


/**
 * Comprehensive validation for multi-injection windows
 * Checks multiple error conditions and generates diagnostics
 * 
 * Validation checks:
 * 1. Minimum dwell between pulses (configurated in table)
 * 2. Pulse 1 start angle not before Pulse 0 start angle (correct ordering)
 * 3. Pulse durations not below injector dead time
 * 4. Pulse durations within acceptable range (0.05-100 ms)
 * 5. Injection timing safety (starts/ends not later than 15° before ignition)
 * 
 * Early exit if single-pulse mode (saves CPU cycles)
 * 
 * @returns true if all validations pass, false if any fail (triggers fallback)
 */
bool InjectionEvent::validateInjectionWindows() {
  // Quick exit: validation only needed for true multi-injection (2 pulses)
  if (getNumberOfPulses() < 2) {
    return true;  // Single injection mode doesn't need window validation
  }

  const auto& pulse0 = getPulse(0);
  const auto& pulse1 = getPulse(1);

  // === DIAGNOSTIC: Get engine state info ===
  float rpm = Sensor::getOrZero(SensorType::Rpm);
  float ignitionAngle = getEngineState()->timingAdvance[cylinderNumber];
  
  // Safety margin before ignition (in degrees): injection should end by 15° before TDC
  const float SAFE_MARGIN_BEFORE_IGNITION = 15.0f;
  float safeEndAngle = normalizeAngle(ignitionAngle - SAFE_MARGIN_BEFORE_IGNITION);

  // Get minimum dwell angle from table
  float minDwell = interpolate3d(
    engineConfiguration->minDwellAngleTable,
    engineConfiguration->multiInjectionLoadBins,
    getFuelingLoad(),
    engineConfiguration->multiInjectionRpmBins,
    rpm
  );
  minDwell = std::clamp(minDwell, 5.0f, 50.0f);

  // Compute normalized pulse boundaries and a wrap-aware dwell (angular distance)
  float pulse0End = normalizeAngle(pulse0.startAngle + pulse0.durationAngle);
  float pulse1Start = normalizeAngle(pulse1.startAngle);

  // Modular angular difference from p0 end to p1 start in 0..720 range
  float dwell = pulse1Start - pulse0End;
  if (dwell < 0.0f) {
    dwell += 720.0f;
  }

  // Now check minimum dwell between pulses using wrap-aware value
  if (dwell < minDwell) {
    warning(ObdCode::CUSTOM_MULTI_INJECTION_INSUFFICIENT_DWELL,
      "mi_dwell_low: dwell=%.1f° min=%.1f° (p0_end=%.1f° p1_start=%.1f°)",
      dwell, minDwell, pulse0End, pulse1Start);
    return false;
  }

  // === CHECK 3: Pulse durations valid (vs dead time) ===
  // Enforce stricter minimum: require each pulse to be at least DEADTIME_MULTIPLIER * deadtime
  float deadtime = engine->module<InjectorModelPrimary>()->getDeadtime();
  float minPulseMs = deadtime * DEADTIME_MULTIPLIER;

  for (uint8_t i = 0; i < 2; i++) {
    const auto& pulse = getPulse(i);

    // Check if pulse is too short (< minPulseMs means unreliable delivery)
    if (pulse.fuelMs > 0.001f && pulse.fuelMs < minPulseMs) {
      warning(ObdCode::CUSTOM_MULTI_INJECTION_PULSE_TOO_SHORT,
        "mi_p%d_below_deadtime_threshold: fuelMs=%.2f ms < min(%.2f ms = %.2fx%.2f ms)", 
        i, pulse.fuelMs, minPulseMs, DEADTIME_MULTIPLIER, deadtime);
      return false;
    }

    // Check if pulse duration exceeds max (hardware/scheduling limit)
    if (pulse.durationAngle > MAX_INJECTION_DURATION) {
      warning(ObdCode::CUSTOM_MULTI_INJECTION_PULSE_TOO_LONG,
        "mi_p%d_long: durationAngle=%.1f° max=%.1f°", i, pulse.durationAngle, MAX_INJECTION_DURATION);
      return false;
    }
  }

  // === CHECK 4: Injection timing safety (starts/ends before ignition) ===
  for (uint8_t i = 0; i < 2; i++) {
    const auto& pulse = getPulse(i);
    
    // Calculate pulse end angle
    float pulseStart = pulse.startAngle;
    float pulseEnd = normalizeAngle(pulse.startAngle + pulse.durationAngle);
    
    // Check if pulse starts too late (after safe margin before ignition)
    // In this check, we want to ensure injection completes well before spark
    // "Late" means starting after the safe margin point
    if (pulseStart > safeEndAngle) {
      float delta = pulseStart - ignitionAngle;
      if (delta < 0) delta += 720.0f;
      if (delta < SAFE_MARGIN_BEFORE_IGNITION) {
        warning(ObdCode::CUSTOM_MULTI_INJECTION_STARTS_TOO_LATE, 
          "mi_p%d_late_start: starts=%.1f° ign=%.1f° margin=%.1f° (need >=%.1f°)", 
          i, pulseStart, ignitionAngle, delta, SAFE_MARGIN_BEFORE_IGNITION);
        return false;
      }
    }

    // Check if pulse ends too late (overlaps with ignition safety window)
    if (pulseEnd > safeEndAngle) {
      float delta = pulseEnd - ignitionAngle;
      if (delta < 0) delta += 720.0f;
      if (delta < SAFE_MARGIN_BEFORE_IGNITION) {
        warning(ObdCode::CUSTOM_MULTI_INJECTION_ENDS_TOO_LATE, 
          "mi_p%d_late_end: ends=%.1f° ign=%.1f° margin=%.1f° (need >=%.1f°)", 
          i, pulseEnd, ignitionAngle, delta, SAFE_MARGIN_BEFORE_IGNITION);
        return false;
      }
    }
  }

  // (Overlap is implicitly covered by the dwell check above in a wrap-aware manner)

  // All checks passed
  if (isVerboseMultiInjection()) {
    efiPrintf("mi_valid: p0[%.1f°,%.2fms,%.1f°] p1[%.1f°,%.2fms,%.1f°] dwell=%.1f° rpm=%.0f",
      pulse0.startAngle, pulse0.fuelMs, pulse0.durationAngle,
      pulse1.startAngle, pulse1.fuelMs, pulse1.durationAngle,
      dwell, rpm);
  }

  return true;
}

#endif // EFI_ENGINE_CONTROL
