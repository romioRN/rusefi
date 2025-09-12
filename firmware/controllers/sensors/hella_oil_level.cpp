#include "pch.h"
#include "hella_oil_level.h"
#include "allsensors.h"

#if EFI_HELLA_OIL

// Основной сенсор
HellaOilLevelSensor hellaSensor(SensorType::HellaOilLevel);

HellaOilLevelSensor::HellaOilLevelSensor(SensorType sensorType)
    : StoredValueSensor(sensorType, MS2NT(2000)) {
}

void HellaOilLevelSensor::init(brain_pin_e pin) {
    if (!isBrainPinValid(pin)) {
        return;
    }
    m_pin = pin;
    
    // Регистрируем основной сенсор
    Register();
    
    // Регистрируем функциональные сенсоры через лямбды (ТОЧНО КАК В FREQUENCY SENSOR)
    registerFunctionalSensor(SensorType::HellaOilTemperature, 
                            [this]() { return getTempC(); }, 
                            [this]() { return isTempValid(); });
                            
    registerFunctionalSensor(SensorType::HellaOilLevelRawPulse,
                            [this]() { return static_cast<float>(getLevelRawPulseUs()); }, 
                            [this]() { return isLevelValid(); });
                            
    registerFunctionalSensor(SensorType::HellaOilTempRawPulse,
                            [this]() { return static_cast<float>(getTempRawPulseUs()); }, 
                            [this]() { return isTempValid(); });
}

void HellaOilLevelSensor::deInit() {
    unregister();
    m_pin = Gpio::Unassigned;
}

void HellaOilLevelSensor::onEdge(efitick_t nowNt, bool value) {
    if (value) {
        // Rising edge: начинаем измерение импульса (КАК В FREQUENCY SENSOR)
        m_pulseTimer.reset(nowNt);
        
        // Измеряем время между импульсами (КАК В FREQUENCY SENSOR)
        float timeBetweenPulses = m_betweenPulseTimer.getElapsedSecondsAndReset(nowNt);
        
        // Определяем тип импульса по паузе между импульсами
        if (timeBetweenPulses > 0.89f * 0.780f && timeBetweenPulses < 1.11f * 0.780f) {
            m_nextPulse = NextPulse::Temp;
        } else if (timeBetweenPulses > 0.89f * 0.110f && timeBetweenPulses < 1.11f * 0.110f) {
            switch (m_nextPulse) {
                case NextPulse::Temp: 
                    m_nextPulse = NextPulse::Level; 
                    break;
                case NextPulse::Level: 
                    m_nextPulse = NextPulse::Diag; 
                    break;
                default: 
                    m_nextPulse = NextPulse::None; 
                    break;
            }
        } else {
            m_nextPulse = NextPulse::None;
        }
    } else {
        // Falling edge: конец импульса, измеряем длительность (КАК В FREQUENCY SENSOR)
        float lastPulseMs = 1000.0f * m_pulseTimer.getElapsedSeconds(nowNt);
        
        // Проверка валидности импульса
        if (lastPulseMs > 100.0f || lastPulseMs < 20.0f) {
            m_nextPulse = NextPulse::None;
            return;
        }
        
        if (m_nextPulse == NextPulse::Diag) {
            // Диагностический импульс - игнорируем
            return;
        } else if (m_nextPulse == NextPulse::Temp) {
            // Температурный импульс
            m_lastPulseWidthTempUs = lastPulseMs * 1000.0f;
            
            // Используем калибровку из конфигурации
            float tempC = interpolateClamped(
                engineConfiguration->hellaOilLevel.minPulseUsTemp / 1000.0f,
                engineConfiguration->hellaOilLevel.minTempC,
                engineConfiguration->hellaOilLevel.maxPulseUsTemp / 1000.0f, 
                engineConfiguration->hellaOilLevel.maxTempC,
                lastPulseMs);
                
            setTemp(tempC, true);
            
        } else if (m_nextPulse == NextPulse::Level) {
            // Импульс уровня масла
            m_lastPulseWidthLevelUs = lastPulseMs * 1000.0f;
            
            // Используем калибровку из конфигурации
            float levelMm = interpolateClamped(
                engineConfiguration->hellaOilLevel.minPulseUsLevel / 1000.0f,
                engineConfiguration->hellaOilLevel.minLevelMm, 
                engineConfiguration->hellaOilLevel.maxPulseUsLevel / 1000.0f,
                engineConfiguration->hellaOilLevel.maxLevelMm,
                lastPulseMs);
                
            setLevel(levelMm, true);
            setValidValue(levelMm, nowNt);
            
            // Синхронизируем с конфигурацией
            engineConfiguration->hellaOilLevel.levelMm = getLevelMm();
            engineConfiguration->hellaOilLevel.tempC = getTempC();
            engineConfiguration->hellaOilLevel.rawPulseUsLevel = getLevelRawPulseUs();
            engineConfiguration->hellaOilLevel.rawPulseUsTemp = getTempRawPulseUs();
        }
    }
}

void HellaOilLevelSensor::setLevel(float level, bool valid) {
    m_levelMm = level;
    m_levelValid = valid;
}

void HellaOilLevelSensor::setTemp(float temp, bool valid) {
    m_tempC = temp;
    m_tempValid = valid;
}

#endif // EFI_HELLA_OIL
