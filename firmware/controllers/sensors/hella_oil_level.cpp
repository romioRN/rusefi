#include "pch.h"
#include "hella_oil_level.h"
#include "digital_input_exti.h"
#include "hella_oil_subsensors.h"
#include "engine_configuration.h"

#if EFI_HELLA_OIL

// Объекты для сырых каналов и температуры
static HellaOilTempSensor hellaOilTempSensor;
static HellaOilLevelRawPulseSensor hellaOilLevelRawPulseSensor;
static HellaOilTempRawPulseSensor hellaOilTempRawPulseSensor;

// Основной объект сенсора уровня масла
HellaOilLevelSensor hellaSensor(SensorType::HellaOilLevel);

HellaOilLevelSensor::HellaOilLevelSensor(SensorType sensorType)
    : StoredValueSensor(sensorType, MS2NT(2000)) {
    // Инициализация калибровочных значений по умолчанию
    updateCalibrationFromConfig();
}

void HellaOilLevelSensor::init(brain_pin_e pin) {
    if (!isBrainPinValid(pin))
        return;
    m_pin = pin;
    
    // Обновляем калибровку из конфигурации
    updateCalibrationFromConfig();
    
    // Регистрируем все подсенсоры
    hellaOilTempSensor.Register();
    hellaOilLevelRawPulseSensor.Register();
    hellaOilTempRawPulseSensor.Register();
    
    Register();
}

void HellaOilLevelSensor::deInit() {
    // Отключаем все подсенсоры
    hellaOilTempSensor.unregister();
    hellaOilLevelRawPulseSensor.unregister();
    hellaOilTempRawPulseSensor.unregister();
    
    unregister();
    m_pin = Gpio::Unassigned;
}

// Главная обработка фронтов и спадов через value (HIGH/LOW)
void HellaOilLevelSensor::onEdge(efitick_t nowNt, bool value) {
    if (value) {
        // Rising edge: запоминаем время для измерения скважности и паузы между импульсами
        m_pulseTimer.reset(nowNt);
        float timeBetweenPulses = m_betweenPulseTimer.getElapsedSecondsAndReset(nowNt);
        
        // Определяем тип следующего импульса по паузе между импульсами
        if (timeBetweenPulses > 0.89f * 0.780f && timeBetweenPulses < 1.11f * 0.780f) {
            m_nextPulse = NextPulse::Temp;
        } else if (timeBetweenPulses > 0.89f * 0.110f && timeBetweenPulses < 1.11f * 0.110f) {
            switch (m_nextPulse) {
                case NextPulse::Temp: m_nextPulse = NextPulse::Level; break;
                case NextPulse::Level: m_nextPulse = NextPulse::Diag; break;
                default: m_nextPulse = NextPulse::None; break;
            }
        } else {
            m_nextPulse = NextPulse::None;
        }
    } else {
        // Falling edge: измеряем длительность импульса
        float lastPulseMs = 1000.0f * m_pulseTimer.getElapsedSeconds(nowNt);
        
        // Проверяем валидность импульса
        if (lastPulseMs > 100.0f || lastPulseMs < 20.0f) {
            // Невалидные импульсы
            m_nextPulse = NextPulse::None;
            return;
        }
        
        if (m_nextPulse == NextPulse::Diag) {
            // TODO: обработка диагностического импульса (если нужно)
            return;
        } else if (m_nextPulse == NextPulse::Temp) {
            // Обработка импульса температуры
            m_lastPulseWidthTempUs = lastPulseMs * 1000.0f;
            
            // Используем калибровочные параметры из конфигурации
            float tempC = interpolateClamped(m_tempMinPulseMs, m_tempMinC, 
                                           m_tempMaxPulseMs, m_tempMaxC, lastPulseMs);
            
            setTemp(tempC, true);
            setValidValue(tempC, nowNt);
            
            // Обновляем отдельные сенсоры
            hellaOilTempSensor.setValue(tempC, nowNt);
            hellaOilTempRawPulseSensor.setValue(m_lastPulseWidthTempUs, nowNt);
            
        } else if (m_nextPulse == NextPulse::Level) {
            // Обработка импульса уровня масла
            m_lastPulseWidthLevelUs = lastPulseMs * 1000.0f;
            
            // Используем калибровочные параметры из конфигурации
            float levelMm = interpolateClamped(m_levelMinPulseMs, m_levelMinMm, 
                                             m_levelMaxPulseMs, m_levelMaxMm, lastPulseMs);
            
            setLevel(levelMm, true);
            setValidValue(levelMm, nowNt);
            
            hellaOilLevelRawPulseSensor.setValue(m_lastPulseWidthLevelUs, nowNt);
        }
    }
}

