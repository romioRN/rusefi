/**
 * @file multi_injection.cpp
 * @brief Multi-injection (split injection) support for GDI engines
 *
 * Implements multiple injection pulses per cycle to improve:
 * - Charge cooling effect
 * - Mixture homogeneity
 * - Knock resistance
 * - Particle emissions reduction
 *
 * @date Nov 11, 2025
 * @author rusEFI Team
 */

#include "pch.h"
#include "fuel_schedule.h"

#if EFI_ENGINE_CONTROL

// Multi-injection constants
static constexpr float MIN_DWELL_ANGLE = 10.0f;           // Minimum gap between pulses (degrees)
static constexpr float ABORT_ANGLE_SAFETY = 30.0f;        // Safety margin before ignition (degrees)
static constexpr float MAX_INJECTION_DURATION = 180.0f;   // Maximum single pulse duration (degrees)

void InjectionEvent::configureMultiInjection(uint8_t numPulses) {
	// Clamp to valid range
	numberOfPulses = minI(maxI(numPulses, 1), MAX_INJECTION_PULSES);
	
	// Disable if not configured
	if (!engineConfiguration->multiInjection.enableMultiInjection) {
		numberOfPulses = 1;
		return;
	}
	
	// Initialize split ratios from configuration
	pulses[0].splitRatio = engineConfiguration->multiInjection.splitRatio1;
	pulses[1].splitRatio = engineConfiguration->multiInjection.splitRatio2;
	pulses[2].splitRatio = engineConfiguration->multiInjection.splitRatio3;
	pulses[3].splitRatio = engineConfiguration->multiInjection.splitRatio4;
	pulses[4].splitRatio = engineConfiguration->multiInjection.splitRatio5;
	
	// Normalize split ratios (must sum to 100%)
	float totalRatio = 0;
	for (uint8_t i = 0; i < numberOfPulses; i++) {
		totalRatio += pulses[i].splitRatio;
	}
	
	if (totalRatio > 0.1f) {  // Avoid division by zero
		for (uint8_t i = 0; i < numberOfPulses; i++) {
			pulses[i].splitRatio = (pulses[i].splitRatio / totalRatio) * 100.0f;
		}
	} else {
		// Fallback: equal distribution
		float equalRatio = 100.0f / numberOfPulses;
		for (uint8_t i = 0; i < numberOfPulses; i++) {
			pulses[i].splitRatio = equalRatio;
		}
	}
	
	// Mark inactive pulses
	for (uint8_t i = numberOfPulses; i < MAX_INJECTION_PULSES; i++) {
		pulses[i].isActive = false;
		pulses[i].splitRatio = 0;
	}
}

float InjectionEvent::computeSplitRatio(uint8_t pulseIndex) const {
	if (pulseIndex >= numberOfPulses) {
		return 0.0f;
	}
	
	// Base ratio from configuration/normalization
	float baseRatio = pulses[pulseIndex].splitRatio;
	
	// Dynamic adjustment from table (only for first pulse)
	if (engineConfiguration->multiInjection.enableLoadBasedSplit && pulseIndex == 0) {
		float rpm = Sensor::getOrZero(SensorType::Rpm);
		float load = getFuelingLoad();
		
		// Interpolate from 3D table
		baseRatio = interpolate3d(
			engineConfiguration->multiInjectionSplitRatioTable,
			engineConfiguration->multiInjectionLoadBins,
			load,
			engineConfiguration->multiInjectionRpmBins,
			rpm
		);
	}
	
	return baseRatio;
}

float InjectionEvent::computeSecondaryInjectionAngle(uint8_t pulseIndex) const {
	if (pulseIndex == 0) {
		// First pulse uses standard injection angle calculation
		return injectionStartAngle;
	}
	
	// Get base angle from configuration
	float baseAngle = 0;
	switch (pulseIndex) {
		case 1:
			baseAngle = engineConfiguration->multiInjection.injection2AngleOffset;
			break;
		case 2:
			baseAngle = engineConfiguration->multiInjection.injection3AngleOffset;
			break;
		case 3:
			baseAngle = engineConfiguration->multiInjection.injection4AngleOffset;
			break;
		case 4:
			baseAngle = engineConfiguration->multiInjection.injection5AngleOffset;
			break;
		default:
			// Fallback: safe angle during compression stroke
			baseAngle = 100.0f;
			break;
	}
	
	// Apply RPM and load correction from table (for second pulse only)
	if (pulseIndex == 1 && engineConfiguration->multiInjection.enableRpmAngleCorrection) {
		float rpm = Sensor::getOrZero(SensorType::Rpm);
		float load = getFuelingLoad();
		
		float tableAngle = interpolate3d(
			engineConfiguration->secondInjectionAngleTable,
			engineConfiguration->multiInjectionLoadBins,
			load,
			engineConfiguration->multiInjectionRpmBins,
			rpm
		);
		
		baseAngle = tableAngle;
	}
	
	return baseAngle;
}

