#pragma once

#include "stored_value_sensor.h"
#include "timer.h"

class HellaOilLevelSensor : public StoredValueSensor {
public:
    explicit HellaOilLevelSensor(SensorType sensorType = SensorType::HellaOilLevel);

    // Инициализация с регистрацией сенсора
    void init(brain_pin_e pin);
    void deInit();

    // Обработчик прерывания 
    void onEdge(efitick_t nowNt, bool value);

    // Getters для калиброванных и сырых значений
    float getLevelMm() const { return m_levelMm; }
    float getTempC() const { return m_tempC; }
    bool isLevelValid() const { return m_levelValid; }
    bool isTempValid() const { return m_tempValid; }

    // Сырые значения в микросекундах  
    int32_t getLevelRawPulseUs() const { return static_cast<int32_t>(m_lastPulseWidthLevelUs); }
    int32_t getTempRawPulseUs() const { return static_cast<int32_t>(m_lastPulseWidthTempUs); }

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

// Глобальный экземпляр
extern HellaOilLevelSensor hellaSensor;
