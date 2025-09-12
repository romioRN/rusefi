#include "pch.h"
#include "hella_oil_level.h"

#if EFI_HELLA_OIL

static StoredValueSensor hellaOilLevelSensor(SensorType::HellaOilLevel, MS2NT(2000));
static StoredValueSensor hellaOilTempSensor(SensorType::HellaOilTemperature, MS2NT(2000));
static StoredValueSensor hellaOilLevelRawSensor(SensorType::HellaOilLevelRawPulse, MS2NT(2000));
static StoredValueSensor hellaOilTempRawSensor(SensorType::HellaOilTempRawPulse, MS2NT(2000));

static Timer hellaPulseTimer, hellaBetweenPulseTimer;
static enum class NextPulse { None, Temp, Level, Diag } nextPulse = NextPulse::None;

static float lastTempC = 0.0f;
static float lastLevelMm = 0.0f;
static uint32_t lastPulseWidthTempUs = 0;
static uint32_t lastPulseWidthLevelUs = 0;
static bool tempValid = false;
static bool levelValid = false;

#if EFI_PROD_CODE
static void hellaOilCallback(efitick_t nowNt, bool value) {
    if (value) {
        hellaPulseTimer.reset(nowNt);
        float dt = hellaBetweenPulseTimer.getElapsedSecondsAndReset(nowNt);
        if (dt > .89f * .780f && dt < 1.11f * .780f) nextPulse = NextPulse::Temp;
        else if (dt > .89f * .110f && dt < 1.11f * .110f)
            nextPulse = (nextPulse == NextPulse::Temp ? NextPulse::Level :
                         nextPulse == NextPulse::Level ? NextPulse::Diag : NextPulse::None);
        else nextPulse = NextPulse::None;
    } else {
        float pulseMs = 1000.0f * hellaPulseTimer.getElapsedSeconds(nowNt);
        if (pulseMs < 20.0f || pulseMs > 100.0f) { nextPulse = NextPulse::None; return; }
        if (nextPulse == NextPulse::Temp) {
            lastPulseWidthTempUs = uint32_t(pulseMs * 1000.0f);
            lastTempC = interpolateClamped(
                engineConfiguration->hellaOilLevel.minPulseUsTemp / 1000.0f,
                engineConfiguration->hellaOilLevel.minTempC,
                engineConfiguration->hellaOilLevel.maxPulseUsTemp / 1000.0f,
                engineConfiguration->hellaOilLevel.maxTempC,
                pulseMs);
            tempValid = true;
            hellaOilTempSensor.setValidValue(lastTempC, nowNt);
            hellaOilTempRawSensor.setValidValue(float(lastPulseWidthTempUs), nowNt);
        } else if (nextPulse == NextPulse::Level) {
            lastPulseWidthLevelUs = uint32_t(pulseMs * 1000.0f);
            lastLevelMm = interpolateClamped(
                engineConfiguration->hellaOilLevel.minPulseUsLevel / 1000.0f,
                engineConfiguration->hellaOilLevel.minLevelMm,
                engineConfiguration->hellaOilLevel.maxPulseUsLevel / 1000.0f,
                engineConfiguration->hellaOilLevel.maxLevelMm,
                pulseMs);
            levelValid = true;
            hellaOilLevelSensor.setValidValue(lastLevelMm, nowNt);
            hellaOilLevelRawSensor.setValidValue(float(lastPulseWidthLevelUs), nowNt);
            // sync to config for TS
            auto &cfg = engineConfiguration->hellaOilLevel;
            cfg.levelMm = lastLevelMm;
            cfg.tempC = lastTempC;
            cfg.rawPulseUsLevel = lastPulseWidthLevelUs;
            cfg.rawPulseUsTemp = lastPulseWidthTempUs;
        }
    }
}

static Gpio hellaPin = Gpio::Unassigned;
static void hellaExtiCallback(void*, efitick_t nowNt) {
    hellaOilCallback(nowNt, efiReadPin(hellaPin) ^ engineConfiguration->hellaOilLevelInverted);
}
#endif // EFI_PROD_CODE

void initHellaOilLevelSensor(bool isFirstTime) {
#if EFI_PROD_CODE
    if (!isBrainPinValid(engineConfiguration->hellaOilLevelPin)) return;
    if (efiExtiEnablePin("hellaOil", engineConfiguration->hellaOilLevelPin,
        PAL_EVENT_MODE_BOTH_EDGES, hellaExtiCallback, nullptr) < 0) return;
    hellaPin = engineConfiguration->hellaOilLevelPin;
    if (isFirstTime) {
        addConsoleAction("hellainfo", []() {
            efiPrintf("HellaOil Level=%.1fmm %s Temp=%.1f°C %s RawL=%dμs RawT=%dμs",
                lastLevelMm, levelValid?"OK":"!", lastTempC, tempValid?"OK":"!",
                lastPulseWidthLevelUs, lastPulseWidthTempUs);
        });
    }
#endif
    hellaOilLevelSensor.Register();
    hellaOilTempSensor.Register();
    hellaOilLevelRawSensor.Register();
    hellaOilTempRawSensor.Register();
}

void deInitHellaOilLevelSensor() {
    hellaOilLevelSensor.unregister();
    hellaOilTempSensor.unregister();
    hellaOilLevelRawSensor.unregister();
    hellaOilTempRawSensor.unregister();
#if EFI_PROD_CODE
    if (isBrainPinValid(hellaPin)) efiExtiDisablePin(hellaPin);
    hellaPin = Gpio::Unassigned;
#endif
}

float getHellaOilLevelMm()     { return lastLevelMm; }
float getHellaOilTempC()       { return lastTempC; }
bool  isHellaOilLevelValid()   { return levelValid; }
bool  isHellaOilTempValid()    { return tempValid; }
uint32_t getHellaOilLevelRawUs(){ return lastPulseWidthLevelUs; }
uint32_t getHellaOilTempRawUs() { return lastPulseWidthTempUs; }

#else // EFI_HELLA_OIL

void initHellaOilLevelSensor(bool) {}
void deInitHellaOilLevelSensor() {}
float getHellaOilLevelMm()     { return 0; }
float getHellaOilTempC()       { return 0; }
bool  isHellaOilLevelValid()   { return false; }
bool  isHellaOilTempValid()    { return false; }
uint32_t getHellaOilLevelRawUs(){ return 0; }
uint32_t getHellaOilTempRawUs() { return 0; }

#endif // EFI_HELLA_OIL
