/**
 * @file fuel_schedule.h
 */

#pragma once

#include "global.h"
#include "efi_gpio.h"
#include "scheduler.h"
#include "fl_stack.h"
#include "trigger_structure.h"
#include "wall_fuel.h"

#define MAX_WIRES_COUNT 2

/**
 * Maximum number of injection pulses per engine cycle
 * Supports split/multiple injection strategies (e.g., pilot + main + post)
 */
#define MAX_INJECTION_PULSES 2


/**
 * Represents a single injection pulse within a multi-pulse injection event
 */
struct InjectionPulse {
	/**
	 * Start angle of this injection pulse (degrees BTDC)
	 */
	float startAngle = 0;
	
	/**
	 * Duration of this injection pulse (in crank degrees)
	 */
	float durationAngle = 0;
	
	/**
	 * Fuel quantity for this pulse (milliseconds)
	 */
	floatms_t fuelMs = 0;
	
	/**
	 * Percentage of total fuel in this pulse (0-100%)
	 */
	float splitRatio = 100.0f;
	
	/**
	 * Whether this pulse is active/scheduled
	 */
	bool isActive = false;
};

class InjectionEvent {
public:
	InjectionEvent() = default;

	bool update();

	// Call this every decoded trigger tooth. It will schedule any relevant events for this injector.
	void onTriggerTooth(efitick_t nowNt, float currentPhase, float nextPhase);
  void schedulePulse(uint8_t pulseIndex, efitick_t nowNt, float currentPhase);
  
	WallFuel& getWallFuel();

	void setIndex(uint8_t index) {
		ownIndex = index;
	}

	// ========== NEW: Multi-injection support ==========
	
	/**
	 * Configure multi-injection with specified number of pulses
	 * @param numPulses Number of injection pulses (1-5)
	 */
	void configureMultiInjection(uint8_t numPulses);
	
	/**
	 * Update injection angles for all pulses in multi-injection mode
	 * @return true if successful, false if validation failed
	 */
	bool updateMultiInjectionAngles();
	
	/**
	 * Compute start angle for a secondary injection pulse
	 * @param pulseIndex Index of the pulse (0 = main, 1+ = secondary)
	 * @return Injection angle in degrees BTDC
	 */
	float computeSecondaryInjectionAngle(uint8_t pulseIndex) const;
	
	/**
	 * Compute split ratio (fuel distribution) for a specific pulse
	 * @param pulseIndex Index of the pulse
	 * @return Split ratio as percentage (0-100%)
	 */
	float computeSplitRatio(uint8_t pulseIndex) const;
	
	/**
	 * Validate that injection windows don't overlap and meet timing constraints
	 * @return true if all windows are valid
	 */
	bool validateInjectionWindows() const;
	
	/**
	 * Calculate dwell time (gap) between consecutive injection pulses
	 * @param pulseIndex Index of the pulse to check dwell after
	 * @return Dwell time in crank degrees
	 */
	float calculateDwellTime(uint8_t pulseIndex) const;
	
	/**
	 * Get the total number of active injection pulses
	 */
	uint8_t getNumberOfPulses() const {
		return numberOfPulses;
	}
	
	/**
	 * Get injection pulse data for a specific pulse
	 * @param pulseIndex Index of the pulse (0-4)
	 * @return Reference to InjectionPulse structure
	 */
	const InjectionPulse& getPulse(uint8_t pulseIndex) const {
		return pulses[pulseIndex];
	}

private:
	// Update the injection start angle
	bool updateInjectionAngle();

	// Compute the injection start angle, compensating for injection duration and injection phase settings.
	expected<float> computeInjectionAngle() const;

	/**
	 * This is a performance optimization for IM_SIMULTANEOUS fuel strategy.
	 * It's more efficient to handle all injectors together if that's the case
	 */
	bool isSimultaneous = false;
	uint8_t ownIndex = 0;
	uint8_t cylinderNumber = 0;

	WallFuel wallFuel{};
	
	// ========== NEW: Multi-injection data ==========
	
	/**
	 * Number of injection pulses for this event (1 = single, 2+ = multi)
	 */
	uint8_t numberOfPulses = 1;
	
	/**
	 * Array of injection pulses for multi-injection
	 */
	InjectionPulse pulses[MAX_INJECTION_PULSES];

public:
	// TODO: this should be private
	InjectorOutputPin *outputs[MAX_WIRES_COUNT]{};
	InjectorOutputPin *outputsStage2[MAX_WIRES_COUNT]{};
	
	/**
	 * Primary injection start angle (degrees BTDC)
	 * For multi-injection, this is the angle of the first pulse
	 */
	float injectionStartAngle = 0;
};

void turnInjectionPinHigh(scheduler_arg_t arg);

/**
 * This class knows about when to inject fuel
 */
class FuelSchedule {
public:
	FuelSchedule();

	// Call this function if something happens that requires a rebuild, like a change to the trigger pattern
	void invalidate();

	// Call this every trigger tooth. It will schedule all required injector events.
	void onTriggerTooth(efitick_t nowNt, float currentPhase, float nextPhase);

	/**
	 * this method schedules all fuel events for an engine cycle
	 * Calculate injector opening angle, pins, and mode for all injectors
	 */
	void addFuelEvents();

	static void resetOverlapping();
	
	// ========== NEW: Multi-injection support ==========
	
	/**
	 * Configure multi-injection for all cylinders based on current engine configuration
	 */
	void configureMultiInjectionForAllCylinders();
	
	/**
	 * Check if multi-injection is enabled and conditions are met
	 * @return true if multi-injection should be active
	 */
	bool shouldUseMultiInjection() const;

	/**
	 * injection events, per cylinder
	 */
	InjectionEvent elements[MAX_CYLINDER_COUNT];
	bool isReady = false;
};

FuelSchedule * getFuelSchedule();
