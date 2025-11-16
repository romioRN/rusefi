#include "pch.h"
#include "fuel_schedule.h"

#define MAX_INJECTION_DURATION 120.0f

#if EFI_ENGINE_CONTROL

static float normalizeAngle(float angle) {
  while (angle < 0) angle += 720.0f;
  while (angle >= 720.0f) angle -= 720.0f;
  return angle;
}

float InjectionEvent::computeSplitRatio(uint8_t pulseIndex) const {
  if (pulseIndex >= 2) {
    return 0.0f;
  }
  
  float ratio = interpolate3d(
    engineConfiguration->multiInjectionSplitRatioTable,
    engineConfiguration->multiInjectionLoadBins,
    getFuelingLoad(),
    engineConfiguration->multiInjectionRpmBins,
    Sensor::getOrZero(SensorType::Rpm)
  );
  
  return (ratio > 0.1f && ratio < 100.0f) ? ratio : (pulseIndex == 0 ? 60.0f : 40.0f);
}

float InjectionEvent::computeSecondaryInjectionAngle(uint8_t pulseIndex) const {
  if (pulseIndex == 0) {
    auto result = computeInjectionAngle();
    return result ? result.Value : 0.0f;
  }
  
  if (pulseIndex == 1) {
    float baseAngle = interpolate3d(
      engineConfiguration->secondInjectionAngleTable,
      engineConfiguration->multiInjectionLoadBins,
      getFuelingLoad(),
      engineConfiguration->multiInjectionRpmBins,
      Sensor::getOrZero(SensorType::Rpm)
    );
    
    if (baseAngle < 5.0f || baseAngle > 720.0f) {
      baseAngle = 100.0f;
    }
    
    floatus_t oneDegreeUs = getEngineRotationState()->getOneDegreeUs();
    if (std::isnan(oneDegreeUs) || oneDegreeUs < 0.1f) {
      return normalizeAngle(baseAngle);
    }
    
    float durationAngle = MS2US(getNumberOfPulses() > pulseIndex ? getPulse(pulseIndex).fuelMs : 0) / oneDegreeUs;
    if (durationAngle > MAX_INJECTION_DURATION) {
      durationAngle = MAX_INJECTION_DURATION;
    }
    
    auto mode = engineConfiguration->injectionTimingMode;
    float correctedAngle = baseAngle;
    
    if (mode == InjectionTimingMode::Center) {
      correctedAngle += durationAngle * 0.5f;
    } else if (mode == InjectionTimingMode::End) {
      correctedAngle += durationAngle;
    }
    
    return normalizeAngle(correctedAngle);
  }
  
  return 0.0f;
}

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
  
  return validateInjectionWindows() ? true : updateInjectionAngle();
}

bool InjectionEvent::validateInjectionWindows() const {
  if (getNumberOfPulses() < 2) {
    return true;
  }
  
  for (uint8_t i = 0; i < getNumberOfPulses() - 1; i++) {
    float endAngle = getPulse(i).startAngle - getPulse(i).durationAngle;
    if (endAngle < 0) endAngle += 720.0f;
    
    float dwell = getPulse(i + 1).startAngle - endAngle;
    if (dwell < 0) dwell += 720.0f;
    
    if (dwell < engineConfiguration->multiInjection.dwellAngleBetweenInjections) {
      return false;
    }
  }
  
  return true;
}

#endif
