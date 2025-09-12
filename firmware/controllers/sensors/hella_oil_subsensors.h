#pragma once
#include "stored_value_sensor.h"

/**
 * Вспомогательные сенсоры для датчика уровня масла Hella
 * Эти классы предоставляют отдельные каналы для:
 * - Температуры масла в градусах Цельсия
 * - Сырого значения импульса уровня в микросекундах  
 * - Сырого значения импульса температуры в микросекундах
 */

// Сенсор температуры масла (калиброванное значение в °C)
class HellaOilTempSensor : public StoredValueSensor {
public:
    HellaOilTempSensor() : StoredValueSensor(SensorType::HellaOilTemperature, MS2NT(2000)) {}
    
    void setValue(float temp, efitick_t nowNt) {
        setValidValue(temp, nowNt);
    }
    
    // Переопределяем для корректного отображения единиц измерения
    const char* getSensorName() const override {
        return "Hella Oil Temperature";
    }
};

// Сенсор сырого значения импульса уровня масла (в микросекундах)
class HellaOilLevelRawPulseSensor : public StoredValueSensor {
public:
    HellaOilLevelRawPulseSensor() : StoredValueSensor(SensorType::HellaOilLevelRawPulse, MS2NT(2000)) {}
    
    void setValue(float pulseUs, efitick_t nowNt) {
        setValidValue(pulseUs, nowNt);
    }
    
    const char* getSensorName() const override {
        return "Hella Oil Level Raw Pulse";
    }
};

// Сенсор сырого значения импульса температуры (в микросекундах)
class HellaOilTempRawPulseSensor : public StoredValueSensor {
public:
    HellaOilTempRawPulseSensor() : StoredValueSensor(SensorType::HellaOilTempRawPulse, MS2NT(2000)) {}
    
    void setValue(float pulseUs, efitick_t nowNt) {
        setValidValue(pulseUs, nowNt);
    }
    
    const char* getSensorName() const override {
        return "Hella Oil Temperature Raw Pulse";
    }
};

// Функции для получения доступа к сенсорам из других модулей
HellaOilTempSensor& getHellaOilTempSensor();
HellaOilLevelRawPulseSensor& getHellaOilLevelRawPulseSensor();  
HellaOilTempRawPulseSensor& getHellaOilTempRawPulseSensor();
