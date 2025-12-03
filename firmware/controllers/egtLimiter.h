#pragma once

#include "periodic_controller.h"
#include "engine.h"

class EgtLimiter : public PeriodicController {
private:
    uint64_t limitActivationTime = 0;
    
    // Throttle control variables
    uint8_t etbDesiredPosition = 100;  // 0-100%
    
public:
    EgtLimiter();
    
    void onPeriodicCallback() override;
    void updateEgtReading();
    void updateThrottleControl();      // ← НОВАЯ ФУНКЦИЯ
    
    bool isLimitActive() const;
    uint8_t getLimitingPercent() const;
    int16_t getCurrentEgt() const;
    uint8_t getDesiredThrottlePosition() const;  // ← НОВАЯ ФУНКЦИЯ
    
    uint8_t calculateLimitingCurve(uint16_t rpm) const;
};

extern EgtLimiter egtLimiter;
