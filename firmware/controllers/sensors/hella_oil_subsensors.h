#pragma once
#include "stored_value_sensor.h"

class HellaOilTempSensor : public StoredValueSensor {
public:
    HellaOilTempSensor() : StoredValueSensor(SensorType::HellaOilTemperature, MS2NT(2000)) {}
    void setValue(float temp, efitick_t nowNt) {
        setValidValue(temp, nowNt);
    }
};

class HellaOilLevelRawPulseSensor : public StoredValueSensor {
public:
    HellaOilLevelRawPulseSensor() : StoredValueSensor(SensorType::HellaOilLevelRawPulse, MS2NT(2000)) {}
    void setValue(float pulseUs, efitick_t nowNt) {
        setValidValue(pulseUs, nowNt);
    }
};

class HellaOilTempRawPulseSensor : public StoredValueSensor {
public:
    HellaOilTempRawPulseSensor() : StoredValueSensor(SensorType::HellaOilTempRawPulse, MS2NT(2000)) {}
    void setValue(float pulseUs, efitick_t nowNt) {
        setValidValue(pulseUs, nowNt);
    }
};
