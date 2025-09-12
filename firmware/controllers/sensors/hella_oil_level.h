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

    // Калибровочные параметры для уровня масла (по двум точкам)
    void setLevelCalibration(float minPulseMs, float maxPulseMs, float minLevelMm, float maxLevelMm);
    void getLevelCalibration(float& minPulseMs, float& maxPulseMs, float& minLevelMm, float& maxLevelMm) const;

    // Калибровочные параметры для температуры
    void setTempCalibration(float minPulseMs, float maxPulseMs, float minTempC, float maxTempC);
    void getTempCalibration(float& minPulseMs, float& maxPulseMs, float& minTempC, float& maxTempC) const;

    // Обновление калибровки из конфигурации
    void updateCalibrationFromConfig();

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

    // Калибровочные переменные для уровня
    float m_levelMinPulseMs = 23.0f;   // мин. длительность импульса (мс)
    float m_levelMaxPulseMs = 87.86f;  // макс. длительность импульса (мс)
    float m_levelMinMm = 0.0f;         // мин. уровень (мм)
    float m_levelMaxMm = 150.0f;       // макс. уровень (мм)

    // Калибровочные переменные для температуры
    float m_tempMinPulseMs = 23.0f;    // мин. длительность импульса (мс)
    float m_tempMaxPulseMs = 87.0f;    // макс. длительность импульса (мс)
    float m_tempMinC = -40.0f;         // мин. температура (°C)
    float m_tempMaxC = 160.0f;         // макс. температура (°C)

    void setLevel(float level, bool valid);
    void setTemp(float temp, bool valid);
};

// Глобальный экземпляр для доступа из других модулей
extern HellaOilLevelSensor hellaSensor;
