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

// Конфигурирует все цилиндры для мультиинъекции
void FuelSchedule::configureMultiInjectionForAllCylinders() {
  if (!engineConfiguration->multiInjection.enableMultiInjection) {
    for (size_t i = 0; i < engineConfiguration->cylindersCount; i++) {
      elements[i].configureMultiInjection(1);
    }
    return;
  }
  
  uint8_t numberOfPulses = 2;
  
  for (size_t i = 0; i < engineConfiguration->cylindersCount; i++) {
    elements[i].configureMultiInjection(numberOfPulses);
  }
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
    return computeInjectionAngle().Value;
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
    
    // ═══════════════════════════════════════════════════════
    // ПРИМЕНИ ТОТ ЖЕ РЕЖИМ КАК PULSE 0
    // ═══════════════════════════════════════════════════════
    
    auto mode = engineConfiguration->injectionTimingMode;
    
    // Вычисли длительность Pulse 1 в градусах
    floatus_t oneDegreeUs = getEngineRotationState()->getOneDegreeUs();
    if (std::isnan(oneDegreeUs) || oneDegreeUs < 0.1f) {
      return normalizeAngle(baseAngle);
    }
    
    float fuelMs = pulses[1].fuelMs;
    float durationAngle = MS2US(fuelMs) / oneDegreeUs;
    
    if (durationAngle > MAX_INJECTION_DURATION) {
      durationAngle = MAX_INJECTION_DURATION;
    }
    
    // Примени режим впрыска
    float correctedAngle = baseAngle;
    
    if (mode == InjectionTimingMode::Start) {
      correctedAngle = baseAngle;
      
    } else if (mode == InjectionTimingMode::Center) {
      correctedAngle = baseAngle + (durationAngle * 0.5f);
      
    } else if (mode == InjectionTimingMode::End) {
      correctedAngle = baseAngle + durationAngle;
    }
    
#if FUEL_MATH_EXTREME_LOGGING
    static uint32_t diagCounter = 0;
    if ((diagCounter % 100) == 0) {
      efiPrintf("Pulse 1: base=%.1f°, dur=%.1f°, mode=%d, final=%.1f°",
                baseAngle, durationAngle, (int)mode, correctedAngle);
    }
    diagCounter++;
#endif
    
    return normalizeAngle(correctedAngle);
  }
  
  return 0.0f;
}

// Обновляет параметры мультиинъекции
bool InjectionEvent::updateMultiInjectionAngles() {
  if (!engineConfiguration->multiInjection.enableMultiInjection || numberOfPulses == 1) {
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
  
  for (uint8_t i = 0; i < numberOfPulses; i++) {
    float ratio = computeSplitRatio(i);
    floatms_t pulseFuelMs = baseFuelMs * (ratio / 100.0f);
    pulses[i].fuelMs = pulseFuelMs;
    pulses[i].splitRatio = ratio;
    
    float pulseDurationAngle = MS2US(pulseFuelMs) / oneDegreeUs;
    if (pulseDurationAngle > MAX_INJECTION_DURATION) {
      pulseDurationAngle = MAX_INJECTION_DURATION;
    }
    pulses[i].durationAngle = pulseDurationAngle;
    
    pulses[i].startAngle = computeSecondaryInjectionAngle(i);
    pulses[i].isActive = true;
  }
  
  if (!validateInjectionWindows()) {
    numberOfPulses = 1;
    pulses[0].splitRatio = 100.0f;
    pulses[0].fuelMs = baseFuelMs;
    return updateInjectionAngle();
  }
  
  return true;
}

// Валидация окон впрыска
bool InjectionEvent::validateInjectionWindows() const {
  if (numberOfPulses < 2) {
    return true;
  }
  
  for (uint8_t i = 0; i < numberOfPulses - 1; i++) {
    float endAngle = pulses[i].startAngle - pulses[i].durationAngle;
    if (endAngle < 0) endAngle += 720.0f;
    
    float startAngle = pulses[i + 1].startAngle;
    
    float dwell = startAngle - endAngle;
    if (dwell < 0) dwell += 720.0f;
    
    if (dwell < engineConfiguration->multiInjection.dwellAngleBetweenInjections) {
      warning(ObdCode::CUSTOM_OBD_INJECTION, "Multi-injection overlap");
      return false;
    }
  }
  
  return true;
}

// Планирует впрыск каждого импульса
void InjectionEvent::schedulePulse(uint8_t pulseIndex, efitick_t nowNt, float currentPhase) {
  if (pulseIndex >= numberOfPulses || !pulses[pulseIndex].isActive) {
    return;
  }
  
  const auto& pulse = pulses[pulseIndex];
  
  if (pulse.fuelMs < 0.001f || pulse.fuelMs > 100.0f) {
    return;
  }
  
  floatus_t oneDegreeUs = getEngineRotationState()->getOneDegreeUs();
  if (std::isnan(oneDegreeUs) || oneDegreeUs < 0.1f) {
    return;
  }
  
  float angleDelta = pulse.startAngle - currentPhase;
  if (angleDelta < 0) {
    angleDelta += 720.0f;
  }
  
  efitick_t delayNt = US2NT(angleDelta * oneDegreeUs);
  efitick_t injectionStartNt = nowNt + delayNt;
  
  floatus_t pulseDurationUs = MS2US(pulse.fuelMs);
  
  for (auto* output : outputs) {
    if (output && output->isInitialized()) {
      output->open(injectionStartNt, pulseDurationUs);
    }
  }
  
  for (auto* output : outputsStage2) {
    if (output && output->isInitialized()) {
      output->open(injectionStartNt, pulseDurationUs);
    }
  }
}

#endif // EFI_ENGINE_CONTROL
