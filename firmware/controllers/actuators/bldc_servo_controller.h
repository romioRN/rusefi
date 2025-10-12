#pragma once

#include "pch.h"
#include "periodic_thread_controller.h"
#include "engine_configuration.h"
#include "closed_loop_controller.h"
//#include "timer.h"
#include "global.h"
#include "pid.h"
#include "sensor.h"
#include "electronic_throttle_impl.h"

#if EFI_BLDC_SERVO

/**
 * @brief BLDC Servo Controller with Electronic Throttle Body functionality
 * 
 * Features:
 * - 6-step Hall sensor commutation using TLE7183F driver
 * - PID position control with configurable parameters
 * - Automatic limit detection (homing procedure)
 * - Direction reversal capability
 * - Electronic Throttle Body (ETB) mode with full engine integration
 * - Failsafe mechanism with automatic fallback to regular ETB
 * - Current monitoring and comprehensive diagnostics
 * - Real-time telemetry and logging
 * 
 * ETB Mode Operation:
 * When ETB mode is enabled, this controller takes over all throttle control functions:
 * - Pedal position processing
 * - Idle control integration
 * - Launch control, traction control, speed limiter
 * - Engine protection functions
 * - Automatic failsafe to regular ETB on any fault
 */
class BldcServoController : public PeriodicController<SLOW_CALLBACK_PERIOD_MS> {
public:    
    // PeriodicController interface
    void onSlowCallback() override;
    void onConfigurationChange() override;
    
    // Closed loop controller interface  
    expected<float> observePlant() override;
    expected<float> getSetpoint() override;
    void setOutput(float output) override;
    
    // Basic servo control API
    void init();
    void resetState();
    bool setTargetPosition(float positionPercent);
    float getCurrentPosition() const { return m_currentPosition; }
    bool performHoming();
    void reverseDirection();
    void enableController(bool enable);
    
    // ETB Mode API
    bool setEtbMode(bool enable);
    bool isEtbMode() const { return m_etbModeEnabled; }
    float getThrottlePosition() const;
    bool setThrottlePosition(float percent);
    float getThrottleTarget() const { return m_throttleTarget; }
    
    // ETB Compatibility methods
    float getTargetFromTable() const;
    void updateEtbTarget();
    bool isWithinDeadband() const;
    float getThrottleError() const;
    
    // Failsafe management
    bool hasEtbFault() const;
    void handleEtbFailsafe();
    void restoreEtbControl();
    bool isEtbFailsafeActive() const { return m_etbFailsafeActive; }
    
    // Diagnostics and status
    bool hasFault() const { return m_faultFlags != 0; }
    BldcFaultCode_e getFaultCode() const;
    void clearFaults() { m_faultFlags = 0; }
    BldcState_e getState() const { return m_state; }
    float getCurrentMa() const { return m_currentMa; }
    uint8_t getHallState() const { return m_hallState; }
    
    // Performance monitoring
    uint32_t getControlLoopCount() const { return m_controlLoopCount; }
    float getPidOutput() const { return m_pidOutput; }
    float getPeakCurrent() const { return m_peakCurrent; }

private:
    // Core state management
    void updateState();
    void updateDiagnostics();
    void updateTelemetry();
    
    // Motor control
    void setMotorOutput(float dutyA, float dutyB, float dutyC);
    void getCommutationDuties(float output, float& dutyA, float& dutyB, float& dutyC);
    void stopMotor();
    
    // Sensor processing
    void processHallSensors();
    void updatePositionFromHall();
    uint8_t readHallState() const;
    float readCurrent() const;
    
    // Homing state machine
    bool homingStateMachine();
    void startHoming();
    void stopHoming();
    bool checkStallCondition();
    
    // Hardware control
    void initializePins();
    void enableDriver(bool enable);
    void configurePwmOutputs();
    
    // ETB Mode implementation
    void processEtbMode();
    void calculateThrottleTarget();
    bool validateEtbTransition();
    void performEtbFailsafe();
    void monitorEtbHealth();
    void syncWithRegularEtb();
    void setRegularEtbFailsafe(bool enable);
    
    // ETB pedal and control processing
    float getPedalPosition() const;
    float getIdleTarget() const;
    float applyEngineProtections(float baseTarget);
    float applyLaunchControl(float target);
    float applyTractionControl(float target);
    float applySpeedLimiter(float target);
    
    // Safety checks
    bool validateConfiguration() const;
    void performSafetyChecks();
    void handleFaultCondition(BldcFaultCode_e faultCode);
    bool isEtbPositionReasonable() const;
    
