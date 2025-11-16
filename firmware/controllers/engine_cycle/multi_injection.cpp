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
  
  // Return table value if valid, otherwise use default 60/40 split
  return (ratio > 0.1f && ratio < 100.0f) ? ratio : (pulseIndex == 0 ? 60.0f : 40.0f);
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
    float baseAngle = interpolate3d(
      engineConfiguration->secondInjectionAngleTable,      // 16×16 table
      engineConfiguration->multiInjectionLoadBins,         // Load axis
      getFuelingLoad(),                                    // Current load
      engineConfiguration->multiInjectionRpmBins,          // RPM axis
      Sensor::getOrZero(SensorType::Rpm)                   // Current RPM
    );
    
    // Validate base angle from table
    if (baseAngle < 5.0f || baseAngle > 720.0f) {
      baseAngle = 100.0f;  // Fallback to safe default (100° BTDC)
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
    auto mode = engineConfiguration->injectionTimingMode;
    float correctedAngle = baseAngle;
    
    if (mode == InjectionTimingMode::Center) {
      // Pulse 1 centered at table angle
      correctedAngle += durationAngle * 0.5f;
    } else if (mode == InjectionTimingMode::End) {
      // Pulse 1 ends at table angle
      correctedAngle += durationAngle;
    }
    // START mode: no correction (pulse starts at table angle)
    
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
 * Validates multi-injection windows to ensure minimum dwell between pulses
 * Dwell = time/angle between end of Pulse 0 and start of Pulse 1
 * 
 * Validates:
 * - Minimum dwell angle maintained (configurable)
 * - No pulse overlap
 * 
 * @returns true if all validation passed, false if overlap detected
 */
bool InjectionEvent::validateInjectionWindows() const {
  if (getNumberOfPulses() < 2) {
    return true;
  }
  
  // Check dwell between consecutive pulses
  for (uint8_t i = 0; i < getNumberOfPulses() - 1; i++) {
    // Calculate end angle of current pulse
    float endAngle = getPulse(i).startAngle - getPulse(i).durationAngle;
    if (endAngle < 0) endAngle += 720.0f;
    
    // Calculate start angle of next pulse
    float startAngle = getPulse(i + 1).startAngle;
    
    // Calculate dwell (angle between end of current pulse and start of next)
    float dwell = startAngle - endAngle;
    if (dwell < 0) dwell += 720.0f;
    
    // Validate minimum dwell
    if (dwell < engineConfiguration->multiInjection.dwellAngleBetweenInjections) {
      return false;  // Overlap detected
    }
  }
  
  return true;  // All windows valid
}

#endif // EFI_ENGINE_CONTROL
