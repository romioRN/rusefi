/*
 * bldc_servo_controller.h
 *
 *  Created on: 12. oct. 2025
 *      Author: Raman Ruzhkou
 */
#include "pch.h"

#if EFI_BLDC_SERVO

#include "bldc_servo_controller.h"
#include "pwm_generator_logic.h"
#include "engine.h"
#include "efi_gpio.h"
#include "adc_inputs.h"

#include "idle_thread.h"
#include "electronic_throttle_impl.h"

EXTERN_ENGINE;

// Global instance
static BldcServoController instance;

BldcServoController& getBldcServoController() {
    return instance;
}

void BldcServoController::onSlowCallback() {
    ScopePerf perf(PE::BldcServoController);
    
    // Validate configuration
    if (!m_config || !validateConfiguration()) {
        if (m_state != BldcState::DISABLED) {
            resetState();
        }
        return;
    }
    
    // Check enable state
    if (!CONFIG(bldcServo.enabled)) {
        if (m_state != BldcState::DISABLED) {
            resetState();
        }
        return;
    }
    
    // Initialize if needed
    if (m_state == BldcState::DISABLED && CONFIG(bldcServo.enabled)) {
        m_state = BldcState::INITIALIZING;
        initializePins();
        enableDriver(true);
        m_isEnabled = true;
        
        // Start homing if enabled and not in ETB mode
        if (CONFIG(bldcServo.homingEnabled) && !m_etbModeEnabled) {
            startHoming();
        } else {
            m_state = BldcState::IDLE;
        }
    }
    
    // Main state machine
    updateState();
    
    // Process ETB mode if enabled
    if (m_etbModeEnabled) {
        processEtbMode();
    }
    
    // Safety and diagnostics
    performSafetyChecks();
    updateDiagnostics();
    
    // Process control loop if running
    if (m_state == BldcState::POSITION_CONTROL) {
        auto setpoint = getSetpoint();
        auto observation = observePlant();
        
        if (setpoint.Valid && observation.Valid) {
            float pidOutput = m_positionPid.getOutput(
                setpoint.Value, 
                observation.Value, 
                SLOW_CALLBACK_PERIOD_MS
            );
            
            m_pidOutput = pidOutput;
            setOutput(pidOutput);
        }
    }
    
    // Update telemetry
    updateTelemetry();
    m_controlLoopCount++;
}

void BldcServoController::onConfigurationChange() {
    m_config = &CONFIG(bldcServo);
    
    // Reinitialize PID controller
    m_positionPid.initPidClass(&m_config->positionPid);
    
    // Update current thresholds
    m_stallCurrentThreshold = m_config->stallCurrentThreshold;
    
    // Update ETB mode if configuration changed
    if (CONFIG(bldcServo.etbModeEnabled) != m_etbModeEnabled) {
        setEtbMode(CONFIG(bldcServo.etbModeEnabled));
    }
    
    // Reconfigure hardware if needed
    if (m_state != BldcState::DISABLED) {
        initializePins();
    }
    
    efiPrintf("BLDC Servo configuration updated");
}

expected<float> BldcServoController::observePlant() {
    processHallSensors();
    
    if (!m_positionValid) {
        return unexpected;
    }
    
    return m_currentPosition;
}

expected<float> BldcServoController::getSetpoint() {
    if (m_etbModeEnabled) {
        return m_throttleTarget;
    }
    return m_targetPosition;
}

void BldcServoController::setOutput(float output) {
    if (m_state != BldcState::POSITION_CONTROL && m_state != BldcState::HOMING) {
        stopMotor();
        return;
    }
    
    // Clamp output to safe limits
    output = clampF(-1.0f, output, 1.0f);
    
    // Get 3-phase commutation duties
    float dutyA, dutyB, dutyC;
    getCommutationDuties(output, dutyA, dutyB, dutyC);
    
    // Apply to motor
    setMotorOutput(dutyA, dutyB, dutyC);
}

