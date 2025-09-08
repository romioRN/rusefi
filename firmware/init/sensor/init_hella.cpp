#include "pch.h"
#include "init.h"
#include "hella_oil_level.h"
#include "digital_input_exti.h"

#if EFI_HELLA_OIL
static HellaOilLevelSensor hellaSensor(SensorType::OilLevel);

void initHellaOilLevelSensor(bool isFirstTime) {
    // HellaOilLevelSensor::init() внутри включает EXTI и вызывает Register()
    // поэтому здесь достаточно только инициализации с пином
#if EFI_PROD_CODE
    hellaSensor.init(engineConfiguration->hellaOilLevelPin);
#endif
}

void deInitHellaOilLevelSensor() {
    // Сначала де-регистрируем в системе, затем освобождаем ресурсы пина
    hellaSensor.unregister();

#if EFI_PROD_CODE
    hellaSensor.deInit();
#endif
}
#else
// Если фича выключена, предоставить пустые заглушки (чтобы линковка была корректной)
void initHellaOilLevelSensor(bool /*isFirstTime*/) { }
void deInitHellaOilLevelSensor() { }
#endif