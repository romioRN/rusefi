#pragma once

#include "stored_value_sensor.h"
#include "hella_oil_subsensors.h"

class HellaOilLevelSensor : public StoredValueSensor {
public:
    explicit HellaOilLevelSensor(SensorType sensorType = SensorType::HellaOilLevel);

    // Инициализация с передачей пина (EXTI) и регистрация сенсора
    void init(brain_pin_e pin);

    // Очистка ресурсов пина и дерегистрация
    void deInit();

    // Обработчик прерывания с передачей времени и уровня сигнала (true = High, false = Low)
    void onEdge(efitick_t nowNt, bool value);

    // Получение уровня масла в мм
    float getLevelMm() const;

    // Получение температуры масла в градусах Цельсия
    float getTempC() const;

    // Флаги валидности измерений
    bool isLevelValid() const;
    bool isTempValid() const;

    // Сырые значения импульсов (микросекунды)
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

