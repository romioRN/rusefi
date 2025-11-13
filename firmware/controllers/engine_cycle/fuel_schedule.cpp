/**
 * @file fuel_schedule.cpp
 *
 * Handles injection scheduling
 */

#include "pch.h"

#if EFI_ENGINE_CONTROL

void turnInjectionPinHigh(scheduler_arg_t const arg) {
  auto const nowNt{ getTimeNowNt() };

  auto const taggedPointer{ TaggedPointer<InjectionEvent>::fromRaw(arg) };
  auto const event{ taggedPointer.getOriginalPointer() };
  auto const hasStage2Injection{ taggedPointer.getFlag() };

  for (auto const& output: event->outputs) {
    if (output) {
      output->open(nowNt);
    }
  }

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

// Determines how much to adjust injection opening angle based on the injection's duration and the current phasing mode
static float getInjectionAngleCorrection(float fuelMs, float oneDegreeUs) {
  auto mode = engineConfiguration->injectionTimingMode;
  if (mode == InjectionTimingMode::Start) {
    // Start of injection gets no correction for duration
    return 0;
  }

  efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !std::isnan(fuelMs), "NaN fuelMs", false);

  angle_t injectionDurationAngle = MS2US(fuelMs) / oneDegreeUs;
  efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !std::isnan(injectionDurationAngle), "NaN injectionDurationAngle", false);
  assertAngleRange(injectionDurationAngle, "injectionDuration_r", ObdCode::CUSTOM_INJ_DURATION);

  if (mode == InjectionTimingMode::Center) {
    // Center of injection is half-corrected for duration
    return injectionDurationAngle * 0.5f;
  } else {
      // End of injection gets "full correction" so we advance opening by the full duration
      return injectionDurationAngle;
  }
}

// Returns the start angle of this injector in engine coordinates (0-720 for a 4 stroke),
// or unexpected if unable to calculate the start angle due to missing information.
expected<float> InjectionEvent::computeInjectionAngle() const {
  floatus_t oneDegreeUs = getEngineRotationState()->getOneDegreeUs(); // local copy
  if (std::isnan(oneDegreeUs)) {
    // in order to have fuel schedule we need to have current RPM
    // wonder if this line slows engine startup?
    return unexpected;
  }

  float fuelMs = getEngineState()->injectionDuration;
  if (std::isnan(fuelMs)) {
    return unexpected;
  }

  // injection phase may be scheduled by injection end, so we need to step the angle back
  // for the duration of the injection
  angle_t injectionDurationAngle = getInjectionAngleCorrection(fuelMs, oneDegreeUs);

  // User configured offset - degrees after TDC combustion
  floatus_t injectionOffset = getEngineState()->injectionOffset;
  if (std::isnan(injectionOffset)) {
    // injection offset map not ready - we are not ready to schedule fuel events
    return unexpected;
  }

  angle_t openingAngle = injectionOffset - injectionDurationAngle;
  assertAngleRange(openingAngle, "openingAngle_r", ObdCode::CUSTOM_ERR_6554);
  wrapAngle(openingAngle, "addFuel#1", ObdCode::CUSTOM_ERR_6555);
  // TODO: should we log per-cylinder injection timing? #76
  getTunerStudioOutputChannels()->injectionOffset = openingAngle;

  // Convert from cylinder-relative to cylinder-1-relative
  openingAngle += getPerCylinderFiringOrderOffset(ownIndex, cylinderNumber);

  efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !std::isnan(openingAngle), "findAngle#3", false);
  assertAngleRange(openingAngle, "findAngle#a33", ObdCode::CUSTOM_ERR_6544);

  wrapAngle(openingAngle, "addFuel#2", ObdCode::CUSTOM_ERR_6555);

#if EFI_UNIT_TEST
//  printf("registerInjectionEvent openingAngle=%.2f inj %d\r\n", openingAngle, cylinderNumber);
#endif

  return openingAngle;
}

bool InjectionEvent::updateInjectionAngle() {
  auto result = computeInjectionAngle();

  if (result) {
    // If injector duty cycle is high, lock injection SOI so that we
    // don't miss injections at or above 100% duty
    if (getEngineState()->shouldUpdateInjectionTiming) {
      injectionStartAngle = result.Value;
    }

    return true;
  } else {
    return false;
  }
}

/**
 * @returns false in case of error, true if success
 */
