#pragma once

#include "stored_value_sensor.h"
#include "hella_oil_subsensors.h"

class HellaOilLevelSensor : public StoredValueSensor {
public:
    explicit HellaOilLevelSensor(SensorType sensorType = SensorType::HellaOilLevel);

    void init(brain_pin_e pin);
    void deInit();
    void onEdge(efitick_t nowNt);

    float getLevelMm() const;
    float getTempC() const;

    bool isLevelValid() const;
    bool isTempValid() const;

    int32_t getLevelRawPulseUs() const;
    int32_t getTempRawPulseUs() const;

private:
    brain_pin_e m_pin = Gpio::Unassigned;
    Timer m_pulseTimer;
    Timer m_betweenPulseTimer;

    enum class NextPulse { None, Temp, Level, Diag };
    NextPulse m_nextPulse = NextPulse::None;

    float m_lastPulseWidthTempUs = 0.0f;
    float m_lastPulseWidthLevelUs = 0.0f;

    float m_levelMm = 0.0f;
    float m_tempC = 0.0f;

    bool m_levelValid = false;
    bool m_tempValid = false;

    void setLevel(float level, bool valid);
    void setTemp(float temp, bool valid);
};
