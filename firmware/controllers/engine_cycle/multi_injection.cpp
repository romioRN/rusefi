/**
 * @file multi_injection.cpp
 * @brief Multi-injection (split injection) support for GDI engines
 *
 * @date Nov 12, 2025
 * @author rusEFI Team
 */

#include "pch.h"
#include "fuel_schedule.h"

#if EFI_ENGINE_CONTROL

// Multi-injection constants
static constexpr float MIN_DWELL_ANGLE = 10.0f;
static constexpr float ABORT_ANGLE_SAFETY = 30.0f;
static constexpr float MAX_INJECTION_DURATION = 180.0f;

void InjectionEvent::configureMultiInjection(uint8_t numPulses) {
  numberOfPulses = minI(maxI(numPulses, 1), MAX_INJECTION_PULSES);
  
  if (!engineConfiguration->multiInjection.enableMultiInjection) {
    numberOfPulses = 1;
    return;
  }
  
  pulses[0].splitRatio = engineConfiguration->multiInjection.splitRatio1;
  pulses[1].splitRatio = engineConfiguration->multiInjection.splitRatio2;
  pulses[2].splitRatio = engineConfiguration->multiInjection.splitRatio3;
  pulses[3].splitRatio = engineConfiguration->multiInjection.splitRatio4;
  pulses[4].splitRatio = engineConfiguration->multiInjection.splitRatio5;
  
  float totalRatio = 0;
  for (uint8_t i = 0; i < numberOfPulses; i++) {
    totalRatio += pulses[i].splitRatio;
  }
  
  if (totalRatio > 0.1f) {
    for (uint8_t i = 0; i < numberOfPulses; i++) {
      pulses[i].splitRatio = (pulses[i].splitRatio / totalRatio) * 100.0f;
    }
  } else {
    float equalRatio = 100.0f / numberOfPulses;
    for (uint8_t i = 0; i < numberOfPulses; i++) {
      pulses[i].splitRatio = equalRatio;
    }
  }
  
  for (uint8_t i = numberOfPulses; i < MAX_INJECTION_PULSES; i++) {
    pulses[i].isActive = false;
    pulses[i].splitRatio = 0;
  }
}



float InjectionEvent::computeSplitRatio(uint8_t pulseIndex) const {
  if (pulseIndex >= numberOfPulses) {
    efiPrintf("ERROR: computeSplitRatio: pulseIndex %d >= numberOfPulses %d", 
              pulseIndex, numberOfPulses);
    return 0.0f;
  }
  
  float baseRatio = pulses[pulseIndex].splitRatio;
  efiPrintf("computeSplitRatio(%d): baseRatio=%.1f", pulseIndex, baseRatio);
  
  if (engineConfiguration->multiInjection.enableLoadBasedSplit && pulseIndex == 0) {
    efiPrintf("WARNING: Using table-based split (not recommended for testing)");
    float rpm = Sensor::getOrZero(SensorType::Rpm);
    float load = getFuelingLoad();
    
    // Find load index - convert float load to uint16_t for comparison
    uint16_t loadValue = (uint16_t)load;
    int loadIdx = findIndexMsg("load", 
      engineConfiguration->multiInjectionLoadBins, 
      16, 
      loadValue);
    
    // Interpolate along RPM axis
    baseRatio = interpolate2d(
      rpm,
      engineConfiguration->multiInjectionRpmBins,
      engineConfiguration->multiInjectionSplitRatioTable[loadIdx]
    );
  }
  efiPrintf("computeSplitRatio(%d) returns: %.1f", pulseIndex, baseRatio);
  return baseRatio;
}

float InjectionEvent::computeSecondaryInjectionAngle(uint8_t pulseIndex) const {
  if (pulseIndex == 0) {
    return injectionStartAngle;
  }
  
  float baseAngle = 0;
  switch (pulseIndex) {
    case 1:
      baseAngle = engineConfiguration->multiInjection.injection2AngleOffset;
      efiPrintf("computeSecondaryInjectionAngle(1): injection2AngleOffset=%.1f", baseAngle);
      break;
    case 2:
      baseAngle = engineConfiguration->multiInjection.injection3AngleOffset;
      break;
    case 3:
      baseAngle = engineConfiguration->multiInjection.injection4AngleOffset;
      break;
    case 4:
      baseAngle = engineConfiguration->multiInjection.injection5AngleOffset;
      break;
    default:
      baseAngle = 100.0f;
      break;
  }
  
  if (pulseIndex == 1 && engineConfiguration->multiInjection.enableRpmAngleCorrection) {
    efiPrintf("WARNING: Using RPM-based angle correction (not recommended for testing)");
    float rpm = Sensor::getOrZero(SensorType::Rpm);
    float load = getFuelingLoad();
    
    // Convert float load to uint16_t for index search
    uint16_t loadValue = (uint16_t)load;
    int loadIdx = findIndexMsg("load", 
      engineConfiguration->multiInjectionLoadBins, 
      16, 
      loadValue);
    
    // Interpolate along RPM axis
    float tableAngle = interpolate2d(
      rpm,
      engineConfiguration->multiInjectionRpmBins,
      engineConfiguration->secondInjectionAngleTable[loadIdx]
    );
    
    baseAngle = tableAngle;
  }
  efiPrintf("computeSecondaryInjectionAngle(%d) returns: %.1f", pulseIndex, baseAngle);
  return baseAngle;
}

