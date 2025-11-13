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

int8_t getOverlappingCounter() const { return overlappingCounter; }

int8_t injectorIndex;

private:
int8_t overlappingCounter;

// Timer for multi-injection custom duration
virtual_timer_t m_multiInjectTimer;

// Timer callback
static void timerCallback(virtual_timer_t *vtp, void *arg);
};