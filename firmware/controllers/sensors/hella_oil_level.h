#pragma once

#include "stored_value_sensor.h"

class HellaOilLevelSensor : public StoredValueSensor {
public:
    HellaOilLevelSensor(SensorType type) : StoredValueSensor(type, MS2NT(2000)) {}

    void init(brain_pin_e pin);
    void deInit();

    void onEdge(efitick_t nowNt);

private:
    brain_pin_e m_pin = Gpio::Unassigned;

    // Таймер для измерения длительности импульса (rising -> falling)
    Timer m_pulseTimer;

    // Таймер для измерения времени между импульсами (rising -> rising)
    Timer m_betweenPulseTimer;

    enum class NextPulse { None, Temp, Level, Diag };
    NextPulse m_nextPulse = NextPulse::None;

    // Добавленные поля для длительности последних импульсов в микросекундах
    float lastPulseWidthTempUs = 0.0f;
    float lastPulseWidthLevelUs = 0.0f;
};
