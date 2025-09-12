#include "pch.h"
#include "init.h"
#include "hella_oil_level.h"
#include "digital_input_exti.h"

#if EFI_HELLA_OIL

static Gpio hellaLevelPin = Gpio::Unassigned;
static bool hellaLevelInverted = false;

#if EFI_PROD_CODE
static void hellaOilLevelExtiCallback(void*, efitick_t nowNt) {
    bool value = efiReadPin(hellaLevelPin) ^ hellaLevelInverted;
    hellaSensor.onEdge(nowNt, value);
}
#endif

void initHellaOilLevelSensor(bool isFirstTime) {
    // Сначала деинициализируем если это не первый раз
    if (!isFirstTime) {
        deInitHellaOilLevelSensor();
    }

#if EFI_PROD_CODE
    // Проверяем валидность пина
    if (!isBrainPinValid(engineConfiguration->hellaOilLevelPin)) {
        criticalError("Hella oil level sensor: invalid pin configuration");
        return;
    }

    // Настраиваем внешнее прерывание
    if (efiExtiEnablePin("hellaOilLevel", engineConfiguration->hellaOilLevelPin,
                        PAL_EVENT_MODE_BOTH_EDGES, hellaOilLevelExtiCallback, nullptr) < 0) {
        criticalError("Hella oil level sensor: failed to enable EXTI");
        return;
    }

    hellaLevelPin = engineConfiguration->hellaOilLevelPin;
    hellaLevelInverted = engineConfiguration->hellaOilLevelInverted;
    
    efiPrintf("Hella oil level sensor initialized on pin %s", 
              hwPortname(engineConfiguration->hellaOilLevelPin));
#endif
    
    // Инициализируем сенсор
    hellaSensor.init(engineConfiguration->hellaOilLevelPin);
    
    // Обновляем калибровку из конфигурации
    hellaSensor.updateCalibrationFromConfig();
    
    efiPrintf("Hella oil level sensor calibration updated");
}

void deInitHellaOilLevelSensor() {
    // Деинициализируем сенсор
    hellaSensor.deInit();

#if EFI_PROD_CODE
    if (isBrainPinValid(hellaLevelPin)) {
        efiExtiDisablePin(hellaLevelPin);
        efiPrintf("Hella oil level sensor EXTI disabled");
    }
    
    hellaLevelPin = Gpio::Unassigned;
#endif
    
    efiPrintf("Hella oil level sensor deinitialized");
}

#else 

void initHellaOilLevelSensor(bool /*isFirstTime*/) {
    // Заглушка для случая когда EFI_HELLA_OIL отключен
}

void deInitHellaOilLevelSensor() {
    // Заглушка для случая когда EFI_HELLA_OIL отключен
}

#endif // EFI_HELLA_OIL
