#include "egtLimiter.h"
#include "engine.h"
#include "sensor_reader.h"
#include "rusefi_hw_enums.h"

// Глобальный экземпляр, как у других контроллеров
EgtLimiter egtLimiter;

void initEgtLimiter() {
    egtLimiter.init();
}

EgtLimiter::EgtLimiter() {
}

void EgtLimiter::init() {
    limitActivationTime = 0;
    etbDesiredPosition = 100;

    engine->engineState.egtLimitState.currentEgt = 0;
    engine->engineState.egtLimitState.isActive = false;
    engine->engineState.egtLimitState.limitPercent = 0;
    engine->engineState.egtLimitState.timeOverLimit = 0;
    engine->engineState.egtLimitState.appliedIgnitionRetard = 0;
    engine->engineState.egtLimitState.appliedThrottlePosition = 100;
    engine->engineState.egtLimitState.appliedLambdaReduction = 0;
    engine->engineState.egtLimitState.criticalEgtReached = false;
}

void EgtLimiter::update(float dtSeconds) {
    // Выключено – всё сбрасываем
    if (!engineConfiguration->egtLimit.enabled) {
        engine->engineState.egtLimitState.isActive = false;
        engine->engineState.egtLimitState.limitPercent = 0;
        engine->engineState.egtLimitState.timeOverLimit = 0;
        engine->engineState.egtLimitState.appliedIgnitionRetard = 0;
        engine->engineState.egtLimitState.appliedThrottlePosition = 100;
        engine->engineState.egtLimitState.appliedLambdaReduction = 0;
        engine->engineState.egtLimitState.criticalEgtReached = false;
        etbDesiredPosition = 100;
        return;
    }

    updateEgtReading();

    int16_t currentEgt = engine->engineState.egtLimitState.currentEgt;
    int16_t maxEgt = engineConfiguration->egtLimit.maxEgtTemp;
    bool egtExceeded = currentEgt > maxEgt;

    // задержка активации
    if (egtExceeded && !engine->engineState.egtLimitState.isActive) {
        limitActivationTime = getTimeNowNt();
    }

    float timeSinceActivation = 0;
    if (limitActivationTime > 0) {
        timeSinceActivation = (getTimeNowNt() - limitActivationTime) / 1e6f;
    }

    bool shouldActivate =
        egtExceeded &&
        (timeSinceActivation >= engineConfiguration->egtLimit.egtLimitDelay);

    engine->engineState.egtLimitState.isActive = shouldActivate;

    if (shouldActivate) {
        uint8_t limitPercent = calculateLimitingCurve(getRpm());
        engine->engineState.egtLimitState.limitPercent = limitPercent;
        engine->engineState.egtLimitState.timeOverLimit += dtSeconds;
    } else {
        engine->engineState.egtLimitState.limitPercent = 0;
    }

    // гистерезис 50°C
    if (currentEgt < (maxEgt - 50)) {
        limitActivationTime = 0;
        engine->engineState.egtLimitState.isActive = false;
        engine->engineState.egtLimitState.limitPercent = 0;
    }

    // обновляем дроссель и остальные эффекты
    updateThrottleControl();
    updateIgnitionRetard();
    updateLambdaReduction();
}

void EgtLimiter::updateEgtReading() {
    // TODO: сюда повесить реальное чтение EGT
    // пока заглушка
    engine->engineState.egtLimitState.currentEgt = 0;
}

void EgtLimiter::updateThrottleControl() {
    int16_t currentEgt = engine->engineState.egtLimitState.currentEgt;
    int16_t maxEgt = engineConfiguration->egtLimit.maxEgtTemp;

    // зона 1 – норма
    if (currentEgt <= maxEgt) {
        etbDesiredPosition = 100;
        engine->engineState.egtLimitState.appliedThrottlePosition = 100;
        engine->engineState.egtLimitState.criticalEgtReached = false;
        return;
    }

    // зона 2 – среднее ограничение
    if (currentEgt <= (maxEgt + 50)) {
        etbDesiredPosition = engineConfiguration->egtLimit.throttleCutLevel2;
        engine->engineState.egtLimitState.appliedThrottlePosition = etbDesiredPosition;
        engine->engineState.egtLimitState.criticalEgtReached = false;
        return;
    }

    // зона 3 – критика
    if (currentEgt > (maxEgt + 100)) {
        etbDesiredPosition = engineConfiguration->egtLimit.throttleCutLevel3;
        engine->engineState.egtLimitState.appliedThrottlePosition = etbDesiredPosition;

        if (engineConfiguration->egtLimit.fuelCutAtCriticalEgt) {
            engine->engineState.egtLimitState.criticalEgtReached = true;
        } else {
            engine->engineState.egtLimitState.criticalEgtReached = false;
        }

        return;
    }
}

void EgtLimiter::updateIgnitionRetard() {
    uint8_t limitPercent = engine->engineState.egtLimitState.limitPercent;
    if (limitPercent == 0) {
        engine->engineState.egtLimitState.appliedIgnitionRetard = 0;
        return;
    }

    float maxRetard = static_cast<float>(engineConfiguration->egtLimit.maxIgnitionRetard);
    float applied = (limitPercent / 100.0f) * maxRetard;
    engine->engineState.egtLimitState.appliedIgnitionRetard = static_cast<int8_t>(-applied);
}

void EgtLimiter::updateLambdaReduction() {
    uint8_t limitPercent = engine->engineState.egtLimitState.limitPercent;
    if (limitPercent == 0) {
        engine->engineState.egtLimitState.appliedLambdaReduction = 0;
        return;
    }

    float maxRed = engineConfiguration->egtLimit.lambdaReductionMax;
    float applied = (limitPercent / 100.0f) * maxRed;
    engine->engineState.egtLimitState.appliedLambdaReduction = applied;
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
