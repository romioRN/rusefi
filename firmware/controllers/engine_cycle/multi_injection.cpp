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

#define MAX_INJECTION_DURATION 120.0f  // Maximum pulse duration in crankshaft degrees

#if EFI_ENGINE_CONTROL

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
  
  // If table value is valid, interpret it as the percentage for the FIRST pulse
  // (pulseIndex == 0). The second pulse should receive the complement so that
  // both pulses sum to 100% (unless the table contains invalid data).
  if (ratio > 0.1f && ratio < 100.0f) {
    if (pulseIndex == 0) {
      return std::clamp(ratio, 0.0f, 100.0f);
    } else {
      return std::clamp(100.0f - ratio, 0.0f, 100.0f);
    }
  }

  // Fallback: if table invalid, use hardcoded defaults (60/40)
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
    // Table stores negative values (degrees before TDC), like injectionOffset
    float baseAngle = interpolate3d(
      engineConfiguration->secondInjectionAngleTable,      // 16×16 table (negative BTDC values)
      engineConfiguration->multiInjectionLoadBins,         // Load axis
      getFuelingLoad(),                                    // Current load
      engineConfiguration->multiInjectionRpmBins,          // RPM axis
      Sensor::getOrZero(SensorType::Rpm)                   // Current RPM
    );
    
    // Validate base angle: must be in range [-50°, -5°] BTDC
    if (baseAngle > -5.0f || baseAngle < -50.0f) {
      baseAngle = -100.0f;  // Fallback to safe default (-100° BTDC)
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
    // baseAngle is negative (e.g., -60 means 60° BTDC)
    // For mode corrections, work with the absolute magnitude first
    auto mode = engineConfiguration->injectionTimingMode;
    float correctedAngle = baseAngle;  // Start with negative value
    
    if (mode == InjectionTimingMode::Center) {
      // Pulse 1 centered at table angle: move earlier (more negative) by half duration
      correctedAngle -= durationAngle * 0.5f;
    } else if (mode == InjectionTimingMode::End) {
      // Pulse 1 ends at table angle: move earlier (more negative) by full duration
      correctedAngle -= durationAngle;
    }
    // START mode: no correction (pulse starts at table angle)
    
    // Clamp to [-50°, -5°] range to ensure reasonable timing
    correctedAngle = std::clamp(correctedAngle, -50.0f, -5.0f);
    
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

    // 2. Получить базовую длительность одного полного впрыска
    floatms_t baseFuelMs = getEngineState()->injectionDuration;
    if (std::isnan(baseFuelMs) || baseFuelMs <= 0) {
        return false;
    }

    // 3. Проверить вращение двигателя
    float rpm = Sensor::getOrZero(SensorType::Rpm);
    if (rpm < 1) {
        return false;
    }

    // 4. Проверить, есть ли данные по таймингу
    floatus_t oneDegreeUs = getEngineRotationState()->getOneDegreeUs();
    if (std::isnan(oneDegreeUs) || oneDegreeUs < 0.1f) {
        return false;
    }

    // 5. Вычислить параметры для каждого пульса
    for (uint8_t i = 0; i < numberOfPulses; i++) {
        float ratio = computeSplitRatio(i);                               // Процент топливного объёма этого пульса
        floatms_t pulseFuelMs = baseFuelMs * (ratio / 100.0f);            // Длительность
        pulses[i].fuelMs = pulseFuelMs;
        pulses[i].splitRatio = ratio;

        // Расчёт длительности в градусах КВ
        float pulseDurationAngle = MS2US(pulseFuelMs) / oneDegreeUs;
        if (pulseDurationAngle > MAX_INJECTION_DURATION) {
            pulseDurationAngle = MAX_INJECTION_DURATION;
        }
        pulses[i].durationAngle = pulseDurationAngle;

        // Угол впрыска для каждого пульса по 3D-таблице
        pulses[i].startAngle = computeSecondaryInjectionAngle(i);

        // Отметить пульс как активный только если есть топливо и валидный угол
        pulses[i].isActive = (pulseFuelMs > 0.05f && !std::isnan(pulses[i].startAngle));
    }

    // 6. Проверить перекрытие окон (минимальный dwell)
    if (!validateInjectionWindows()) {
        // Fallback: если ошибка перекрытия, разрешаем только 1 пульс — single injection
        numberOfPulses = 1;
        pulses[0].splitRatio = 100.0f;
        pulses[0].fuelMs = baseFuelMs;
        pulses[0].isActive = true;
        return updateInjectionAngle();
    }

    // Всё ок!
    return true;
}


