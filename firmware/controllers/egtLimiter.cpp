#include "egtLimiter.h"
#include "engine.h"
#include "sensor_reader.h"
#include "rusefi_hw_enums.h"

EgtLimiter egtLimiter;

EgtLimiter::EgtLimiter() : PeriodicController("EGT Limiter") {
    // 10ms period
}

void EgtLimiter::onPeriodicCallback() {
    // Skip if disabled
    if (!engineConfiguration->egtLimit.enabled) {
        engine->engineState.egtLimitState.isActive = false;
        engine->engineState.egtLimitState.limitPercent = 0;
        return;
    }
    
    // Update EGT reading
    updateEgtReading();
    
    int16_t currentEgt = engine->engineState.egtLimitState.currentEgt;
    int16_t maxEgt = engineConfiguration->egtLimit.maxEgtTemp;
    bool egtExceeded = currentEgt > maxEgt;
    
    // Handle activation delay
    if (egtExceeded && !engine->engineState.egtLimitState.isActive) {
        limitActivationTime = getTimeNowNt();
    }
    
    // Check if delay time passed
    float timeSinceActivation = 0;
    if (limitActivationTime > 0) {
        timeSinceActivation = (getTimeNowNt() - limitActivationTime) / 1e6f;
    }
    
    // Activate if EGT still exceeded AND delay passed
    bool shouldActivate = egtExceeded && 
                         (timeSinceActivation >= engineConfiguration->egtLimit.egtLimitDelay);
    
    engine->engineState.egtLimitState.isActive = shouldActivate;
    
    if (shouldActivate) {
        engine->engineState.egtLimitState.limitPercent = 
            calculateLimitingCurve(getRpm());
        engine->engineState.egtLimitState.timeOverLimit += 0.01f;
    } else {
        engine->engineState.egtLimitState.limitPercent = 0;
    }
    
    // Clear if EGT drops below threshold (hysteresis 50°C)
    if (currentEgt < (maxEgt - 50)) {
        limitActivationTime = 0;
        engine->engineState.egtLimitState.isActive = false;
    }
}

void EgtLimiter::updateEgtReading() {
    // TODO: Implement based on your sensor type
    // Placeholder implementation
    engine->engineState.egtLimitState.currentEgt = 0;
    
    // Examples for different sensor types:
    // 1. MAX31855 thermocouple via SPI:
    //    engine->engineState.egtLimitState.currentEgt = readEgtFromSpi(0);
    
    // 2. AEM X-Series via CAN:
    //    engine->engineState.egtLimitState.currentEgt = readEgtFromCan();
    
    // 3. Analog input via ADC:
    //    engine->engineState.egtLimitState.currentEgt = convertAdcToEgt(getAdcValue(EGT_CHANNEL));
}

bool EgtLimiter::isLimitActive() const {
    return engine->engineState.egtLimitState.isActive;
}

uint8_t EgtLimiter::getLimitingPercent() const {
    return engine->engineState.egtLimitState.limitPercent;
}

int16_t EgtLimiter::getCurrentEgt() const {
    return engine->engineState.egtLimitState.currentEgt;
}

uint8_t EgtLimiter::calculateLimitingCurve(uint16_t rpm) const {
    uint16_t limitRpm = engineConfiguration->egtLimit.egtLimitRpm;
    uint16_t limitRange = engineConfiguration->egtLimit.egtLimitRange;
    
    if (rpm < limitRpm) {
        return 0;
    }
    
    if (rpm >= (limitRpm + limitRange)) {
        return 100;
    }
    
    uint16_t rpmInRange = rpm - limitRpm;
    return (rpmInRange * 100) / limitRange;
}
