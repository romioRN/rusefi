/**
 * @file fuel_schedule.cpp
 * @brief Handles fuel injection scheduling and multi-injection support
 * 
 * This file manages:
 * - Single injection timing calculation
 * - Multi-injection pulse scheduling (Pulse 0 and Pulse 1)
 * - Injection angle correction based on timing mode (START/CENTER/END)
 * - Per-cylinder fuel event generation
 */

#include "pch.h"

#if EFI_ENGINE_CONTROL

/**
 * Callback for injection pin opening on single injection
 * Handles both primary and stage2 injectors
 */
void turnInjectionPinHigh(scheduler_arg_t const arg) {
  auto const nowNt{ getTimeNowNt() };
  auto const taggedPointer{ TaggedPointer<InjectionEvent>::fromRaw(arg) };
  auto const event{ taggedPointer.getOriginalPointer() };
  auto const hasStage2Injection{ taggedPointer.getFlag() };

  // Open primary injectors
  for (auto const& output: event->outputs) {
    if (output) {
      output->open(nowNt);
    }
  }

  // Open stage2 injectors if needed
  if (hasStage2Injection) {
    for (auto const& output: event->outputsStage2) {
      if (output) {
        output->open(nowNt);
      }
    }
  }
}

FuelSchedule::FuelSchedule() {
  for (int cylinderIndex = 0; cylinderIndex < MAX_CYLINDER_COUNT; cylinderIndex++) {
    elements[cylinderIndex].setIndex(cylinderIndex);
  }
}

WallFuel& InjectionEvent::getWallFuel() {
  return wallFuel;
}

void FuelSchedule::invalidate() {
  isReady = false;
}

void FuelSchedule::resetOverlapping() {
  for (auto& inj : enginePins.injectors) {
    inj.reset();
  }
}

/**
 * Calculates angle correction based on injection duration and timing mode
 * @param fuelMs Injection duration in milliseconds
 * @param oneDegreeUs Time for one degree of crankshaft rotation in microseconds
 * @return Angle correction in degrees (0 for START mode, half duration for CENTER, full for END)
 */
static float getInjectionAngleCorrection(float fuelMs, float oneDegreeUs) {
  auto mode = engineConfiguration->injectionTimingMode;
  if (mode == InjectionTimingMode::Start) {
    return 0;  // No correction for START mode
  }

  efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !std::isnan(fuelMs), "NaN fuelMs", false);

  angle_t injectionDurationAngle = MS2US(fuelMs) / oneDegreeUs;
  efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !std::isnan(injectionDurationAngle), "NaN injectionDurationAngle", false);
  assertAngleRange(injectionDurationAngle, "injectionDuration_r", ObdCode::CUSTOM_INJ_DURATION);

  if (mode == InjectionTimingMode::Center) {
    return injectionDurationAngle * 0.5f;  // Half correction for CENTER mode
  } else {
    return injectionDurationAngle;  // Full correction for END mode
  }
}

/**
 * Calculates the starting angle for single injection
 * Takes into account injection duration and timing mode
 * @return Expected<float> angle in engine coordinates (0-720°), or unexpected if calculation fails
 */
expected<float> InjectionEvent::computeInjectionAngle() const {
  floatus_t oneDegreeUs = getEngineRotationState()->getOneDegreeUs();
  if (std::isnan(oneDegreeUs)) {
    return unexpected;
  }

  float fuelMs = getEngineState()->injectionDuration;
  if (std::isnan(fuelMs)) {
    return unexpected;
  }

  angle_t injectionDurationAngle = getInjectionAngleCorrection(fuelMs, oneDegreeUs);

  floatus_t injectionOffset = getEngineState()->injectionOffset;
  if (std::isnan(injectionOffset)) {
    return unexpected;
  }

  angle_t openingAngle = injectionOffset - injectionDurationAngle;
  assertAngleRange(openingAngle, "openingAngle_r", ObdCode::CUSTOM_ERR_6554);
  wrapAngle(openingAngle, "addFuel#1", ObdCode::CUSTOM_ERR_6555);
  getTunerStudioOutputChannels()->injectionOffset = openingAngle;

  // Convert from cylinder-relative to cylinder-1-relative
  openingAngle += getPerCylinderFiringOrderOffset(ownIndex, cylinderNumber);

  efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !std::isnan(openingAngle), "findAngle#3", false);
  assertAngleRange(openingAngle, "findAngle#a33", ObdCode::CUSTOM_ERR_6544);
  wrapAngle(openingAngle, "addFuel#2", ObdCode::CUSTOM_ERR_6555);

  return openingAngle;
}

/**
 * Updates injection starting angle for single injection mode
 * Only updates if engine state allows it
 */
bool InjectionEvent::updateInjectionAngle() {
  auto result = computeInjectionAngle();

  if (result) {
    if (getEngineState()->shouldUpdateInjectionTiming) {
      injectionStartAngle = result.Value;
    }
    return true;
  } else {
    return false;
  }
}

/**
 * Main update function for injection events
 * Handles both single and multi-injection modes
 * @returns true if successful, false if error
 */
