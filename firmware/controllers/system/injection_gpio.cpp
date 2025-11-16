
/*
 * injection_gpio.cpp
 */

#include "pch.h" 
#include "injection_gpio.h"

extern bool printFuelDebug;

// Debug counters for diagnostics
static uint32_t g_multiInjectionCount = 0;
static uint32_t g_singleInjectionCount = 0;

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
  overlappingCounter = 1;
  reset();
  injectorIndex = -1;
  chVTObjectInit(&m_multiInjectTimer);
}



// Timer callback for multi-injection
void InjectorOutputPin::timerCallback(virtual_timer_t *vtp, void *arg) {
  (void)vtp; // Unused
  InjectorOutputPin* output = static_cast<InjectorOutputPin*>(arg);
  if (output) {
    output->close(getTimeNowNt());
  }
}

// Standard single injection
void InjectorOutputPin::open(efitick_t nowNt) {
  overlappingCounter++;
  getEngineState()->fuelInjectionCounter++;
  g_singleInjectionCount++;

#if FUEL_MATH_EXTREME_LOGGING
  if (printFuelDebug) {
    printf("InjectorOutputPin::open %s %d now=%0.1fms\r\n", 
           getName(), overlappingCounter, time2print(getTimeNowUs()) / 1000.0);
  }
#endif

  if (overlappingCounter > 1) {
#if FUEL_MATH_EXTREME_LOGGING
    if (printFuelDebug) {
      printf("overlapping, no need to touch pin %s\r\n", getName());
    }
#endif
  } else {
#if EFI_TOOTH_LOGGER
    LogTriggerInjectorState(nowNt, injectorIndex, true);
#endif
    setHigh();
  }
}

// Multi-injection with custom duration
void InjectorOutputPin::open(efitick_t nowNt, floatus_t durationUs) {
  overlappingCounter++;
  getEngineState()->fuelInjectionCounter++;
  g_multiInjectionCount++;

#if FUEL_MATH_EXTREME_LOGGING
  if (printFuelDebug) {
    printf("InjectorOutputPin::open (multi) %s dur=%.2fms\r\n",
           getName(), durationUs / 1000.0);
  }
#endif

  if (overlappingCounter > 1) {
#if FUEL_MATH_EXTREME_LOGGING
    if (printFuelDebug) {
      printf("overlapping (multi)\r\n");
    }
#endif
    return;
  }

#if EFI_TOOTH_LOGGER
  LogTriggerInjectorState(nowNt, injectorIndex, true);
#endif
  
  setHigh();
  
  // Validate duration
  if (durationUs < 0.1f || durationUs > 100000.0f) {
    // Invalid duration, close immediately
    close(nowNt);
    return;
  }
  
  // Schedule closing using ChibiOS virtual timer
  sysinterval_t delayTicks = TIME_US2I(durationUs);
  
  // Cancel any pending timer
  chVTReset(&m_multiInjectTimer);
  
  // Set timer to fire callback after delay
  chVTSet(&m_multiInjectTimer, delayTicks, 
    InjectorOutputPin::timerCallback, this);
}

void InjectorOutputPin::close(efitick_t nowNt) {
#if FUEL_MATH_EXTREME_LOGGING
  if (printFuelDebug) {
    printf("InjectorOutputPin::close %s %d\r\n", 
           getName(), overlappingCounter);
  }
#endif

  overlappingCounter--;
  if (overlappingCounter > 0) {
#if FUEL_MATH_EXTREME_LOGGING
    if (printFuelDebug) {
      printf("was overlapping, no need to touch pin %s\r\n", getName());
    }
#endif
  } else {
#if EFI_TOOTH_LOGGER
    LogTriggerInjectorState(nowNt, injectorIndex, false);
#endif
    setLow();
  }

  if (overlappingCounter < 0) {
    overlappingCounter = 0;
  }
}

void InjectorOutputPin::setHigh() {
  NamedOutputPin::setHigh();
  TunerStudioOutputChannels *state = getTunerStudioOutputChannels();
  switch (injectorIndex) {
  case 0: state->injectorState1 = true; break;
  case 1: state->injectorState2 = true; break;
  case 2: state->injectorState3 = true; break;
  case 3: state->injectorState4 = true; break;
  case 4: state->injectorState5 = true; break;
  case 5: state->injectorState6 = true; break;
  }
}

void InjectorOutputPin::setLow() {
  NamedOutputPin::setLow();
  TunerStudioOutputChannels *state = getTunerStudioOutputChannels();
  switch (injectorIndex) {
  case 0: state->injectorState1 = false; break;
  case 1: state->injectorState2 = false; break;
  case 2: state->injectorState3 = false; break;
  case 3: state->injectorState4 = false; break;
  case 4: state->injectorState5 = false; break;
  case 5: state->injectorState6 = false; break;
  }
}

// Console command for diagnostics
void printMultiInjectionStats() {
  efiPrintf("=== MULTI-INJECTION STATS ===");
  efiPrintf("Multi-injection calls: %lu", (unsigned long)g_multiInjectionCount);
  efiPrintf("Single-injection calls: %lu", (unsigned long)g_singleInjectionCount);
  efiPrintf("============================");
}
