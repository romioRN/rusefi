#pragma once

#include "rusefi_types.h"
#include "periodic_controller.h"

/**
 * @brief EGT-based RPM Limiter
 * 
 * Controls RPM by monitoring Exhaust Gas Temperature
 * Similar to knock control and temperature-based limiting
 */
class EgtLimiter : public PeriodicController {
public:
    EgtLimiter();
    
    void onPeriodicCallback() override;
    
    bool isLimitActive() const;
    uint8_t getLimitingPercent() const;
    int16_t getCurrentEgt() const;

private:
    efitick_t limitActivationTime = 0;
    
    void updateEgtReading();
    uint8_t calculateLimitingCurve(uint16_t rpm) const;
};

extern EgtLimiter egtLimiter;
