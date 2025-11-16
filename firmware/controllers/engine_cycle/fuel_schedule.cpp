/**
 * @file fuel_schedule.cpp
 * Handles injection scheduling
 */

#include "pch.h"

#if EFI_ENGINE_CONTROL

// ... весь существующий код (turnInjectionPinHigh, FuelSchedule::FuelSchedule, и т.д.) ...
// ВСЕ СТАРЫЕ ФУНКЦИИ ОСТАВЬ КАК ЕСТЬ!

bool InjectionEvent::update() {
  // Multi-injection support
  bool updatedAngle = engineConfiguration->multiInjection.enableMultiInjection && getNumberOfPulses() > 1
    ? updateMultiInjectionAngles()
    : updateInjectionAngle();

  if (!updatedAngle) {
    return false;
  }

  // ... остальной код как был ...
  injection_mode_e mode = getCurrentInjectionMode();
  // ... ВСЕ ОСТАЛЬНЫЕ СТРОКИ ОСТАВЬ ...
}

void FuelSchedule::addFuelEvents() {
  for (size_t cylinderIndex = 0; cylinderIndex < engineConfiguration->cylindersCount; cylinderIndex++) {
    bool result = elements[cylinderIndex].update();
    if (!result) {
      invalidate();
      return;
    }
  }
  isReady = true;
}

void FuelSchedule::onTriggerTooth(efitick_t nowNt, float currentPhase, float nextPhase) {
  if (!isReady) {
    return;
  }

  for (size_t i = 0; i < engineConfiguration->cylindersCount; i++) {
    auto& event = elements[i];
    
    if (engineConfiguration->multiInjection.enableMultiInjection && 
        event.getNumberOfPulses() > 1) {
      
      for (uint8_t pulseIdx = 0; pulseIdx < event.getNumberOfPulses(); pulseIdx++) {
        const auto& pulse = event.getPulse(pulseIdx);
        if (!pulse.isActive) continue;
        
        float pulseAngle = pulse.startAngle;
        bool inWindow = (nextPhase > currentPhase)
          ? (pulseAngle >= currentPhase && pulseAngle < nextPhase)
          : (pulseAngle >= currentPhase || pulseAngle < nextPhase);
        
        if (inWindow) {
          event.schedulePulse(pulseIdx, nowNt, currentPhase);
        }
      }
    } else {
      event.onTriggerTooth(nowNt, currentPhase, nextPhase);
    }
  }
}

void InjectionEvent::schedulePulse(uint8_t pulseIndex, efitick_t nowNt, float currentPhase) {
  if (pulseIndex >= getNumberOfPulses() || !getPulse(pulseIndex).isActive) {
    return;
  }

  const auto& pulse = getPulse(pulseIndex);
  if (pulse.fuelMs < 0.001f || pulse.fuelMs > 100.0f) {
    return;
  }

  floatus_t oneDegreeUs = getEngineRotationState()->getOneDegreeUs();
  if (std::isnan(oneDegreeUs) || oneDegreeUs < 0.1f) {
    return;
  }

  float angleDelta = pulse.startAngle - currentPhase;
  if (angleDelta < 0) angleDelta += 720.0f;

  efitick_t injectionStartNt = nowNt + US2NT(angleDelta * oneDegreeUs);
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
