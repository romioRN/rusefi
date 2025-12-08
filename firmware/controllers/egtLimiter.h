#pragma once

#include "pch.h"  // как у других контроллеров

class EgtLimiter {
public:
    EgtLimiter() = default;

    void init();                       // вызвать при инициализации ECU
    void update(float dtSeconds);      // вызвать из fuel_computer/logic раз в цикл

    // геттеры состояния (для OutputChannels)
    float getCurrentEgt() const;
    bool  isActive() const;
    uint8_t getLimitPercent() const;
    float getTimeOverLimit() const;
    float getAppliedIgnitionRetard() const;
    uint8_t getAppliedThrottlePosition() const;
    float getAppliedLambdaReduction() const;

private:
    // внутреннее состояние, соответствующее egtLimitState_s
    float m_currentEgt = 0.0f;
    bool  m_isActive = false;
    uint8_t m_limitPercent = 0;
    float m_timeOverLimit = 0.0f;
    float m_appliedIgnitionRetard = 0.0f;
    uint8_t m_appliedThrottlePosition = 100;
    float m_appliedLambdaReduction = 0.0f;

    void computeLimits(float dtSeconds);
};