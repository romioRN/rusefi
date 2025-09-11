#include "pch.h"
#include "hella_oil_level.h"
#include "digital_input_exti.h"
#include "hella_oil_subsensors.h"

#if EFI_HELLA_OIL

// Объекты для сырых каналов и температуры
static HellaOilTempSensor hellaOilTempSensor;
static HellaOilLevelRawPulseSensor hellaOilLevelRawPulseSensor;
static HellaOilTempRawPulseSensor hellaOilTempRawPulseSensor;

// Основной объект сенсора уровня масла
HellaOilLevelSensor hellaSensor(SensorType::HellaOilLevel);

HellaOilLevelSensor::HellaOilLevelSensor(SensorType sensorType)
    : StoredValueSensor(sensorType, MS2NT(2000)) {}

void HellaOilLevelSensor::init(brain_pin_e pin) {
    if (!isBrainPinValid(pin))
        return;
    m_pin = pin;
    Register();
}

void HellaOilLevelSensor::deInit() {
    m_pin = Gpio::Unassigned;
}

// Главная обработка фронтов и спадов через value (HIGH/LOW)
void HellaOilLevelSensor::onEdge(efitick_t nowNt, bool value) {
    if (value) {
        // Rising edge: запоминаем время для измерения скважности и паузы между импульсами
        m_pulseTimer.reset(nowNt);
        float timeBetweenPulses = m_betweenPulseTimer.getElapsedSecondsAndReset(nowNt);
        if (timeBetweenPulses > 0.89f * 0.780f && timeBetweenPulses < 1.11f * 0.780f)
            m_nextPulse = NextPulse::Temp;
        else if (timeBetweenPulses > 0.89f * 0.110f && timeBetweenPulses < 1.11f * 0.110f) {
            switch (m_nextPulse) {
                case NextPulse::Temp:  m_nextPulse = NextPulse::Level; break;
                case NextPulse::Level: m_nextPulse = NextPulse::Diag;  break;
                default:               m_nextPulse = NextPulse::None;  break;
            }
        } else {
            m_nextPulse = NextPulse::None;
        }
    } else {
        // Falling edge: измеряем длительность импульса
        float lastPulseMs = 1000.0f * m_pulseTimer.getElapsedSeconds(nowNt);
        if (lastPulseMs > 100.0f || lastPulseMs < 20.0f) { // невалидные импульсы
            m_nextPulse = NextPulse::None;
            return;
        }
        if (m_nextPulse == NextPulse::Diag) {
            // TODO: обработка диагностического импульса (если нужно)
            return;
        } else if (m_nextPulse == NextPulse::Temp) {
            m_lastPulseWidthTempUs = lastPulseMs * 1000.0f;
            float tempC = interpolateClamped(23.f, -40.f, 87.f, 160.f, lastPulseMs);
            setTemp(tempC, true);
            setValidValue(tempC, nowNt);
            // Обновляем отдельные сенсоры
            hellaOilTempSensor.setValue(tempC, nowNt);
            hellaOilTempRawPulseSensor.setValue(m_lastPulseWidthTempUs, nowNt);
        } else if (m_nextPulse == NextPulse::Level) {
            m_lastPulseWidthLevelUs = lastPulseMs * 1000.0f;
            float levelMm = interpolateClamped(23.f, 0.f, 87.86f, 150.f, lastPulseMs);
            setLevel(levelMm, true);
            setValidValue(levelMm, nowNt);
            hellaOilLevelRawPulseSensor.setValue(m_lastPulseWidthLevelUs, nowNt);
        }
    }
}

float HellaOilLevelSensor::getLevelMm() const { return m_levelMm; }
float HellaOilLevelSensor::getTempC() const { return m_tempC; }
bool HellaOilLevelSensor::isLevelValid() const { return m_levelValid; }
bool HellaOilLevelSensor::isTempValid() const { return m_tempValid; }
int32_t HellaOilLevelSensor::getLevelRawPulseUs() const { return static_cast<int32_t>(m_lastPulseWidthLevelUs); }
int32_t HellaOilLevelSensor::getTempRawPulseUs() const { return static_cast<int32_t>(m_lastPulseWidthTempUs); }
void HellaOilLevelSensor::setLevel(float level, bool valid) { m_levelMm = level; m_levelValid = valid; }
void HellaOilLevelSensor::setTemp(float temp, bool valid) { m_tempC = temp; m_tempValid = valid; }

#endif // EFI_HELLA_OIL