float InjectionEvent::calculateDwellTime(uint8_t pulseIndex) const {
	if (pulseIndex >= numberOfPulses - 1) {
		return 0;  // No dwell after last pulse
	}
	
	// Dwell = end of current pulse to start of next pulse
	float endOfCurrent = pulses[pulseIndex].startAngle - pulses[pulseIndex].durationAngle;
	float startOfNext = pulses[pulseIndex + 1].startAngle;
	
	return endOfCurrent - startOfNext;
}

bool InjectionEvent::validateInjectionWindows() const {
	// Get minimum dwell requirement
	float minDwell = engineConfiguration->multiInjection.dwellAngleBetweenInjections;
	if (minDwell < MIN_DWELL_ANGLE) {
		minDwell = MIN_DWELL_ANGLE;
	}
	
	// Check overlaps between consecutive pulses
	for (uint8_t i = 0; i < numberOfPulses - 1; i++) {
		if (!pulses[i].isActive || !pulses[i + 1].isActive) {
			continue;
		}
		
		float dwell = calculateDwellTime(i);
		
		// Check for overlap or insufficient dwell
		if (dwell < minDwell) {
			warning(ObdCode::CUSTOM_MULTI_INJECTION_OVERLAP,
					"Multi-injection overlap: pulse %d->%d, dwell %.1f° < min %.1f°",
					i, i + 1, dwell, minDwell);
			return false;
		}
	}
	
	// Check last pulse doesn't interfere with ignition
	uint8_t lastPulseIdx = numberOfPulses - 1;
	if (pulses[lastPulseIdx].isActive) {
		float lastEnd = pulses[lastPulseIdx].startAngle - pulses[lastPulseIdx].durationAngle;
		
		// Get ignition advance angle
		float ignitionAngle = engine->engineState.timingAdvance[cylinderNumber];
		
		// Ensure safety margin before ignition
		if (lastEnd < (ignitionAngle + ABORT_ANGLE_SAFETY)) {
			warning(ObdCode::CUSTOM_MULTI_INJECTION_TOO_LATE,
					"Multi-injection too late: pulse %d ends at %.1f°, ignition at %.1f°",
					lastPulseIdx, lastEnd, ignitionAngle);
			return false;
		}
	}
	
	return true;
}

bool InjectionEvent::updateMultiInjectionAngles() {
	// Fallback to single injection if disabled or only 1 pulse
	if (!engineConfiguration->multiInjection.enableMultiInjection || numberOfPulses == 1) {
		return updateInjectionAngle();
	}
	
	// Get total fuel duration from main calculation
	floatms_t totalFuelMs = engine->engineState.injectionDuration;
	
	// Get current engine parameters
	float rpm = Sensor::getOrZero(SensorType::Rpm);
	if (rpm < 1) {
		// Engine not running, can't calculate angles
		return false;
	}
	
	// Calculate microseconds per degree
	float oneDegreeUs = getOneDegreeTimeUs(rpm);
	if (oneDegreeUs < 0.1f) {
		return false;  // Invalid
	}
	
	// Distribute fuel and calculate angles for each pulse
	for (uint8_t i = 0; i < numberOfPulses; i++) {
		// Calculate split ratio (may be dynamic from table)
		float ratio = computeSplitRatio(i);
		
		// Calculate fuel quantity for this pulse
		floatms_t pulseFuelMs = totalFuelMs * (ratio / 100.0f);
		pulses[i].fuelMs = pulseFuelMs;
		pulses[i].splitRatio = ratio;
		
		// Convert fuel duration (ms) to crank angle (degrees)
		float pulseDurationAngle = MS2US(pulseFuelMs) / oneDegreeUs;
		
		// Clamp to reasonable limits
		if (pulseDurationAngle > MAX_INJECTION_DURATION) {
			warning(ObdCode::CUSTOM_MULTI_INJECTION_INVALID_CONFIG,
					"Multi-injection pulse %d too long: %.1f° > max %.1f°",
					i, pulseDurationAngle, MAX_INJECTION_DURATION);
			pulseDurationAngle = MAX_INJECTION_DURATION;
		}
		
		pulses[i].durationAngle = pulseDurationAngle;
		
		// Calculate start angle
		if (i == 0) {
			// First pulse: use standard injection angle calculation
			auto result = computeInjectionAngle();
			if (!result.Valid) {
				return false;
			}
			pulses[0].startAngle = result.Value;
			injectionStartAngle = result.Value;  // Update main angle
		} else {
			// Secondary pulses: use configured/table-based angles
			pulses[i].startAngle = computeSecondaryInjectionAngle(i);
		}
		
		pulses[i].isActive = true;
	}
	
	// Validate that all windows are valid
	if (!validateInjectionWindows()) {
		// Validation failed, fall back to single injection
		warning(ObdCode::CUSTOM_MULTI_INJECTION_INVALID_CONFIG,
				"Multi-injection validation failed, falling back to single injection");
		numberOfPulses = 1;
		pulses[0].splitRatio = 100.0f;
		pulses[0].fuelMs = totalFuelMs;
		return updateInjectionAngle();
	}
	
	return true;
}

#endif // EFI_ENGINE_CONTROL
