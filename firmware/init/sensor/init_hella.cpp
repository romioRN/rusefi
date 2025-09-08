#include "pch.h"
#include "init.h"
#include "hella_oil_level.h"
#include "digital_input_exti.h"

static HellaOilLevelSensor hellaSensor(SensorType::OilLevel);

#if EFI_PROD_CODE
static Gpio hellaPin = Gpio::Unassigned;

static void hellaExtiCallback(void*, efitick_t nowNt) {
    hellaSensor.onEdge(nowNt);
}
#endif

void initHellaOilLevelSensor(bool isFirstTime) {
#if EFI_PROD_CODE
    if (efiExtiEnablePin("hellaOilLevel", engineConfiguration->hellaOilLevelPin,
        PAL_EVENT_MODE_BOTH_EDGES, hellaExtiCallback, nullptr) < 0) {
        return;
    }
    hellaPin = engineConfiguration->hellaOilLevelPin;

    // перед регистрацией датчика инициализировать его с пином
    hellaSensor.init(hellaPin);
#endif

    // зарегистрировать сенсор в системе
    hellaSensor.Register();
}

void deInitHellaOilLevelSensor() {
    // де-регистрируем сенсор в системе
    hellaSensor.Unregister();

#if EFI_PROD_CODE
    if (!isBrainPinValid(hellaPin)) {
        return;
    }

    // деинициализировать датчик (удалить локальные ресурсы, если нужно)
    hellaSensor.deInit();

    efiExtiDisablePin(hellaPin);
    hellaPin = Gpio::Unassigned;
#endif
}