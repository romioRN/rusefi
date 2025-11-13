
/*
 * injection_gpio.cpp
 */

#include "pch.h"

extern bool printFuelDebug;

void startSimultaneousInjection() {
  efitick_t nowNt = getTimeNowNt();
  for (size_t i = 0; i < engineConfiguration->cylindersCount; i++) {
    enginePins.injectors[i].open(nowNt);
  }
}

void endSimultaneousInjectionOnlyTogglePins() {
  efitick_t nowNt = getTimeNowNt();
  for (size_t i = 0; i < engineConfiguration->cylindersCount; i++) {
    enginePins.injectors[i].close(nowNt);
  }
}

InjectorOutputPin::InjectorOutputPin() : NamedOutputPin() {
  overlappingCounter = 1; // Force update in reset
  reset();
  injectorIndex = -1;
  chVTObjectInit(&m_multiInjectTimer);  // ← NEW: Initialize timer for multi-injection
}

// ========== NEW: Timer callback for multi-injection closing ==========
void InjectorOutputPin::timerCallback(virtual_timer_t *vtp, void *arg) {
  InjectorOutputPin* output = static_cast<InjectorOutputPin*>(arg);
  if (output) {
    output->close(getTimeNowNt());
  }
}

// =======================================================================

void InjectorOutputPin::open(efitick_t nowNt) {
  printf("DEBUG: single-open %s at %u us\n", getName(), (unsigned)nowNt);
  // per-output counter for error detection
  overlappingCounter++;
  // global counter for logging
  getEngineState()->fuelInjectionCounter++;

#if FUEL_MATH_EXTREME_LOGGING
  if (printFuelDebug) {
    printf("InjectorOutputPin::open %s %d now=%0.1fms\r\n", getName(), overlappingCounter, time2print(getTimeNowUs()) / 1000.0);
  }
#endif /* FUEL_MATH_EXTREME_LOGGING */

  if (overlappingCounter > 1) {
//    /**
//     * #299
//     * this is another kind of overlap which happens in case of a small duty cycle after a large duty cycle
//     */
#if FUEL_MATH_EXTREME_LOGGING
    if (printFuelDebug) {
      printf("overlapping, no need to touch pin %s %d\r\n", getName(), time2print(getTimeNowUs()));
    }
#endif /* FUEL_MATH_EXTREME_LOGGING */
  } else {
#if EFI_TOOTH_LOGGER
    LogTriggerInjectorState(nowNt, injectorIndex, true);
#endif // EFI_TOOTH_LOGGER
    setHigh();
  }
}

// ========== NEW: Overload for multi-injection with custom duration ==========
void InjectorOutputPin::open(efitick_t nowNt, floatus_t durationUs) {
  printf("DEBUG: multi-open %s at %u us for %.2f ms\n", getName(), (unsigned)nowNt, durationUs/1000.0);
  overlappingCounter++;
  getEngineState()->fuelInjectionCounter++;

#if FUEL_MATH_EXTREME_LOGGING
  if (printFuelDebug) {
    printf("InjectorOutputPin::open (multi-inj) %s %d dur=%.2fms\r\n",
           getName(), overlappingCounter, durationUs / 1000.0);
  }
#endif

  if (overlappingCounter > 1) {
#if FUEL_MATH_EXTREME_LOGGING
    if (printFuelDebug) {
      printf("overlapping (multi-inj)\r\n");
    }
#endif
    return;
  }

#if EFI_TOOTH_LOGGER
  LogTriggerInjectorState(nowNt, injectorIndex, true);
#endif
  
  setHigh();
  
  // Schedule closing using ChibiOS virtual timer
  sysinterval_t delayTicks = TIME_US2I(durationUs);
  
  // Cancel any pending timer
  chVTReset(&m_multiInjectTimer);
  
  // Set timer to fire callback after delay
  chVTSet(&m_multiInjectTimer, delayTicks, 
    InjectorOutputPin::timerCallback, this);
}
// ===========================================================================

void InjectorOutputPin::close(efitick_t nowNt) {
#if FUEL_MATH_EXTREME_LOGGING
  if (printFuelDebug) {
    printf("InjectorOutputPin::close %s %d %d\r\n", getName(), overlappingCounter, time2print(getTimeNowUs()));
  }
#endif /* FUEL_MATH_EXTREME_LOGGING */

  overlappingCounter--;
  if (overlappingCounter > 0) {
#if FUEL_MATH_EXTREME_LOGGING
    if (printFuelDebug) {
      printf("was overlapping, no need to touch pin %s %d\r\n", getName(), time2print(getTimeNowUs()));
    }
#endif /* FUEL_MATH_EXTREME_LOGGING */
  } else {
#if EFI_TOOTH_LOGGER
  LogTriggerInjectorState(nowNt, injectorIndex, false);
#endif // EFI_TOOTH_LOGGER
    setLow();
  }

  // Don't allow negative overlap count
  if (overlappingCounter < 0) {
    overlappingCounter = 0;
  }
}

void InjectorOutputPin::setHigh() {
    NamedOutputPin::setHigh();
    TunerStudioOutputChannels *state = getTunerStudioOutputChannels();
  // this is NASTY but what's the better option? bytes? At cost of 22 extra bytes in output status packet?
  switch (injectorIndex) {
  case 0:
    state->injectorState1 = true;
    break;
  case 1:
    state->injectorState2 = true;
    break;
  case 2:
    state->injectorState3 = true;
    break;
  case 3:
    state->injectorState4 = true;
    break;
  case 4:
    state->injectorState5 = true;
    break;
  case 5:
    state->injectorState6 = true;
    break;
  }
}

void InjectorOutputPin::setLow() {
    NamedOutputPin::setLow();
    TunerStudioOutputChannels *state = getTunerStudioOutputChannels();
  // this is NASTY but what's the better option? bytes? At cost of 22 extra bytes in output status packet?
  switch (injectorIndex) {
  case 0:
    state->injectorState1 = false;
    break;
  case 1:
    state->injectorState2 = false;
    break;
  case 2:
    state->injectorState3 = false;
    break;
  case 3:
    state->injectorState4 = false;
    break;
  case 4:
    state->injectorState5 = false;
    break;
  case 5:
    state->injectorState6 = false;
    break;
  }
}