float InjectionEvent::calculateDwellTime(uint8_t pulseIndex) const {
  if (pulseIndex >= numberOfPulses - 1) {
    return 0;
  }
  
  float endOfCurrent = pulses[pulseIndex].startAngle - pulses[pulseIndex].durationAngle;
  float startOfNext = pulses[pulseIndex + 1].startAngle;
  
  return endOfCurrent - startOfNext;
}

bool InjectionEvent::validateInjectionWindows() const {
  float minDwell = engineConfiguration->multiInjection.dwellAngleBetweenInjections;
  if (minDwell < MIN_DWELL_ANGLE) {
    minDwell = MIN_DWELL_ANGLE;
  }
  
  for (uint8_t i = 0; i < numberOfPulses - 1; i++) {
    if (!pulses[i].isActive || !pulses[i + 1].isActive) {
      continue;
    }
    
    float dwell = calculateDwellTime(i);
    
    if (dwell < minDwell) {
      warning(ObdCode::CUSTOM_MULTI_INJECTION_OVERLAP,
          "Multi-injection overlap: pulse %d->%d (dwell %.1f < %.1f)", 
          i, i + 1, dwell, minDwell);
      return false;
    }
  }
  
  uint8_t lastPulseIdx = numberOfPulses - 1;
  if (pulses[lastPulseIdx].isActive) {
    float lastEnd = pulses[lastPulseIdx].startAngle - pulses[lastPulseIdx].durationAngle;
    
    float ignitionAngle = 15.0f;
    if (cylinderNumber < MAX_CYLINDER_COUNT) {
      ignitionAngle = getEngineState()->timingAdvance[cylinderNumber];
    }
    
    if (lastEnd < (ignitionAngle + ABORT_ANGLE_SAFETY)) {
      warning(ObdCode::CUSTOM_MULTI_INJECTION_TOO_LATE,
          "Multi-injection too late: pulse %d ends at %.1f°",
          lastPulseIdx, lastEnd);
      return false;
    }
  }
  
  return true;
}



bool InjectionEvent::updateMultiInjectionAngles() {
  if (!engineConfiguration->multiInjection.enableMultiInjection || numberOfPulses == 1) {
    return updateInjectionAngle();
  }
  
  // ✅ ИСПРАВЛЕНО: выводим реальное количество импульсов (numberOfPulses уже корректное)
  efiPrintf("Multi-injection active: %d pulses", numberOfPulses);

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
      warning(ObdCode::CUSTOM_MULTI_INJECTION_PULSE_TOO_LONG,
          "Multi-injection pulse %d too long: %.1f° > max %.1f°",
          i, pulseDurationAngle, MAX_INJECTION_DURATION);
      pulseDurationAngle = MAX_INJECTION_DURATION;
    }
    
    pulses[i].durationAngle = pulseDurationAngle;
    
    if (i == 0) {
      auto result = computeInjectionAngle();
      if (!result.Valid) {
        return false;
      }
      pulses[0].startAngle = result.Value;
      injectionStartAngle = result.Value;
    } else {
      pulses[i].startAngle = computeSecondaryInjectionAngle(i);
    }
    
    pulses[i].isActive = true;
  }
  
  if (!validateInjectionWindows()) {
    warning(ObdCode::CUSTOM_MULTI_INJECTION_INVALID_CONFIG,
        "Multi-injection validation failed, falling back to single injection");
    numberOfPulses = 1;
    pulses[0].splitRatio = 100.0f;
    pulses[0].fuelMs = baseFuelMs;
    return updateInjectionAngle();
  }
  
  return true;
}

#endif // EFI_ENGINE_CONTROL