float HellaOilLevelSensor::getLevelMm() const { 
    return m_levelMm; 
}

float HellaOilLevelSensor::getTempC() const { 
    return m_tempC; 
}

bool HellaOilLevelSensor::isLevelValid() const { 
    return m_levelValid; 
}

bool HellaOilLevelSensor::isTempValid() const { 
    return m_tempValid; 
}

int32_t HellaOilLevelSensor::getLevelRawPulseUs() const { 
    return static_cast<int32_t>(m_lastPulseWidthLevelUs); 
}

int32_t HellaOilLevelSensor::getTempRawPulseUs() const { 
    return static_cast<int32_t>(m_lastPulseWidthTempUs); 
}

void HellaOilLevelSensor::setLevel(float level, bool valid) { 
    m_levelMm = level; 
    m_levelValid = valid; 
}

void HellaOilLevelSensor::setTemp(float temp, bool valid) { 
    m_tempC = temp; 
    m_tempValid = valid; 
}

// Калибровочные методы для уровня масла
void HellaOilLevelSensor::setLevelCalibration(float minPulseMs, float maxPulseMs, 
                                             float minLevelMm, float maxLevelMm) {
    m_levelMinPulseMs = minPulseMs;
    m_levelMaxPulseMs = maxPulseMs;
    m_levelMinMm = minLevelMm;
    m_levelMaxMm = maxLevelMm;
}

void HellaOilLevelSensor::getLevelCalibration(float& minPulseMs, float& maxPulseMs, 
                                             float& minLevelMm, float& maxLevelMm) const {
    minPulseMs = m_levelMinPulseMs;
    maxPulseMs = m_levelMaxPulseMs;
    minLevelMm = m_levelMinMm;
    maxLevelMm = m_levelMaxMm;
}

// Калибровочные методы для температуры
void HellaOilLevelSensor::setTempCalibration(float minPulseMs, float maxPulseMs, 
                                            float minTempC, float maxTempC) {
    m_tempMinPulseMs = minPulseMs;
    m_tempMaxPulseMs = maxPulseMs;
    m_tempMinC = minTempC;
    m_tempMaxC = maxTempC;
}

void HellaOilLevelSensor::getTempCalibration(float& minPulseMs, float& maxPulseMs, 
                                            float& minTempC, float& maxTempC) const {
    minPulseMs = m_tempMinPulseMs;
    maxPulseMs = m_tempMaxPulseMs;
    minTempC = m_tempMinC;
    maxTempC = m_tempMaxC;
}

// Обновление калибровки из конфигурации
void HellaOilLevelSensor::updateCalibrationFromConfig() {
    // Загружаем калибровочные данные из engine configuration
    if (engineConfiguration != nullptr) {
        setLevelCalibration(
            engineConfiguration->hellaOilLevelMinPulseMs,
            engineConfiguration->hellaOilLevelMaxPulseMs,
            engineConfiguration->hellaOilLevelMinMm,
            engineConfiguration->hellaOilLevelMaxMm
        );
        
        setTempCalibration(
            engineConfiguration->hellaOilTempMinPulseMs,
            engineConfiguration->hellaOilTempMaxPulseMs,
            engineConfiguration->hellaOilTempMinC,
            engineConfiguration->hellaOilTempMaxC
        );
    }
}

#endif // EFI_HELLA_OIL
