#include "egtLimiter.h"
#include "engine.h"
#include "sensor_reader.h"
#include "rusefi_hw_enums.h"


EgtLimiter egtLimiter;


EgtLimiter::EgtLimiter() : PeriodicController("EGT Limiter") {
    // 10ms period
}


void initEgtLimiter() {
    // Initialize EGT limiter settings from configuration
    egtLimiter.configure();
}


void EgtLimiter::onPeriodicCallback() {
    // Skip if disabled
    if (!engineConfiguration->egtLimit.enabled) {
        engine->engineState.egtLimitState.isActive = false;
        engine->engineState.egtLimitState.limitPercent = 0;
        etbDesiredPosition = 100;  // Keep throttle fully open
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
    
    // ← ДОБАВЛЯЕМ УПРАВЛЕНИЕ ДРОССЕЛЕМ
    updateThrottleControl();
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


// ← НОВАЯ ФУНКЦИЯ: УПРАВЛЕНИЕ ДРОССЕЛЕМ
void EgtLimiter::updateThrottleControl() {
    int16_t currentEgt = engine->engineState.egtLimitState.currentEgt;
    int16_t maxEgt = engineConfiguration->egtLimit.maxEgtTemp;
    
    // ЗОНА 1: Нормально (EGT < maxEgtTemp)
    if (currentEgt <= maxEgt) {
        etbDesiredPosition = 100;  // ✅ Дроссель ПОЛНОСТЬЮ открыт
        return;
    }
    
    // ЗОНА 2: Среднее ограничение (maxEgtTemp < EGT < maxEgtTemp+50°C)
    if (currentEgt <= (maxEgt + 50)) {
        // 🟡 СРЕДНЕЕ ОГРАНИЧЕНИЕ
        // Закрываем дроссель до параметра из конфигурации
        etbDesiredPosition = engineConfiguration->egtLimit.throttleCutLevel2;
        return;
    }
    
    // ЗОНА 3: Экстренное отключение (EGT > maxEgtTemp+100°C)
    if (currentEgt > (maxEgt + 100)) {
        // 🔴 ЭКСТРЕННОЕ ОГРАНИЧЕНИЕ
        // Закрываем дроссель до минимума
        etbDesiredPosition = engineConfiguration->egtLimit.throttleCutLevel3;
        
        // Дополнительно: режем топливо
        if (engineConfiguration->egtLimit.fuelCutAtCriticalEgt) {
            // Сигнал для fuel_computer.cpp
            engine->engineState.egtLimitState.criticalEgtReached = true;
        }
        return;
    }
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


// ← НОВАЯ ФУНКЦИЯ: ПОЛУЧИТЬ ЖЕЛАЕМУЮ ПОЗИЦИЮ ДРОССЕЛЯ
uint8_t EgtLimiter::getDesiredThrottlePosition() const {
    return etbDesiredPosition;
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
