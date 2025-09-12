#include "pch.h"
#include "hella_oil_level.h"
#include "digital_input_exti.h"  

#if EFI_HELLA_OIL

static StoredValueSensor levelSensor(SensorType::HellaOilLevel, MS2NT(2000));
static StoredValueSensor tempSensor(SensorType::HellaOilTemperature, MS2NT(2000));
static StoredValueSensor rawLevelSensor(SensorType::HellaOilLevelRawPulse, MS2NT(2000));
static StoredValueSensor rawTempSensor(SensorType::HellaOilTempRawPulse, MS2NT(2000));

static Timer pulseTimer, betweenTimer;
static enum class NextPulse { None, Temp, Level, Diag } nextPulse = NextPulse::None;

static float lastLevelMm = 0.0f;
static float lastTempC = 0.0f;
static uint32_t lastPulseWidthLevelUs = 0;
static uint32_t lastPulseWidthTempUs = 0;
static bool levelValid = false;
static bool tempValid = false;

#if EFI_PROD_CODE
static Gpio hellaPin = Gpio::Unassigned;

static void hellaOilCallback(efitick_t nowNt, bool value) {
    if (value) {
        pulseTimer.reset(nowNt);
        float dt = betweenTimer.getElapsedSecondsAndReset(nowNt);
        if (dt > 0.696f && dt < 0.864f) {
            nextPulse = NextPulse::Temp;
        } else if (dt > 0.098f && dt < 0.122f) {
            switch (nextPulse) {
                case NextPulse::Temp: nextPulse = NextPulse::Level; break;
                case NextPulse::Level: nextPulse = NextPulse::Diag; break;
                default: nextPulse = NextPulse::None; break;
            }
        } else {
            nextPulse = NextPulse::None;
        }
    } else {
        float ms = 1000.0f * pulseTimer.getElapsedSeconds(nowNt);
        if (ms < 20.0f || ms > 100.0f) { 
            nextPulse = NextPulse::None; 
            return; 
        }
        
        if (nextPulse == NextPulse::Temp) {
            lastPulseWidthTempUs = static_cast<uint32_t>(ms * 1000.0f);
            lastTempC = interpolateClamped(
                engineConfiguration->hellaOilLevel.minPulseUsTemp / 1000.0f,
                engineConfiguration->hellaOilLevel.minTempC,
                engineConfiguration->hellaOilLevel.maxPulseUsTemp / 1000.0f,
                engineConfiguration->hellaOilLevel.maxTempC,
                ms);
            tempValid = true;
            tempSensor.setValidValue(lastTempC, nowNt);
            rawTempSensor.setValidValue(static_cast<float>(lastPulseWidthTempUs), nowNt);
        } else if (nextPulse == NextPulse::Level) {
            lastPulseWidthLevelUs = static_cast<uint32_t>(ms * 1000.0f);
            lastLevelMm = interpolateClamped(
                engineConfiguration->hellaOilLevel.minPulseUsLevel / 1000.0f,
                engineConfiguration->hellaOilLevel.minLevelMm,
                engineConfiguration->hellaOilLevel.maxPulseUsLevel / 1000.0f,
                engineConfiguration->hellaOilLevel.maxLevelMm,
                ms);
            levelValid = true;
            levelSensor.setValidValue(lastLevelMm, nowNt);
            rawLevelSensor.setValidValue(static_cast<float>(lastPulseWidthLevelUs), nowNt);
            
            // Синхронизация с конфигурацией для TunerStudio
            engineConfiguration->hellaOilLevel.levelMm = lastLevelMm;
            engineConfiguration->hellaOilLevel.tempC = lastTempC;
            engineConfiguration->hellaOilLevel.rawPulseUsLevel = lastPulseWidthLevelUs;
            engineConfiguration->hellaOilLevel.rawPulseUsTemp = lastPulseWidthTempUs;
        }
    }
}

static void hellaExtiCallback(void*, efitick_t nowNt) {
    hellaOilCallback(nowNt, efiReadPin(hellaPin) ^ engineConfiguration->hellaOilLevelInverted);
}
#endif // EFI_PROD_CODE

void initHellaOilLevelSensor(bool isFirstTime) {
#if EFI_PROD_CODE
    if (!isBrainPinValid(engineConfiguration->hellaOilLevelPin)) {
        return;
    }
    
    if (efiExtiEnablePin("hellaOil", engineConfiguration->hellaOilLevelPin,
                        PAL_EVENT_MODE_BOTH_EDGES, hellaExtiCallback, nullptr) < 0) {
        return;
    }
    
    hellaPin = engineConfiguration->hellaOilLevelPin;
    
   if (isFirstTime) {
     addConsoleAction("hellainfo", []() {
        efiPrintf("HellaOil Level=%.1fmm[%s] Temp=%.1f°C[%s] RawL=%uμs RawT=%uμs",
                  lastLevelMm, levelValid ? "OK" : "NO",
                  lastTempC, tempValid ? "OK" : "NO",
                  lastPulseWidthLevelUs, lastPulseWidthTempUs);
     });
   }
#endif // EFI_PROD_CODE

    levelSensor.Register();
    tempSensor.Register();
    rawLevelSensor.Register();
    rawTempSensor.Register();
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
