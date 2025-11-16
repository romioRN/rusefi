#include "pch.h"
#include "fuel_schedule.h"

// Multi-injection configuration
#define MAX_INJECTION_DURATION 120.0f  // Maximum injection duration in degrees
#define MIN_INJECTION_DWELL 15.0f      // Minimum dwell between pulses in degrees

#if EFI_ENGINE_CONTROL

// Нормализует угол в диапазон 0-720°
static float normalizeAngle(float angle) {
  while (angle < 0) {
    angle += 720.0f;
  }
  while (angle >= 720.0f) {
    angle -= 720.0f;
  }
  return angle;
}

// Вычисляет split ratio из таблицы
float InjectionEvent::computeSplitRatio(uint8_t pulseIndex) const {
  if (pulseIndex >= 2) {
    return 0.0f;
  }
  
  float rpm = Sensor::getOrZero(SensorType::Rpm);
  float load = getFuelingLoad();
  
  float ratio = interpolate3d(
    engineConfiguration->multiInjectionSplitRatioTable,
    engineConfiguration->multiInjectionLoadBins,
    load,
    engineConfiguration->multiInjectionRpmBins,
    rpm
  );
  
  if (ratio > 0.1f && ratio < 100.0f) {
    return ratio;
  }
  
  return (pulseIndex == 0) ? 60.0f : 40.0f;
}

// Вычисляет угол второго импульса используя 3D интерполяцию
// С учётом режима впрыска (START/CENTER/END)
float InjectionEvent::computeSecondaryInjectionAngle(uint8_t pulseIndex) const {
  if (pulseIndex == 0) {
    auto result = computeInjectionAngle();
    return result ? result.Value : 0.0f;
  }
  
  if (pulseIndex == 1) {
    float rpm = Sensor::getOrZero(SensorType::Rpm);
    float load = getFuelingLoad();
    
    // Получи базовый угол из таблицы
    float baseAngle = interpolate3d(
      engineConfiguration->secondInjectionAngleTable,
      engineConfiguration->multiInjectionLoadBins,
      load,
      engineConfiguration->multiInjectionRpmBins,
      rpm
    );
    
    // Валидация базового угла
    if (baseAngle < 5.0f || baseAngle > 720.0f) {
      baseAngle = 100.0f;
    }
    
    auto mode = engineConfiguration->injectionTimingMode;
    
    // Вычисли длительность Pulse 1 в градусах
    floatus_t oneDegreeUs = getEngineRotationState()->getOneDegreeUs();
    if (std::isnan(oneDegreeUs) || oneDegreeUs < 0.1f) {
      return normalizeAngle(baseAngle);
    }
    
    float fuelMs = getNumberOfPulses() > pulseIndex ? getPulse(pulseIndex).fuelMs : 0;
    float durationAngle = MS2US(fuelMs) / oneDegreeUs;
    
    if (durationAngle > MAX_INJECTION_DURATION) {
      durationAngle = MAX_INJECTION_DURATION;
    }
    
    // Примени режим впрыска
    float correctedAngle = baseAngle;
    
    if (mode == InjectionTimingMode::Center) {
      correctedAngle = baseAngle + (durationAngle * 0.5f);
    } else if (mode == InjectionTimingMode::End) {
      correctedAngle = baseAngle + durationAngle;
    }
    
    return normalizeAngle(correctedAngle);
  }
  
  return 0.0f;
}

// Обновляет параметры мультиинъекции
bool InjectionEvent::updateMultiInjectionAngles() {
  if (!engineConfiguration->multiInjection.enableMultiInjection || getNumberOfPulses() == 1) {
    return updateInjectionAngle();
  }
  
  floatms_t baseFuelMs = getEngineState()->injectionDuration;
  if (std::isnan(baseFuelMs) || baseFuelMs <= 0) {
    return false;
  }
  
  float rpm = Sensor::getOrZero(SensorType::Rpm);
  if (rpm < 1) {
    return false;
  }
  
  floatus_t oneDegreeUs = getEngineRotationState()->getOneDegreeUs();
  if (std::isnan(oneDegreeUs) || oneDegreeUs < 0.1f) {
    return false;
  }
  
  for (uint8_t i = 0; i < getNumberOfPulses(); i++) {
    float ratio = computeSplitRatio(i);
    floatms_t pulseFuelMs = baseFuelMs * (ratio / 100.0f);
    
    // Используй setter если есть, иначе прямой доступ через getPulse
    float pulseDurationAngle = MS2US(pulseFuelMs) / oneDegreeUs;
    if (pulseDurationAngle > MAX_INJECTION_DURATION) {
      pulseDurationAngle = MAX_INJECTION_DURATION;
    }
  }
  
  if (!validateInjectionWindows()) {
    return updateInjectionAngle();
  }
  
  return true;
}

// Валидация окон впрыска
bool InjectionEvent::validateInjectionWindows() const {
  if (getNumberOfPulses() < 2) {
    return true;
  }
  
  for (uint8_t i = 0; i < getNumberOfPulses() - 1; i++) {
    const auto& pulse = getPulse(i);
    float endAngle = pulse.startAngle - pulse.durationAngle;
    if (endAngle < 0) endAngle += 720.0f;
    
    const auto& nextPulse = getPulse(i + 1);
    float startAngle = nextPulse.startAngle;
    
    float dwell = startAngle - endAngle;
    if (dwell < 0) dwell += 720.0f;
    
    if (dwell < engineConfiguration->multiInjection.dwellAngleBetweenInjections) {
      return false;
    }
  }
  
  return true;
}

#endif // EFI_ENGINE_CONTROL

