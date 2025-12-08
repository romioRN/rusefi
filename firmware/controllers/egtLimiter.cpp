#include "egtLimiter.h"
#include "engine.h"
#include "sensor_reader.h"
#include "rusefi_hw_enums.h"

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

    auto &st = engine->engineState.egtLimitState;
    st.currentEgt = 0;
    st.isActive = false;
    st.limitPercent = 0;
    st.timeOverLimit = 0;
    st.appliedIgnitionRetard = 0;
    st.appliedThrottlePosition = 100;
    st.appliedLambdaReduction = 0;
    st.criticalEgtReached = false;
}

void EgtLimiter::update(float dtSeconds) {
    if (!engineConfiguration->egtLimit.enabled) {
        m_isActive = false;
        m_limitPercent = 0;
        m_timeOverLimit = 0;
        m_appliedIgnitionRetard = 0;
        m_appliedThrottlePosition = 100;
        m_appliedLambdaReduction = 0;

        auto &st = engine->engineState.egtLimitState;
        st.isActive = false;
        st.limitPercent = 0;
        st.timeOverLimit = 0;
        st.appliedIgnitionRetard = 0;
        st.appliedThrottlePosition = 100;
        st.appliedLambdaReduction = 0;
        st.criticalEgtReached = false;

        return;
    }

    computeLimits(dtSeconds);

    // синхронизируем m_* в engineState (для OutputChannels)
    auto &st = engine->engineState.egtLimitState;
    st.currentEgt = static_cast<int16_t>(m_currentEgt);
    st.isActive = m_isActive;
    st.limitPercent = m_limitPercent;
    st.timeOverLimit = m_timeOverLimit;
    st.appliedIgnitionRetard = static_cast<int8_t>(m_appliedIgnitionRetard);
    st.appliedThrottlePosition = m_appliedThrottlePosition;
    st.appliedLambdaReduction = m_appliedLambdaReduction;
}

void EgtLimiter::computeLimits(float dtSeconds) {
    // 1. Обновление EGT
    // TODO: реальное чтение датчика, пока заглушка
    m_currentEgt = 0.0f;

    int16_t maxEgt = engineConfiguration->egtLimit.maxEgtTemp;
    bool egtExceeded = m_currentEgt > maxEgt;

    static efitime_t activationTime = 0;

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
        uint16_t rpm = getRpm();
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
        engine->engineState.egtLimitState.criticalEgtReached = false;
    } else if (m_currentEgt <= (maxEgt + 50)) {
        m_appliedThrottlePosition = engineConfiguration->egtLimit.throttleCutLevel2;
        engine->engineState.egtLimitState.criticalEgtReached = false;
    } else if (m_currentEgt > (maxEgt + 100)) {
        m_appliedThrottlePosition = engineConfiguration->egtLimit.throttleCutLevel3;

        if (engineConfiguration->egtLimit.fuelCutAtCriticalEgt) {
            engine->engineState.egtLimitState.criticalEgtReached = true;
        } else {
            engine->engineState.egtLimitState.criticalEgtReached = false;
        }
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
