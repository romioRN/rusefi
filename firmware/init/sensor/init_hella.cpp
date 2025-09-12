#include "pch.h"
#include "init.h"
#include "hella_oil_level.h"
#include "digital_input_exti.h"

#if EFI_HELLA_OIL

static Gpio hellaLevelPin = Gpio::Unassigned;
static bool hellaLevelInverted = false;

#if EFI_PROD_CODE
// Внешний callback из hella_oil_level.cpp
extern void hellaExtiCallback(void*, efitick_t nowNt);
#endif

void initHellaOilLevelSensor(bool isFirstTime) {
    // Деинициализация при повторном вызове
    if (!isFirstTime) {
        deInitHellaOilLevelSensor();
    }

#if EFI_PROD_CODE
    if (!isBrainPinValid(engineConfiguration->hellaOilLevelPin)) {
        return;
    }

    if (efiExtiEnablePin("hellaOilLevel", engineConfiguration->hellaOilLevelPin,
                        PAL_EVENT_MODE_BOTH_EDGES, hellaExtiCallback, nullptr) < 0) {
        criticalError("Hella oil sensor: failed to enable EXTI");
        return;
    }

    hellaLevelPin = engineConfiguration->hellaOilLevelPin;
    hellaLevelInverted = engineConfiguration->hellaOilLevelInverted;
    
    efiPrintf("Hella oil level sensor initialized on pin %s", 
              hwPortname(engineConfiguration->hellaOilLevelPin));
#endif
    
    // Инициализируем сенсор (теперь это функция, а не метод класса)
    // Вся логика инициализации перенесена в hella_oil_level.cpp
}

void deInitHellaOilLevelSensor() {
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