/**
 * Validates and adjusts multi-injection windows to ensure minimum dwell between pulses
 * If dwell requirement is not met, adjusts pulse timing based on injection timing mode
 * 
 * Dwell = angle between end of Pulse 0 and start of Pulse 1
 * 
 * Adjustment strategies:
 * - END/CENTER mode: shift both pulses earlier (more negative) to maintain timing reference
 * - START mode: shift Pulse 1 later (toward TDC) to increase dwell, respecting 15° TDC limit
 * 
 * @returns true if windows valid after adjustment, false if cannot be fixed
 */
bool InjectionEvent::validateInjectionWindows() {
  if (getNumberOfPulses() < 2) {
    return true;
  }
  
  // Get minimum dwell angle from table for this cylinder
  float minDwell = interpolate3d(
    engineConfiguration->minDwellAngleTable,
    engineConfiguration->multiInjectionLoadBins,
    getFuelingLoad(),
    engineConfiguration->multiInjectionRpmBins,
    Sensor::getOrZero(SensorType::Rpm)
  );
  
  // Clamp to [5°, 50°]
  minDwell = std::clamp(minDwell, 5.0f, 50.0f);
  
  // Check dwell between Pulse 0 and Pulse 1
  float pulse0End = getPulse(0).startAngle + getPulse(0).durationAngle;
  pulse0End = normalizeAngle(pulse0End);
  
  float pulse1Start = getPulse(1).startAngle;
  
  // Calculate dwell (angle between end of Pulse 0 and start of Pulse 1)
  float dwell = pulse1Start - pulse0End;
  if (dwell < 0) dwell += 720.0f;
  
  // Check if dwell is sufficient
  if (dwell >= minDwell) {
    return true;  // All windows valid
  }
  
  // Dwell is insufficient — need to adjust pulse timing
  auto mode = engineConfiguration->injectionTimingMode;
  
  if (mode == InjectionTimingMode::Center || mode == InjectionTimingMode::End) {
    // For CENTER/END modes: shift both pulses earlier to maintain timing reference
    // and increase dwell. Pulse 0 cannot be shifted back more than 360°.
    float dwellShortfall = minDwell - dwell;
    
    // Attempt to move Pulse 0 earlier (more negative angle)
    pulses[0].startAngle -= dwellShortfall;
    
    // Ensure Pulse 0 doesn't get shifted back more than 360°
    float shift = fabs(dwellShortfall);
    if (shift > 360.0f) {
      return false;  // Cannot accommodate dwell requirement
    }
    
    // Pulse 1 stays at its calculated angle (dwell will now be satisfied)
    return true;
  } else {
    // START mode: shift Pulse 1 later (toward TDC, more positive) to increase dwell
    // but don't go closer than 15° to TDC (angle 360° or -720° + 15° = -705° normalized to ~15°)
    float dwellShortfall = minDwell - dwell;
    
    // Calculate new Pulse 1 start angle (shift toward TDC)
    float newPulse1Start = pulse1Start + dwellShortfall;
    newPulse1Start = normalizeAngle(newPulse1Start);
    
    // Check if new angle is within 15° of TDC
    // In normalized coords (0-720°), TDC is at 0° or 720°. 
    // Within 15° of TDC means angle is in [0°, 15°] or [705°, 720°]
    if ((newPulse1Start > 0 && newPulse1Start < 15.0f) || 
        (newPulse1Start > 705.0f && newPulse1Start < 720.0f)) {
      return false;  // Cannot move Pulse 1 that close to TDC
    }
    
    // Apply the shift
    pulses[1].startAngle = newPulse1Start;
    return true;
  }
}

#endif // EFI_ENGINE_CONTROL
