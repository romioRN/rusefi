
/*
 * injection_gpio.cpp
 */

#include "pch.h" 
#include "injection_gpio.h"

extern bool printFuelDebug;

// Debug counters for diagnostics
static uint32_t g_multiInjectionCount __attribute__((used)) = 0;
static uint32_t g_singleInjectionCount __attribute__((used)) = 0;

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
  m_multiInjectEndNt = 0;
}

// Static callback wrapper for scheduled close event
void InjectorOutputPin::multiInjectCloseCallback(InjectorOutputPin* pin) {
  if (pin) {
    pin->close(getTimeNowNt());
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

// Multi-injection with custom duration (uses high-precision nanosecond scheduler)
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

  // Calculate scheduled close time in nanoseconds (nanosecond precision)
  efitick_t durationNt = US2NT((int32_t)durationUs);
  efitick_t desiredCloseNt = nowNt + durationNt;

  if (newCount > 1) {
#if FUEL_MATH_EXTREME_LOGGING
    if (printFuelDebug) {
      printf("overlapping (multi) — considering extend\r\n");
    }
#endif
    // Already high — extend scheduled end if this request lasts longer
    efitick_t prevEndNt = __atomic_load_n(&m_multiInjectEndNt, __ATOMIC_SEQ_CST);
    if (desiredCloseNt > prevEndNt) {
      // Cancel previous scheduled close and reschedule with new time
      __atomic_store_n(&m_multiInjectEndNt, desiredCloseNt, __ATOMIC_SEQ_CST);
      getScheduler()->cancel(&m_multiInjectCloseScheduling);
      
      // Schedule new close event with extended duration
      action_s closeAction = action_s::make<multiInjectCloseCallback>(this);
      getScheduler()->schedule("multi_inj_close_extend", &m_multiInjectCloseScheduling, desiredCloseNt, closeAction);
    }
    return;
  }

#if EFI_TOOTH_LOGGER
  LogTriggerInjectorState(nowNt, injectorIndex, true);
#endif

  // First opener: raise pin and schedule close via high-precision scheduler
  setHigh();
  __atomic_store_n(&m_multiInjectEndNt, desiredCloseNt, __ATOMIC_SEQ_CST);
  
  // Schedule close event with nanosecond precision
  action_s closeAction = action_s::make<multiInjectCloseCallback>(this);
  getScheduler()->schedule("multi_inj_close", &m_multiInjectCloseScheduling, desiredCloseNt, closeAction);
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
    // No more opens: cancel any pending scheduler close event and reset end marker
    getScheduler()->cancel(&m_multiInjectCloseScheduling);
    __atomic_store_n(&m_multiInjectEndNt, (efitick_t)0, __ATOMIC_SEQ_CST);
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

 