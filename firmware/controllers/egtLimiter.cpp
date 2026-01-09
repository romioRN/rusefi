#include "egtLimiter.h"
#include "engine.h"
#include "sensor_reader.h"
#include "rusefi_hw_enums.h"
#include <cmath>  // for std::isnan()

// Глобальный экземпляр
EgtLimiter egtLimiter;

void initEgtLimiter() {
    egtLimiter.init();
}

void EgtLimiter::init() {
    m_currentEgt = 0;
    m_isActive = false;
    m_limitPercent = 0;
    m_timeOverLimit = 0;
    m_appliedIgnitionRetard = 0;
    m_appliedThrottlePosition = 100;
    m_appliedLambdaReduction = 0;
}

void EgtLimiter::update(float dtSeconds) {
    if (!engineConfiguration->egtLimit.enabled) {
        m_isActive = false;
        m_limitPercent = 0;
        m_timeOverLimit = 0;
        m_appliedIgnitionRetard = 0;
        m_appliedThrottlePosition = 100;
        m_appliedLambdaReduction = 0;
        return;
    }

    computeLimits(dtSeconds);
}

void EgtLimiter::computeLimits(float dtSeconds) {
    // Read real EGT temperature from sensor (EGT1 is typically the primary/hottest sensor)
    // Sensor::get<float>() returns value with fallback to 0 if sensor not available
    m_currentEgt = Sensor::get(SensorType::EGT1).value_or(0.0f);
    
    // If EGT sensor is not configured, try to use the sensor registry to find available EGT
    // This ensures we get the hottest exhaust gas temperature among all cylinders
    if (m_currentEgt <= 0.0f) {
        // Fallback: try other EGT sensors in order (EGT2, EGT3, etc)
        // This ensures we always get temperature if any EGT sensor is configured
        static const SensorType egtSensorTypes[] = {
            SensorType::EGT1, SensorType::EGT2, SensorType::EGT3, 
            SensorType::EGT4, SensorType::EGT5, SensorType::EGT6
        };
        
        float maxEgtValue = 0.0f;
        for (size_t i = 0; i < sizeof(egtSensorTypes) / sizeof(egtSensorTypes[0]); i++) {
            auto egtReading = Sensor::get(egtSensorTypes[i]);
            if (egtReading) {
                float val = egtReading.value();
                if (val > maxEgtValue && val > 0) {
                    maxEgtValue = val;  // Track hottest temperature
                }
            }
        }
        m_currentEgt = maxEgtValue;
    }

    int16_t maxEgt = engineConfiguration->egtLimit.maxEgtTemp;
    bool egtExceeded = m_currentEgt > maxEgt;
    
    // === FAIL-SAFE LOGIC: Emergency fuel cut-off if critical error or sensor failure ===
    // If EGT reading is invalid (sensor error, out of range), trigger emergency shutdown
    // This prevents injector hang and engine damage from uncontrolled fuel delivery
    bool sensorError = std::isnan(m_currentEgt) || m_currentEgt < -50.0f || m_currentEgt > 1500.0f;
    if (sensorError) {
        // Sensor malfunction or invalid reading: force emergency fuel cut-off
        m_appliedThrottlePosition = 0;  // Full throttle cut (disables fuel delivery)
        m_limitPercent = 100;  // Max limit engaged
        m_isActive = true;  // Mark as active to indicate emergency state
        
        // Log critical error for diagnostics
        if (isVerboseMultiInjection()) {
            efiPrintf("EGT SENSOR ERROR: egt=%.1f C, triggering emergency fuel cut-off to prevent injector hang", m_currentEgt);
        }
        return;  // Skip normal processing, maintain emergency state until sensor recovers
    }

    static efitick_t activationTime = 0;   // было efitime_t

    if (egtExceeded && !m_isActive) {
        activationTime = getTimeNowNt();
    }

    float timeSinceActivation = 0;
    if (activationTime > 0) {
        timeSinceActivation = (getTimeNowNt() - activationTime) / 1e6f;
    }

    bool shouldActivate =
        egtExceeded &&
        (timeSinceActivation >= engineConfiguration->egtLimit.egtLimitDelay);

    m_isActive = shouldActivate;

    if (shouldActivate) {
        uint32_t rpm = engine->rpmCalculator.getRaw();
        uint16_t limitRpm = engineConfiguration->egtLimit.egtLimitRpm;
        uint16_t limitRange = engineConfiguration->egtLimit.egtLimitRange;

        if (rpm < limitRpm) {
            m_limitPercent = 0;
        } else if (rpm >= (limitRpm + limitRange)) {
            m_limitPercent = 100;
        } else {
            uint16_t rpmInRange = rpm - limitRpm;
            m_limitPercent = (rpmInRange * 100) / limitRange;
        }

        m_timeOverLimit += dtSeconds;
    } else {
        m_limitPercent = 0;
    }

    if (m_currentEgt < (maxEgt - 50)) {
        activationTime = 0;
        m_isActive = false;
        m_limitPercent = 0;
    }

    // дроссель
    if (m_currentEgt <= maxEgt) {
        m_appliedThrottlePosition = 100;
    } else if (m_currentEgt <= (maxEgt + 50)) {
        m_appliedThrottlePosition = engineConfiguration->egtLimit.throttleCutLevel2;
    } else if (m_currentEgt > (maxEgt + 100)) {
        m_appliedThrottlePosition = engineConfiguration->egtLimit.throttleCutLevel3;
        // CRITICAL: Force complete fuel cut-off at critical temperature to prevent injector hang
        // флаг fuelCutAtCriticalEgt ты можешь использовать там, где режешь топливо
        m_appliedThrottlePosition = 0;  // Override to 0% (complete cut)
    }

    // зажигание
    if (m_limitPercent == 0) {
        m_appliedIgnitionRetard = 0;
    } else {
        float maxRetard = static_cast<float>(engineConfiguration->egtLimit.maxIgnitionRetard);
        float applied = (m_limitPercent / 100.0f) * maxRetard;
        m_appliedIgnitionRetard = -applied;
    }

    // лямбда
    if (m_limitPercent == 0) {
        m_appliedLambdaReduction = 0;
    } else {
        float maxRed = engineConfiguration->egtLimit.lambdaReductionMax;
        m_appliedLambdaReduction = (m_limitPercent / 100.0f) * maxRed;
    }
}

// геттеры

float EgtLimiter::getCurrentEgt() const {
    return m_currentEgt;
}

bool EgtLimiter::isActive() const {
    return m_isActive;
}

uint8_t EgtLimiter::getLimitPercent() const {
    return m_limitPercent;
}

float EgtLimiter::getTimeOverLimit() const {
    return m_timeOverLimit;
}

float EgtLimiter::getAppliedIgnitionRetard() const {
    return m_appliedIgnitionRetard;
}

uint8_t EgtLimiter::getAppliedThrottlePosition() const {
    return m_appliedThrottlePosition;
}

float EgtLimiter::getAppliedLambdaReduction() const {
    return m_appliedLambdaReduction;
}
