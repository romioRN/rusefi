#include "pch.h"
#include "init.h"
#include "hella_oil_level.h"
#include "digital_input_exti.h"

#if EFI_HELLA_OIL

static HellaOilLevelSensor hellaSensor(SensorType::HellaOilLevel);
static Gpio hellaOilLevelPin = Gpio::Unassigned;
static bool hellaOilLevelInverted = false;

#if EFI_PROD_CODE
static void hellaOilLevelExtiCallback(void*, efitick_t nowNt) {
    bool value = efiReadPin(hellaOilLevelPin) ^ hellaOilLevelInverted;
    hellaSensor.onEdge(nowNt, value);
}
#endif

void initHellaOilLevelSensor(bool isFirstTime) {
#if EFI_PROD_CODE
    if (!isBrainPinValid(engineConfiguration->hellaOilLevelPin))
        return;

    if (efiExtiEnablePin("hellaOilLevel", engineConfiguration->hellaOilLevelPin,
                         PAL_EVENT_MODE_BOTH_EDGES, hellaOilLevelExtiCallback, nullptr) < 0)
        return;

    hellaOilLevelPin = engineConfiguration->hellaOilLevelPin;
    hellaOilLevelInverted = engineConfiguration->hellaOilLevelInverted;
#endif
    
    hellaSensor.init(engineConfiguration->hellaOilLevelPin);
}

void deInitHellaOilLevelSensor() {
    hellaSensor.unregister();

#if EFI_PROD_CODE
    if (isBrainPinValid(hellaOilLevelPin))
        efiExtiDisablePin(hellaOilLevelPin);

    hellaOilLevelPin = Gpio::Unassigned;
}
#else
void initHellaOilLevelSensor(bool /*isFirstTime*/) {}
void deInitHellaOilLevelSensor() {}
#endif
#endif