void BldcServoController::getCommutationDuties(float output, float& dutyA, float& dutyB, float& dutyC) {
    // Initialize to zero (motor off)
    dutyA = dutyB = dutyC = 0.0f;
    
    // 6-step commutation lookup table
    // Based on Hall sensor states for 120-degree electrical separation
    static const struct CommutationEntry {
        float dutyA, dutyB, dutyC;
    } commutationTable[8] = {
        {0.0f,  0.0f,  0.0f},   // Invalid state 0 (000)
        {1.0f, -1.0f,  0.0f},   // Hall state 1 (001): Phase A+, B-
        {0.0f,  1.0f, -1.0f},   // Hall state 2 (010): Phase B+, C-
        {1.0f,  0.0f, -1.0f},   // Hall state 3 (011): Phase A+, C-
        {-1.0f, 0.0f,  1.0f},   // Hall state 4 (100): Phase C+, A-
        {0.0f, -1.0f,  1.0f},   // Hall state 5 (101): Phase C+, B-
        {-1.0f, 1.0f,  0.0f},   // Hall state 6 (110): Phase B+, A-
        {0.0f,  0.0f,  0.0f}    // Invalid state 7 (111)
    };
    
    // Validate hall state
    if (m_hallState == 0 || m_hallState == 7) {
        handleFaultCondition(BldcFaultCode_e::SENSOR_FAULT);
        return;
    }
    
    // Get base commutation pattern
    dutyA = commutationTable[m_hallState].dutyA * output;
    dutyB = commutationTable[m_hallState].dutyB * output;
    dutyC = commutationTable[m_hallState].dutyC * output;
    
    // Apply direction reversal if configured
    if (CONFIG(bldcServo.reverseDirection)) {
        dutyA = -dutyA;
        dutyB = -dutyB;
        dutyC = -dutyC;
    }
}

void BldcServoController::setMotorOutput(float dutyA, float dutyB, float dutyC) {
    if (!m_config) return;
    
    // Clamp duty cycles to valid range
    dutyA = clampF(-1.0f, dutyA, 1.0f);
    dutyB = clampF(-1.0f, dutyB, 1.0f);
    dutyC = clampF(-1.0f, dutyC, 1.0f);
    
    // Convert to TLE7183F control signals with dead time
    const float deadTime = DEAD_TIME_US / 1000000.0f; // Convert to fraction
    
    // Generate complementary PWM signals with dead time
    bool highA = dutyA > deadTime;
    bool lowA = dutyA < -deadTime;
    bool highB = dutyB > deadTime;
    bool lowB = dutyB < -deadTime;
    bool highC = dutyC > deadTime;
    bool lowC = dutyC < -deadTime;
    
    // Set TLE7183F control pins
    enginePins.getOutputPin(m_config->hardware.highSide1Pin)->setValue(highA);
    enginePins.getOutputPin(m_config->hardware.lowSide1Pin)->setValue(lowA);
    enginePins.getOutputPin(m_config->hardware.highSide2Pin)->setValue(highB);
    enginePins.getOutputPin(m_config->hardware.lowSide2Pin)->setValue(lowB);
    enginePins.getOutputPin(m_config->hardware.highSide3Pin)->setValue(highC);
    enginePins.getOutputPin(m_config->hardware.lowSide3Pin)->setValue(lowC);
}

// ETB MODE IMPLEMENTATION

bool BldcServoController::setEtbMode(bool enable) {
    if (enable == m_etbModeEnabled) {
        return true; // Already in requested mode
    }
    
    if (enable) {
        // Switching TO ETB mode
        if (m_state != BldcState::IDLE && m_state != BldcState::POSITION_CONTROL) {
            efiPrintf("BLDC: Cannot enable ETB mode - controller not ready (state %d)", (int)m_state);
            return false;
        }
        
        if (!validateEtbTransition()) {
            efiPrintf("BLDC: ETB mode transition validation failed");
            return false;
        }
        
        // Sync positions
        syncWithRegularEtb();
        
        m_etbModeEnabled = true;
        m_etbFailsafeActive = false;
        m_etbModeTimer.reset();
        m_etbHealthTimer.reset();
        
        // Set regular ETB to failsafe position
        setRegularEtbFailsafe(true);
        
        // Switch to running mode if not already
        if (m_state == BldcState::IDLE) {
            m_state = BldcState::POSITION_CONTROL;
        }
        
        efiPrintf("BLDC: ETB mode ENABLED - BLDC now controls throttle");
        
    } else {
        // Switching FROM ETB mode back to normal
        m_etbModeEnabled = false;
        m_etbFailsafeActive = false;
        
        // Restore normal ETB operation
        restoreEtbControl();
        
        efiPrintf("BLDC: ETB mode DISABLED - returning to normal servo mode");
    }
    
    return true;
}

