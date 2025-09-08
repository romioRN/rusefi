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
#endif

    hellaSensor.Register();
}

void deInitHellaOilLevelSensor() {
    hellaSensor.unregister();

#if EFI_PROD_CODE
    if (!isBrainPinValid(hellaPin)) {
        return;
    }
    efiExtiDisablePin(hellaPin);
    hellaPin = Gpio::Unassigned;
#endif
}