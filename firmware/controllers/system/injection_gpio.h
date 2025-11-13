/*
 * @file injection_gpio.h
 */

#pragma once

#include "efi_output.h"

#ifdef UNIT_TEST
typedef void* virtual_timer_t;
#endif

void startSimultaneousInjection();
void endSimultaneousInjectionOnlyTogglePins();

class InjectorOutputPin final : public NamedOutputPin {
public:
	InjectorOutputPin();
	void reset();

	void open(efitick_t nowNt);
  void open(efitick_t nowNt, floatus_t durationUs);
	void close(efitick_t nowNt);
	void setHigh() override;
	void setLow() override;

	int8_t getOverlappingCounter() const { return overlappingCounter; }

	// todo: re-implement this injectorIndex via address manipulation to reduce memory usage?
	int8_t injectorIndex;

private:
	int8_t overlappingCounter;

  virtual_timer_t m_multiInjectTimer;  
  static void timerCallback(virtual_timer_t *vtp, void *arg);  
};