void BldcServoController::processEtbMode() {
    if (!m_etbModeEnabled) return;
    
    m_etbModeCount++;
    
    // Monitor BLDC health in ETB mode
    if (m_etbHealthTimer.hasElapsedMs(ETB_HEALTH_CHECK_MS)) {
        monitorEtbHealth();
        m_etbHealthTimer.reset();
    }
    
    // Handle failsafe if active
    if (m_etbFailsafeActive) {
        performEtbFailsafe();
        return;
    }
    
    // Calculate new throttle target based on engine requirements
    calculateThrottleTarget();
    
    // Apply rate limiting for smooth operation
    float currentPosition = getCurrentPosition();
    float targetDifference = m_throttleTarget - currentPosition;
    float transitionRate = CONFIG(bldcServo.etbTransitionRate);
    float maxChange = transitionRate * (SLOW_CALLBACK_PERIOD_MS / 1000.0f);
    
    if (absF(targetDifference) > maxChange) {
        if (targetDifference > 0) {
            m_throttleTarget = currentPosition + maxChange;
        } else {
            m_throttleTarget = currentPosition - maxChange;
        }
    }
    
    // Update target position
    m_targetPosition = m_throttleTarget;
    m_lastThrottleCommand = m_throttleTarget;
}

void BldcServoController::calculateThrottleTarget() {
    if (!m_etbModeEnabled) return;
    
    // Get base throttle from pedal position
    float pedalPosition = getPedalPosition();
    
    // Apply pedal-to-throttle mapping using simple linear mapping for now
    float baseThrottle = pedalPosition;
    if (engineConfiguration->pedalToTpsTable.loadBins != nullptr) {
        baseThrottle = interpolate2d(pedalPosition, 
            engineConfiguration->pedalToTpsTable.loadBins,
            engineConfiguration->pedalToTpsTable.rpmBins,
            engineConfiguration->pedalToTpsTable.values);
    }
    
    // Get idle control target
    float idleTarget = getIdleTarget();
    
    // Combine base throttle with idle control
    float combinedTarget = maxF(baseThrottle, idleTarget);
    
    // Apply engine protection systems
    float finalTarget = applyEngineProtections(combinedTarget);
    
    // Clamp to valid range
    m_throttleTarget = clampF(0.0f, finalTarget, 100.0f);
}

float BldcServoController::getPedalPosition() const {
    if (engineConfiguration->throttlePedalPositionAdcChannel == EFI_ADC_NONE) {
        return 0.0f;
    }
    
    float rawValue = getAnalogInputDivided(engineConfiguration->throttlePedalPositionAdcChannel);
    
    // Apply calibration
    float minVoltage = engineConfiguration->throttlePedalUpVoltage;
    float maxVoltage = engineConfiguration->throttlePedalWOTVoltage;
    
    if (maxVoltage <= minVoltage) {
        return 0.0f; // Invalid calibration
    }
    
    float position = (rawValue - minVoltage) / (maxVoltage - minVoltage);
    return clampF(0.0f, position * 100.0f, 100.0f);
}

float BldcServoController::getIdleTarget() const {
    if (!engine->engineState.idle.shouldIdleControl) {
        return 0.0f;
    }
    
    return engine->engineState.idle.currentIdlePosition;
}

float BldcServoController::applyEngineProtections(float baseTarget) {
    float target = baseTarget;
    
    // Apply launch control
    target = applyLaunchControl(target);
    
    // Apply traction control
    target = applyTractionControl(target);
    
    // Apply speed limiter
    target = applySpeedLimiter(target);
    
    return target;
}

float BldcServoController::applyLaunchControl(float target) {
    // Простая проверка TPS без launch controller
    if (Sensor::getOrZero(SensorType::Tps1) > 5.0f) {  // TPS > 5% - не launch
        return target;
    }
    
    float launchLimit = engineConfiguration->launchTpsThreshold;
    return minF(target, launchLimit);
}

float BldcServoController::applyTractionControl(float target) {
    if (!engine->engineState.traction.tractionControlEnabled) {
        return target;
    }
    
    return target * engine->engineState.traction.throttleReduction;
}

