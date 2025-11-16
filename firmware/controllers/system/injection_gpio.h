/*

@file injection_gpio.h
*/

#pragma once

#include "efi_output.h"



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

int16_t getOverlappingCounter() const { return overlappingCounter; }

int8_t injectorIndex;

private:
// Use a wider integer for atomic operations to avoid ABI/atomic-size issues
int16_t overlappingCounter;

// Timer for multi-injection custom duration
virtual_timer_t m_multiInjectTimer;

// Scheduled end time for the multi-injection (in microseconds since epoch returned by getTimeNowUs())
// Scheduled end time for the multi-injection (in microseconds since epoch returned by getTimeNowUs())
efitimeus_t m_multiInjectEndUs;

// Timer callback
static void timerCallback(virtual_timer_t *vtp, void *arg);
};