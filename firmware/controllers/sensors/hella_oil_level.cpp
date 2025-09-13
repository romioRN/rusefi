#include "pch.h"
#include "hella_oil_level.h"
#include "digital_input_exti.h"  

#if EFI_HELLA_OIL

static int cb_num = 0;
static float prevRise = 0;
static float prevFall = 0;

static StoredValueSensor levelSensor(SensorType::HellaOilLevel, MS2NT(2000));
static StoredValueSensor tempSensor(SensorType::HellaOilTemperature, MS2NT(2000));
static StoredValueSensor rawLevelSensor(SensorType::HellaOilLevelRawPulse, MS2NT(2000));
static StoredValueSensor rawTempSensor(SensorType::HellaOilTempRawPulse, MS2NT(2000));

static Timer pulseTimer, betweenTimer;
//1static enum class NextPulse { None, Temp, Level, Diag } nextPulse = NextPulse::None;

static float lastLevelMm = 0.0f;
static float lastTempC = 0.0f;
static uint32_t lastPulseWidthLevelUs = 0;
static uint32_t lastPulseWidthTempUs = 0;
static bool levelValid = false;
static bool tempValid = false;

#if EFI_PROD_CODE
static Gpio hellaPin = Gpio::Unassigned;

static float lastRiseMs = 0;
static float lastFallMs = 0;



static void hellaOilCallback(efitick_t nowNt, bool value) {
    cb_num++;
    float callback_ms = nowNt / 1000.0f;

    if (value) { // RISE фронт
        float pause_ms = callback_ms - lastFallMs;
        lastRiseMs = callback_ms;
        if (pause_ms > 90.0f && pause_ms < 500.0f) {
            float level = interpolateClamped(
                engineConfiguration->hellaOilLevel.minPulseUsLevel / 1000.0f,
                engineConfiguration->hellaOilLevel.minLevelMm,
                engineConfiguration->hellaOilLevel.maxPulseUsLevel / 1000.0f,
                engineConfiguration->hellaOilLevel.maxLevelMm,
                pause_ms
            );
            efiPrintf("LEVEL: pause=%.3f ms, level=%.3fmm", pause_ms, level);
            levelValid = true;
            levelSensor.setValidValue(level, nowNt);
            rawLevelSensor.setValidValue(pause_ms, nowNt);
            engineConfiguration->hellaOilLevel.levelMm = level;
        }
    }
    else { // FALL
        float width_ms = callback_ms - lastRiseMs;
        lastFallMs = callback_ms;
        if (width_ms > 5.0f && width_ms < 15.0f) {
            float temp = interpolateClamped(
                engineConfiguration->hellaOilLevel.minPulseUsTemp / 1000.0f,
                engineConfiguration->hellaOilLevel.minTempC,
                engineConfiguration->hellaOilLevel.maxPulseUsTemp / 1000.0f,
                engineConfiguration->hellaOilLevel.maxTempC,
                width_ms
            );
            efiPrintf("TEMP: width=%.3f ms, temp=%.3fC", width_ms, temp);
            tempValid = true;
            tempSensor.setValidValue(temp, nowNt);
            rawTempSensor.setValidValue(width_ms, nowNt);
            engineConfiguration->hellaOilLevel.tempC = temp;
        } else if (width_ms > 20.0f && width_ms < 40.0f) {
            efiPrintf("DIAG: width=%.3f ms", width_ms);
        }
    }
}





static void hellaExtiCallback(void*, efitick_t nowNt) {
    hellaOilCallback(nowNt, efiReadPin(hellaPin) ^ engineConfiguration->hellaOilLevelInverted);
}
#endif // EFI_PROD_CODE


void initHellaOilLevelSensor(bool isFirstTime) {
    efiPrintf("***** HELLA INIT CALLED! isFirstTime=%d *****", isFirstTime);
    efiPrintf("***** EFI_HELLA_OIL = %d *****", EFI_HELLA_OIL);

#if EFI_HELLA_OIL
    efiPrintf("***** HELLA: Starting real initialization *****");

#if EFI_PROD_CODE
    if (!isBrainPinValid(engineConfiguration->hellaOilLevelPin)) {
        efiPrintf("***** HELLA ERROR: Pin not valid: %s *****", hwPortname(engineConfiguration->hellaOilLevelPin));
        return;
    }

    efiPrintf("***** HELLA: Pin is valid: %s *****", hwPortname(engineConfiguration->hellaOilLevelPin));

    if (efiExtiEnablePin("hellaOil", engineConfiguration->hellaOilLevelPin,
                        PAL_EVENT_MODE_BOTH_EDGES, hellaExtiCallback, nullptr) < 0) {
        efiPrintf("***** HELLA ERROR: Failed to enable EXTI! *****");
        return;
    }

    hellaPin = engineConfiguration->hellaOilLevelPin;
    efiPrintf("***** HELLA: EXTI enabled successfully! *****");

    if (isFirstTime) {
        addConsoleAction("hellainfo", []() {
            efiPrintf("HellaOil Level=%.1fmm[%s] Temp=%.1f°C[%s] RawL=%luμs RawT=%luμs",
                      lastLevelMm, levelValid ? "OK" : "NO",
                      lastTempC, tempValid ? "OK" : "NO",
                      (unsigned long)lastPulseWidthLevelUs, 
                      (unsigned long)lastPulseWidthTempUs);
        });
        efiPrintf("***** HELLA: Console command added! *****");
    }
#else
    efiPrintf("***** HELLA: EFI_PROD_CODE is disabled *****");
#endif // EFI_PROD_CODE

    levelSensor.Register();
    tempSensor.Register();
    rawLevelSensor.Register();
    rawTempSensor.Register();
    efiPrintf("***** HELLA: All sensors registered! *****");

#else
    efiPrintf("***** HELLA: EFI_HELLA_OIL is disabled *****");
#endif // EFI_HELLA_OIL
}



void deInitHellaOilLevelSensor() {
    levelSensor.unregister();
    tempSensor.unregister();
    rawLevelSensor.unregister();
    rawTempSensor.unregister();
    
#if EFI_PROD_CODE
    if (isBrainPinValid(hellaPin)) {
        efiExtiDisablePin(hellaPin);
    }
    hellaPin = Gpio::Unassigned;
#endif // EFI_PROD_CODE
}

// Функции доступа к данным
float getHellaOilLevelMm() { return lastLevelMm; }
float getHellaOilTempC() { return lastTempC; }
bool isHellaOilLevelValid() { return levelValid; }
bool isHellaOilTempValid() { return tempValid; }
uint32_t getHellaOilLevelRawUs() { return lastPulseWidthLevelUs; }
uint32_t getHellaOilTempRawUs() { return lastPulseWidthTempUs; }

#else
// Заглушки когда EFI_HELLA_OIL отключен
void initHellaOilLevelSensor(bool /*isFirstTime*/) {}
void deInitHellaOilLevelSensor() {}
float getHellaOilLevelMm() { return 0.0f; }
float getHellaOilTempC() { return 0.0f; }
bool isHellaOilLevelValid() { return false; }
bool isHellaOilTempValid() { return false; }
uint32_t getHellaOilLevelRawUs() { return 0; }
uint32_t getHellaOilTempRawUs() { return 0; }
#endif // EFI_HELLA_OIL