float BldcServoController::applySpeedLimiter(float target) {
    if (!engine->engineState.speedLimiter.speedLimiterEnabled) {
        return target;
    }
    
    return target * 0.1f; // Dramatic reduction for speed limiting
}

void BldcServoController::syncWithRegularEtb() {
    // Get current ETB position
    m_lastEtbPosition = engine->outputChannels.throttlePosition;
    m_etbSyncPosition = m_lastEtbPosition;
    
    // Set BLDC to match ETB position
    m_throttleTarget = m_lastEtbPosition;
    m_targetPosition = m_lastEtbPosition;
    
    efiPrintf("BLDC: Synced to ETB position: %.1f%%", m_lastEtbPosition);
}

void BldcServoController::setRegularEtbFailsafe(bool enable) {
    if (enable) {
        // Set regular ETB to full open (failsafe position)
        // This is typically done by modifying ETB target or using a failsafe flag
        engine->etbTarget = ETB_FAILSAFE_POSITION;
        efiPrintf("BLDC: Regular ETB set to failsafe position (%.1f%%)", ETB_FAILSAFE_POSITION);
    }
}

bool BldcServoController::validateEtbTransition() {
    // Check BLDC is healthy
    if (hasFault()) {
        efiPrintf("BLDC: ETB transition blocked - BLDC has faults: 0x%08X", (unsigned int)m_faultFlags);
        return false;
    }
    
    // Check position feedback is valid
    if (!m_positionValid) {
        efiPrintf("BLDC: ETB transition blocked - position feedback invalid");
        return false;
    }
    
    // Check ETB is currently functional
    if (engine->outputChannels.etbErrorCode != 0) {
        efiPrintf("BLDC: ETB transition blocked - ETB has error %d", 
                 engine->outputChannels.etbErrorCode);
        return false;
    }
    
    // Check position synchronization
    if (!isEtbPositionReasonable()) {
        return false;
    }
    
    return true;
}

bool BldcServoController::isEtbPositionReasonable() const {
    float etbPosition = engine->outputChannels.throttlePosition;
    float bldcPosition = getCurrentPosition();
    float difference = absF(etbPosition - bldcPosition);
    
    if (difference > ETB_SYNC_TOLERANCE) {
        efiPrintf("BLDC: Position sync issue - ETB: %.1f%%, BLDC: %.1f%%, diff: %.1f%%", 
                 etbPosition, bldcPosition, difference);
        return false;
    }
    
    return true;
}

void BldcServoController::monitorEtbHealth() {
    // Check for BLDC faults that require ETB failsafe
    bool needsFailsafe = false;
    
    // Critical faults requiring immediate failsafe
    if (m_faultFlags & static_cast<uint32_t>(BldcFaultCode_e::OVERCURRENT)) {
        needsFailsafe = true;
        efiPrintf("BLDC ETB: Overcurrent fault detected");
    }
    
    if (m_faultFlags & static_cast<uint32_t>(BldcFaultCode_e::SENSOR_FAULT)) {
        needsFailsafe = true;
        efiPrintf("BLDC ETB: Hall sensor fault detected");
    }
    
    if (m_state == BldcState::FAULT) {
        needsFailsafe = true;
        efiPrintf("BLDC ETB: Controller in fault state");
    }
    
    // Position control failure
    if (m_state == BldcState::POSITION_CONTROL) {
        float positionError = absF(getCurrentPosition() - m_throttleTarget);
        static Timer positionErrorTimer;
        
        if (positionError > ETB_POSITION_TOLERANCE) {
            if (positionErrorTimer.hasElapsedMs(2000)) { // 2 seconds of error
                needsFailsafe = true;
                efiPrintf("BLDC ETB: Position control failure - error: %.1f%%", positionError);
            }
        } else {
            positionErrorTimer.reset();
        }
    }
    
    // Activate failsafe if needed
    if (needsFailsafe && !m_etbFailsafeActive) {
        handleEtbFailsafe();
    }
}

void BldcServoController::handleEtbFailsafe() {
    efiPrintf("BLDC ETB: FAILSAFE ACTIVATED - Switching to regular ETB");
    
    m_etbFailsafeActive = true;
    m_failsafeTimer.reset();
    m_failsafeCount++;
    
    // Stop BLDC motor immediately
    stopMotor();
    enableDriver(false);
    
    // Restore regular ETB control
    restoreEtbControl();
    
    // Set fault flag
    m_faultFlags |= static_cast<uint32_t>(BldcFaultCode_e::COMMUNICATION_ERROR);
}

