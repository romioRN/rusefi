/*

@file injection_gpio.h
*/

#pragma once

#include "efi_output.h"

// Forward declaration для unit tests
#if defined(UNIT_TEST) || defined(EFI_UNIT_TEST)
typedef void* virtual_timer_t;
#endif

void startSimultaneousInjection();
void endSimultaneousInjectionOnlyTogglePins();

class InjectorOutputPin final : public NamedOutputPin {
public:
InjectorOutputPin();
void reset();

// Standard single injection
void open(efitick_t nowNt);

// Multi-injection with custom duration
void open(efitick_t nowNt, floatus_t durationUs);

void close(efitick_t nowNt);
void setHigh() override;
void setLow() override;

int8_t getOverlappingCounter() const { return overlappingCounter; }

int8_t injectorIndex;

private:
int8_t overlappingCounter;

// Timer for multi-injection custom duration
virtual_timer_t m_multiInjectTimer;

// Timer callback
static void timerCallback(virtual_timer_t *vtp, void *arg);
};