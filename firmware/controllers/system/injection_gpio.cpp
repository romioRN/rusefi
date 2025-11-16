
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
  // Start with no active opens
  overlappingCounter = 0;
  reset();
  injectorIndex = -1;
  m_multiInjectEndUs = 0;
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
#if FUEL_MATH_EXTREME_LOGGING
  if (printFuelDebug) {
    printf("InjectorOutputPin::open %s now=%0.1fms\r\n", getName(), time2print(getTimeNowUs()) / 1000.0);
  }
#endif

  // Atomically increment — safe from ISR or thread
  int16_t newCount = (int16_t)__atomic_add_fetch(&overlappingCounter, 1, __ATOMIC_SEQ_CST);
  getEngineState()->fuelInjectionCounter++;
  g_singleInjectionCount++;

  if (newCount > 1) {
#if FUEL_MATH_EXTREME_LOGGING
#if FUEL_MATH_EXTREME_LOGGING
    if (printFuelDebug) {
      int16_t cur = __atomic_load_n(&overlappingCounter, __ATOMIC_SEQ_CST);
      printf("overlapping, no need to touch pin %s count=%d\r\n", getName(), cur);
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
  // Atomically increment the overlapping counter so this is safe from ISR or thread
  int16_t newCount = (int16_t)__atomic_add_fetch(&overlappingCounter, 1, __ATOMIC_SEQ_CST);
  getEngineState()->fuelInjectionCounter++;
  g_multiInjectionCount++;

#if FUEL_MATH_EXTREME_LOGGING
  if (printFuelDebug) {
    printf("InjectorOutputPin::open (multi) %s dur=%.2fms\r\n",
           getName(), durationUs / 1000.0);
  }
#endif

  // Validate duration
  if (durationUs < 0.1f || durationUs > 100000.0f) {
    // Invalid duration — keep counters balanced and ignore
    (void)__atomic_sub_fetch(&overlappingCounter, 1, __ATOMIC_SEQ_CST);
    return;
  }

  // Convert nanoseconds to microseconds for current time comparison
  efitimeus_t nowUs = NT2US(nowNt);
  efitimeus_t desiredEndUs = nowUs + (efitimeus_t)durationUs;

  if (newCount > 1) {
#if FUEL_MATH_EXTREME_LOGGING
    if (printFuelDebug) {
      printf("overlapping (multi) — considering extend\r\n");
    }
#endif
    // Already high — extend scheduled end if this request lasts longer
    efitimeus_t prevEndUs = __atomic_load_n(&m_multiInjectEndUs, __ATOMIC_SEQ_CST);
    if (desiredEndUs > prevEndUs) {
      __atomic_store_n(&m_multiInjectEndUs, desiredEndUs, __ATOMIC_SEQ_CST);
      sysinterval_t delayTicks = TIME_US2I((floatus_t)(desiredEndUs - nowUs));
      chVTResetI(&m_multiInjectTimer);
      chVTSetI(&m_multiInjectTimer, delayTicks, InjectorOutputPin::timerCallback, this);
    }
    return;
  }

#if EFI_TOOTH_LOGGER
  LogTriggerInjectorState(nowNt, injectorIndex, true);
#endif

  // First opener: raise pin and schedule close
  setHigh();
  __atomic_store_n(&m_multiInjectEndUs, desiredEndUs, __ATOMIC_SEQ_CST);
  sysinterval_t delayTicks = TIME_US2I(durationUs);
  chVTResetI(&m_multiInjectTimer);
  chVTSetI(&m_multiInjectTimer, delayTicks, InjectorOutputPin::timerCallback, this);
}

void InjectorOutputPin::close(efitick_t nowNt) {
#if FUEL_MATH_EXTREME_LOGGING
  if (printFuelDebug) {
    int16_t cur = __atomic_load_n(&overlappingCounter, __ATOMIC_SEQ_CST);
    printf("InjectorOutputPin::close %s %d\r\n", getName(), cur);
  }
#endif

  // Atomically decrement the overlapping counter
  int16_t newCount = (int16_t)__atomic_sub_fetch(&overlappingCounter, 1, __ATOMIC_SEQ_CST);
  if (newCount > 0) {
#if FUEL_MATH_EXTREME_LOGGING
    if (printFuelDebug) {
      printf("was overlapping, no need to touch pin %s count=%d\r\n", getName(), newCount);
    }
#endif
  } else {
#if EFI_TOOTH_LOGGER
    LogTriggerInjectorState(nowNt, injectorIndex, false);
#endif
    setLow();
    // No more opens: cancel any pending timer and reset end marker
    chVTResetI(&m_multiInjectTimer);
    __atomic_store_n(&m_multiInjectEndUs, (efitimeus_t)0, __ATOMIC_SEQ_CST);
  }

  if (newCount < 0) {
    __atomic_store_n(&overlappingCounter, (int16_t)0, __ATOMIC_SEQ_CST);
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

 