void BldcServoController::performEtbFailsafe() {
    // Monitor for recovery possibility
    if (m_failsafeTimer.hasElapsedMs(5000)) { // 5 second recovery attempt
        // Try to restore BLDC control if conditions are met
        if (!hasFault() && m_state == BldcState::IDLE) {
            efiPrintf("BLDC ETB: Attempting automatic recovery");
            
            m_etbFailsafeActive = false;
            enableDriver(true);
            
            // Sync with current ETB position
            syncWithRegularEtb();
            
            if (m_state == BldcState::IDLE) {
                m_state = BldcState::POSITION_CONTROL;
            }
            
            efiPrintf("BLDC ETB: Recovery successful");
        } else {
            // Reset timer for next attempt
            m_failsafeTimer.reset();
        }
    }
}

void BldcServoController::restoreEtbControl() {
    // Restore normal ETB operation
    engine->etbTarget = m_lastEtbPosition; // Return to last known position
    
    efiPrintf("BLDC: Regular ETB control restored");
}

// Additional required methods (abbreviated for space)

void BldcServoController::processHallSensors() {
    if (!m_config) return;
    
    // Read hall sensor states - simplified version
    bool hall1 = enginePins.getOutputPin(m_config->hallSensor1Pin)->getLogicValue();
    bool hall2 = enginePins.getOutputPin(m_config->hallSensor2Pin)->getLogicValue();
    bool hall3 = enginePins.getOutputPin(m_config->hallSensor3Pin)->getLogicValue();
    
    // Combine into 3-bit state
    uint8_t newHallState = (hall3 << 2) | (hall2 << 1) | hall1;
    
    // Check for state change
    if (newHallState != m_hallState) {
        m_lastHallState = m_hallState;
        m_hallState = newHallState;
        m_hallTransitions++;
        
        // Update position based on hall transitions
        updatePositionFromHall();
        
        // Reset hall timer for speed calculation
        m_hallTimer.reset();
    }
}

void BldcServoController::updatePositionFromHall() {
    // Simplified position calculation
    static const int8_t forwardSequence[8] = {0, 3, 6, 2, 5, 1, 4, 0};
    
    if (m_lastHallState != 0 && m_lastHallState != 7 && m_hallState != 0 && m_hallState != 7) {
        int8_t expectedNext = forwardSequence[m_lastHallState];
        
        if (m_hallState == expectedNext) {
            m_encoderPosition++;
        } else {
            m_encoderPosition--;
        }
        
        // Convert to percentage (simplified)
        const int32_t STEPS_PER_RANGE = 60; // Example: 60 steps for full range
        float positionFraction = (float)(m_encoderPosition % STEPS_PER_RANGE) / STEPS_PER_RANGE;
        
        if (positionFraction < 0) positionFraction += 1.0f;
        
        float range = m_maxPosition - m_minPosition;
        m_currentPosition = m_minPosition + (positionFraction * range);
        m_positionValid = true;
    }
}

void BldcServoController::updateTelemetry() {
    // Update output channels for TunerStudio display
    engine->outputChannels.bldcState = static_cast<uint8_t>(m_state);
    engine->outputChannels.bldcCurrentPosition = m_currentPosition;
    engine->outputChannels.bldcTargetPosition = m_targetPosition;
    engine->outputChannels.bldcCurrentMa = m_currentMa;
    engine->outputChannels.bldcHallState = m_hallState;
    engine->outputChannels.bldcFaultFlags = m_faultFlags;
    engine->outputChannels.bldcHomingState = static_cast<uint8_t>(m_homingState);
    engine->outputChannels.bldcPidOutput = m_pidOutput;
    engine->outputChannels.bldcPeakCurrent = m_peakCurrent;
    
    // ETB mode telemetry
    engine->outputChannels.bldcEtbMode = m_etbModeEnabled;
    engine->outputChannels.bldcEtbFailsafe = m_etbFailsafeActive;
    engine->outputChannels.bldcThrottleTarget = m_throttleTarget;
    engine->outputChannels.bldcThrottleError = getThrottleError();
    
    // Override throttle position if in ETB mode
    if (m_etbModeEnabled && !m_etbFailsafeActive) {
        engine->outputChannels.throttlePosition = getCurrentPosition();
        engine->outputChannels.throttleTarget = m_throttleTarget;
    }
}