bool InjectionEvent::update() {
  // Choose between multi-injection and single injection based on configuration
  bool updatedAngle = engineConfiguration->multiInjection.enableMultiInjection && getNumberOfPulses() > 1
    ? updateMultiInjectionAngles()
    : updateInjectionAngle();

  if (!updatedAngle) {
    return false;
  }

  injection_mode_e mode = getCurrentInjectionMode();
  engine->outputChannels.currentInjectionMode = static_cast<uint8_t>(mode);

  // Determine which injector to use based on injection mode
  int injectorIndex;
  if (mode == IM_SIMULTANEOUS || mode == IM_SINGLE_POINT) {
    injectorIndex = 0;
  } else if (mode == IM_SEQUENTIAL || mode == IM_BATCH) {
    injectorIndex = getCylinderNumberAtIndex(ownIndex);
  } else {
    firmwareError(ObdCode::CUSTOM_OBD_UNEXPECTED_INJECTION_MODE, "Unexpected injection mode %d", mode);
    injectorIndex = 0;
  }

  InjectorOutputPin *secondOutput;
  InjectorOutputPin* secondOutputStage2;

  // For BATCH mode, also fire the twin injector 360° later
  if (mode == IM_BATCH) {
    int secondOrder = (ownIndex + (engineConfiguration->cylindersCount / 2)) % engineConfiguration->cylindersCount;
    int secondIndex = getCylinderNumberAtIndex(secondOrder);
    secondOutput = &enginePins.injectors[secondIndex];
    secondOutputStage2 = &enginePins.injectorsStage2[secondIndex];
  } else {
    secondOutput = nullptr;
    secondOutputStage2 = nullptr;
  }

  InjectorOutputPin *output = &enginePins.injectors[injectorIndex];

  outputs[0] = output;
  outputs[1] = secondOutput;
  isSimultaneous = mode == IM_SIMULTANEOUS;
  cylinderNumber = injectorIndex;

  outputsStage2[0] = &enginePins.injectorsStage2[injectorIndex];
  outputsStage2[1] = secondOutputStage2;

  if (!isSimultaneous && !output->isInitialized()) {
    warning(ObdCode::CUSTOM_OBD_INJECTION_NO_PIN_ASSIGNED, "no_pin_inj #%s", output->getName());
  }

  return true;
}

/**
 * Generates fuel events for all cylinders
 * Validates configuration and marks schedule as ready
 */
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

/**
 * Main injection scheduling function
 * Called on every trigger tooth to schedule upcoming injections
 * 
 * For multi-injection:
 * - Schedules Pulse 0 (main injection) at computed angle
 * - Schedules Pulse 1 (secondary injection) at secondary angle offset
 * 
 * For single injection:
 * - Uses standard onTriggerTooth callback
 */
void FuelSchedule::onTriggerTooth(efitick_t nowNt, float currentPhase, float nextPhase) {
  if (!isReady) {
    return;
  }

  for (size_t i = 0; i < engineConfiguration->cylindersCount; i++) {
    auto& event = elements[i];
    
    if (engineConfiguration->multiInjection.enableMultiInjection && 
        event.getNumberOfPulses() > 1) {
      
      // Multi-injection: schedule each pulse separately
      for (uint8_t pulseIdx = 0; pulseIdx < event.getNumberOfPulses(); pulseIdx++) {
        const auto& pulse = event.getPulse(pulseIdx);
        if (!pulse.isActive) continue;
        
        float pulseAngle = pulse.startAngle;
        
        // Check if pulse angle falls within current tooth window
        bool inWindow = (nextPhase > currentPhase)
          ? (pulseAngle >= currentPhase && pulseAngle < nextPhase)
          : (pulseAngle >= currentPhase || pulseAngle < nextPhase);
        
        if (inWindow) {
          event.schedulePulse(pulseIdx, nowNt, currentPhase);
        }
      }
    } else {
      // Single injection: use standard trigger tooth handling
      event.onTriggerTooth(nowNt, currentPhase, nextPhase);
    }
  }
}

/**
 * Schedules a single multi-injection pulse
 * Calculates precise timing and calls output->open() with duration
 * 
 * @param pulseIndex Index of pulse (0 = Pulse 0, 1 = Pulse 1)
 * @param nowNt Current engine time in nanoseconds
 * @param currentPhase Current crankshaft phase in degrees
 */
void InjectionEvent::schedulePulse(uint8_t pulseIndex, efitick_t nowNt, float currentPhase) {
  if (pulseIndex >= getNumberOfPulses() || !getPulse(pulseIndex).isActive) {
    return;
  }

  const auto& pulse = getPulse(pulseIndex);

  // Validate fuel amount
  if (pulse.fuelMs < 0.001f || pulse.fuelMs > 100.0f) {
    return;
  }

  floatus_t oneDegreeUs = getEngineRotationState()->getOneDegreeUs();
  if (std::isnan(oneDegreeUs) || oneDegreeUs < 0.1f) {
    return;
  }

  // Calculate angle delta from current phase to injection start angle
  float angleDelta = pulse.startAngle - currentPhase;
  if (angleDelta < 0) angleDelta += 720.0f;

  // Convert angle delta to nanoseconds
  efitick_t injectionStartNt = nowNt + US2NT(angleDelta * oneDegreeUs);
  floatus_t pulseDurationUs = MS2US(pulse.fuelMs);

  // Schedule pulse on all configured injectors (primary and stage2)
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