    // Configuration
    const bldc_servo_s* m_config = nullptr;
    
    // Core runtime state
    BldcState_e m_state = BldcState::DISABLED;
    float m_currentPosition = 0.0f;  // Current position 0-100%
    float m_targetPosition = 0.0f;   // Target position 0-100%
    uint32_t m_faultFlags = 0;       // Fault status bits
    bool m_isEnabled = false;        // Controller enable state
    
    // ETB Mode state
    bool m_etbModeEnabled = false;        // ETB mode active flag
    bool m_etbFailsafeActive = false;     // Failsafe state
    float m_throttleTarget = 0.0f;        // Target throttle position (0-100%)
    float m_lastEtbPosition = 0.0f;       // Last known regular ETB position
    float m_etbSyncPosition = 0.0f;       // Position for ETB sync
    Timer m_etbModeTimer;                 // ETB mode timing
    Timer m_failsafeTimer;                // Failsafe activation timer
    Timer m_etbHealthTimer;               // ETB health monitoring
    
    // Hall sensor processing
    uint8_t m_hallState = 0;         // Current hall state (0-7)
    uint8_t m_lastHallState = 0;     // Previous hall state
    uint32_t m_hallTransitions = 0;  // Total hall transitions
    Timer m_hallTimer;               // Hall sensor timing
    
    // Position tracking
    int32_t m_encoderPosition = 0;   // Accumulated position from hall sensors
    float m_lastValidPosition = 0.0f;// Last known good position
    bool m_positionValid = false;    // Position validity flag
    
    // Homing state machine
    HomingState_e m_homingState = HomingState::IDLE;
    float m_homingStartPosition = 0.0f;
    float m_minPosition = 0.0f;      // Minimum limit (0%)
    float m_maxPosition = 100.0f;    // Maximum limit (100%)
    Timer m_homingTimer;             // Overall homing timeout
    Timer m_stallTimer;              // Stall detection timer
    float m_stallCurrentThreshold = 0.0f;
    
    // PID controller
    Pid m_positionPid;
    Timer m_pidTimer;
    float m_pidOutput = 0.0f;
    
    // Current monitoring
    float m_currentMa = 0.0f;        // Current motor current in mA
    float m_peakCurrent = 0.0f;      // Peak current since reset
    float m_avgCurrent = 0.0f;       // Running average current
    Timer m_currentTimer;            // Current measurement timing
    
    // Performance monitoring
    uint32_t m_controlLoopCount = 0; // Control loop execution counter
    Timer m_performanceTimer;        // Performance measurement
    uint32_t m_etbModeCount = 0;     // ETB mode cycles
    uint32_t m_failsafeCount = 0;    // Failsafe activations
    
    // ETB integration state
    float m_pedalPosition = 0.0f;    // Cached pedal position
    float m_idleTarget = 0.0f;       // Cached idle target
    float m_lastThrottleCommand = 0.0f; // Last throttle command
    Timer m_pedalTimer;              // Pedal reading timer
    
    // Constants
    static constexpr float DEAD_TIME_US = 1.0f;               // Dead time in microseconds
    static constexpr float POSITION_TOLERANCE = 0.5f;        // Position tolerance in %
    static constexpr float ETB_POSITION_TOLERANCE = 2.0f;    // ETB position tolerance in %
    static constexpr float HOMING_BACKUP_DISTANCE = 2.0f;    // Backup distance from limits
    static constexpr uint32_t HOMING_TIMEOUT_MS = 30000;     // 30 second homing timeout
    static constexpr uint32_t FAULT_RETRY_DELAY_MS = 1000;   // 1 second fault retry delay
    static constexpr uint32_t ETB_FAILSAFE_TIMEOUT_MS = 500; // 500ms ETB failsafe timeout
    static constexpr uint32_t ETB_HEALTH_CHECK_MS = 100;     // 100ms ETB health check period
    static constexpr float ETB_FAILSAFE_POSITION = 100.0f;   // Full open failsafe position
    static constexpr float ETB_SYNC_TOLERANCE = 5.0f;        // 5% sync tolerance
};

/**
 * @brief Global instance accessor
 * @return Reference to the singleton BLDC servo controller instance
 */
BldcServoController& getBldcServoController();

/**
 * @brief ETB integration helper functions
 */
namespace BldcEtbIntegration {
    bool isEtbModeActive();
    float getThrottlePosition();
    bool setThrottlePosition(float percent);
    void handleEtbFailsafe();
}

#endif // EFI_BLDC_SERVO
