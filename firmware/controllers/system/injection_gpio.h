/*

@file injection_gpio.h
*/

#pragma once

#include "efi_output.h"
#include "scheduler.h"



void startSimultaneousInjection();
void endSimultaneousInjectionOnlyTogglePins();

class InjectorOutputPin final : public NamedOutputPin {
public:
InjectorOutputPin();
void reset();

// Standard single injection
void open(efitick_t nowNt);

// Multi-injection with custom duration (uses high-precision scheduler)
void open(efitick_t nowNt, floatus_t durationUs);

void close(efitick_t nowNt);
void setHigh() override;
void setLow() override;

int16_t getOverlappingCounter() const { return overlappingCounter; }

int8_t injectorIndex;

private:
// Use a wider integer for atomic operations to avoid ABI/atomic-size issues
int16_t overlappingCounter;

// Storage for scheduled close event (nanosecond precision via scheduler)
scheduling_s m_multiInjectCloseScheduling;

// Scheduled end time for the multi-injection (in nanoseconds for scheduler precision)
efitick_t m_multiInjectEndNt;

efitick_t m_lastOpenTime = 0;

// Close callback for scheduled close event
static void multiInjectCloseCallback(InjectorOutputPin* pin);
};