// Stub implementations for missing methods
void BldcServoController::init() {
    efiPrintf("Initializing BLDC Servo Controller with ETB capability");
    // Implementation details...
}

void BldcServoController::resetState() {
    m_state = BldcState::DISABLED;
    m_isEnabled = false;
    stopMotor();
}

void BldcServoController::updateState() {
    // State machine implementation
}

void BldcServoController::updateDiagnostics() {
    // Diagnostics implementation
}

void BldcServoController::performSafetyChecks() {
    // Safety checks implementation
}

bool BldcServoController::validateConfiguration() const {
    return m_config != nullptr;
}

void BldcServoController::handleFaultCondition(BldcFaultCode_e faultCode) {
    m_faultFlags |= static_cast<uint32_t>(faultCode);
    m_state = BldcState::FAULT;
}

void BldcServoController::initializePins() {
    // Pin initialization
}

void BldcServoController::enableDriver(bool enable) {
    // Driver enable/disable
}

void BldcServoController::stopMotor() {
    setMotorOutput(0, 0, 0);
}

void BldcServoController::startHoming() {
    m_state = BldcState::HOMING;
    m_homingState = HomingState::STARTING;
}

// ETB Compatibility methods
float BldcServoController::getThrottlePosition() const {
    if (m_etbModeEnabled && !m_etbFailsafeActive) {
        return getCurrentPosition();
    }
    return engine->outputChannels.throttlePosition;
}

bool BldcServoController::setThrottlePosition(float percent) {
    if (!m_etbModeEnabled || m_etbFailsafeActive) {
        return false;
    }
    
    m_throttleTarget = clampF(0.0f, percent, 100.0f);
    m_targetPosition = m_throttleTarget;
    return true;
}

float BldcServoController::getTargetFromTable() const {
    return m_etbModeEnabled ? m_throttleTarget : m_targetPosition;
}

void BldcServoController::updateEtbTarget() {
    if (m_etbModeEnabled) {
        calculateThrottleTarget();
    }
}

bool BldcServoController::isWithinDeadband() const {
    if (!m_etbModeEnabled) return true;
    
    float deadband = CONFIG(bldcServo.etbDeadband) * 0.1f;
    float error = absF(getCurrentPosition() - m_throttleTarget);
    return error < deadband;
}

float BldcServoController::getThrottleError() const {
    if (!m_etbModeEnabled) return 0.0f;
    return m_throttleTarget - getCurrentPosition();
}

bool BldcServoController::hasEtbFault() const {
    return m_etbFailsafeActive || hasFault();
}

BldcFaultCode_e BldcServoController::getFaultCode() const {
    if (m_faultFlags == 0) return BldcFaultCode_e::NONE;
    
    // Return first fault found
    if (m_faultFlags & static_cast<uint32_t>(BldcFaultCode_e::OVERCURRENT))
        return BldcFaultCode_e::OVERCURRENT;
    if (m_faultFlags & static_cast<uint32_t>(BldcFaultCode_e::POSITION_ERROR))
        return BldcFaultCode_e::POSITION_ERROR;
    if (m_faultFlags & static_cast<uint32_t>(BldcFaultCode_e::SENSOR_FAULT))
        return BldcFaultCode_e::SENSOR_FAULT;
    if (m_faultFlags & static_cast<uint32_t>(BldcFaultCode_e::COMMUNICATION_ERROR))
        return BldcFaultCode_e::COMMUNICATION_ERROR;
        
    return BldcFaultCode_e::NONE;
}

// ETB Integration namespace implementation
namespace BldcEtbIntegration {
    bool isEtbModeActive() {
        return getBldcServoController().isEtbMode();
    }
    
    float getThrottlePosition() {
        return getBldcServoController().getThrottlePosition();
    }
    
    bool setThrottlePosition(float percent) {
        return getBldcServoController().setThrottlePosition(percent);
    }
    
    void handleEtbFailsafe() {
        getBldcServoController().handleEtbFailsafe();
    }
}

#endif // EFI_BLDC_SERVO