bool InjectionEvent::update() {
  // ========== NEW: Multi-injection support ==========
  bool updatedAngle;
  
  if (engineConfiguration->multiInjection.enableMultiInjection && numberOfPulses > 1) {
    // Use multi-injection angle calculation
    updatedAngle = updateMultiInjectionAngles();
  } else {
    // Standard single injection
    updatedAngle = updateInjectionAngle();
  }
  // ===================================================

  if (!updatedAngle) {
    return false;
  }

  injection_mode_e mode = getCurrentInjectionMode();
  engine->outputChannels.currentInjectionMode = static_cast<uint8_t>(mode);

  int injectorIndex;
  if (mode == IM_SIMULTANEOUS || mode == IM_SINGLE_POINT) {
    // These modes only have one injector
    injectorIndex = 0;
  } else if (mode == IM_SEQUENTIAL || mode == IM_BATCH) {
    // Map order index -> cylinder index (firing order)
    injectorIndex = getCylinderNumberAtIndex(ownIndex);
  } else {
    firmwareError(ObdCode::CUSTOM_OBD_UNEXPECTED_INJECTION_MODE, "Unexpected injection mode %d", mode);
    injectorIndex = 0;
  }

  InjectorOutputPin *secondOutput;
  InjectorOutputPin* secondOutputStage2;

  if (mode == IM_BATCH) {
    /**
     * also fire the 2nd half of the injectors so that we can implement a batch mode on individual wires
     */
    // Compute the position of this cylinder's twin in the firing order
    // Each injector gets fired as a primary (the same as sequential), but also
    // fires the injector 360 degrees later in the firing order.
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
  // Stash the cylinder number so we can select the correct fueling bank later
  cylinderNumber = injectorIndex;

  outputsStage2[0] = &enginePins.injectorsStage2[injectorIndex];
  outputsStage2[1] = secondOutputStage2;

  if (!isSimultaneous && !output->isInitialized()) {
    // todo: extract method for this index math
    warning(ObdCode::CUSTOM_OBD_INJECTION_NO_PIN_ASSIGNED, "no_pin_inj #%s", output->getName());
  }

  return true;
}

void FuelSchedule::addFuelEvents() {
  // ========== NEW: Configure multi-injection ==========
  configureMultiInjectionForAllCylinders();
  // ====================================================
  
  for (size_t cylinderIndex = 0; cylinderIndex < engineConfiguration->cylindersCount; cylinderIndex++) {
    bool result = elements[cylinderIndex].update();

    if (!result) {
      invalidate();
      return;
    }
  }

  // We made it through all cylinders, mark the schedule as ready so it can be used
  isReady = true;
}

void FuelSchedule::onTriggerTooth(efitick_t nowNt, float currentPhase, float nextPhase) {
  // Wait for schedule to be built - this happens the first time we get RPM
  if (!isReady) {
    return;
  }

  for (size_t i = 0; i < engineConfiguration->cylindersCount; i++) {
    auto& event = elements[i];
    
    // ========== Multi-injection support ==========
    if (engineConfiguration->multiInjection.enableMultiInjection && event.getNumberOfPulses() > 1) {
      // Schedule all active pulses for multi-injection
      for (uint8_t pulseIdx = 0; pulseIdx < event.getNumberOfPulses(); pulseIdx++) {
        const auto& pulse = event.getPulse(pulseIdx);
        
        if (!pulse.isActive) continue;
        
        float pulseAngle = pulse.startAngle;
        
        // Check if pulse falls within current trigger window
        bool inWindow = false;
        if (nextPhase > currentPhase) {
          // Normal case
          inWindow = (pulseAngle >= currentPhase && pulseAngle < nextPhase);
        } else {
          // Wrap around 720°
          inWindow = (pulseAngle >= currentPhase || pulseAngle < nextPhase);
        }
        
        if (inWindow) {
          event.schedulePulse(pulseIdx, nowNt, currentPhase);
        }
      }
    } else {
      // ============================================
      // Standard single injection
      event.onTriggerTooth(nowNt, currentPhase, nextPhase);
    }
  }
}


// ========== NEW: Multi-injection implementation ==========

void FuelSchedule::configureMultiInjectionForAllCylinders() {
  if (!engineConfiguration->multiInjection.enableMultiInjection) {
    // Disabled, ensure single injection for all cylinders
    for (size_t i = 0; i < engineConfiguration->cylindersCount; i++) {
      elements[i].configureMultiInjection(1);
    }
    return;
  }
  
  uint8_t numPulses = engineConfiguration->multiInjection.numberOfInjections;

  efiPrintf("Configuring multi-injection: %d pulses", numPulses);
  
  // Clamp to valid range
  if (numPulses < 1) numPulses = 1;
  if (numPulses > MAX_INJECTION_PULSES) numPulses = MAX_INJECTION_PULSES;
  
  // Configure all cylinder injection events
  for (size_t i = 0; i < engineConfiguration->cylindersCount; i++) {
    elements[i].configureMultiInjection(numPulses);
  }
}

bool FuelSchedule::shouldUseMultiInjection() const {
  if (!engineConfiguration->multiInjection.enableMultiInjection) {
    return false;
  }
  
  // Check engine conditions (optional - can add more logic here)
  float rpm = Sensor::getOrZero(SensorType::Rpm);
  float load = getFuelingLoad();
  
  // Example: only use multi-injection above certain conditions
  // This can be made configurable via threshold parameters
  return (load > 100.0f && rpm > 1000.0f);
}

void InjectionEvent::schedulePulse(uint8_t pulseIndex, efitick_t nowNt, float currentPhase) {
  if (pulseIndex >= numberOfPulses || !pulses[pulseIndex].isActive) return;
  
  const auto& pulse = pulses[pulseIndex];
  floatus_t oneDegreeUs = getEngineRotationState()->getOneDegreeUs();
  if (std::isnan(oneDegreeUs) || oneDegreeUs < 0.1f) return;
  
  float angleDelta = pulse.startAngle - currentPhase;
  if (angleDelta < 0) angleDelta += 720;
  
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
  printf("DEBUG: SCH pulse#%d at %u time: %.2fms\n", pulseIndex, (unsigned)nowNt, pulse.fuelMs);
}


// ==========================================================

#endif // EFI_ENGINE_CONTROL
