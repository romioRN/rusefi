#include "pch.h"
#include "hella_oil_level.h"
#include "digital_input_exti.h"  

#if EFI_HELLA_OIL

static int cb_num = 0;
static float prevRise = 0;
static float prevFall = 0;
static float lastTempImpulseEnd = 0;  // время окончания последнего TEMP импульса

static StoredValueSensor levelSensor(SensorType::HellaOilLevel, MS2NT(2000));
static StoredValueSensor tempSensor(SensorType::HellaOilTemperature, MS2NT(2000));
static StoredValueSensor rawLevelSensor(SensorType::HellaOilLevelRawPulse, MS2NT(2000));
static StoredValueSensor rawTempSensor(SensorType::HellaOilTempRawPulse, MS2NT(2000));

static Timer pulseTimer, betweenTimer;
1static enum class NextPulse { None, Temp, Level, Diag } nextPulse = NextPulse::None;

static float lastLevelMm = 0.0f;
static float lastTempC = 0.0f;
static uint32_t lastPulseWidthLevelUs = 0;
static uint32_t lastPulseWidthTempUs = 0;
static bool levelValid = false;
static bool tempValid = false;

#if EFI_PROD_CODE
static Gpio hellaPin = Gpio::Unassigned;


static void hellaOilCallback(efitick_t nowNt, bool value) {
    cb_num++;
    float callback_ms = nowNt / 1000.0f;
    
    if (value) { // RISE
        float dt = callback_ms - prevRise;
        prevRise = callback_ms;
        efiPrintf("CB #%d RISE @ %.3f ms, dt=%.3f ms, FSM=%d", cb_num, callback_ms, dt, (int)nextPulse);
        
        // Если есть предыдущий TEMP импульс, вычисляем уровень по LOW паузе
        if (lastTempImpulseEnd > 0) {
            float low_pause = callback_ms - lastTempImpulseEnd;
            efiPrintf("  LOW pause=%.3f ms since last TEMP", low_pause);
            
            // LOW time пропорционален уровню масла (по документации)
            if (low_pause > 20.0f && low_pause < 200.0f) {  // разумный диапазон пауз
                float level = interpolateClamped(
                    engineConfiguration->hellaOilLevel.minPulseUsLevel / 1000.0f,  // min пауза мс
                    engineConfiguration->hellaOilLevel.minLevelMm,                  // min уровень
                    engineConfiguration->hellaOilLevel.maxPulseUsLevel / 1000.0f,  // max пауза мс
                    engineConfiguration->hellaOilLevel.maxLevelMm,                  // max уровень
                    low_pause
                );
                efiPrintf("  LEVEL: pause=%.3f ms, level=%.3fmm", low_pause, level);
                
                levelValid = true;
                levelSensor.setValidValue(level, nowNt);
                rawLevelSensor.setValidValue(low_pause, nowNt);
                engineConfiguration->hellaOilLevel.levelMm = level;
            }
        }
        
        pulseTimer.reset(nowNt);
        float dt_sec = betweenTimer.getElapsedSecondsAndReset(nowNt);
        float dt_ms = dt_sec * 1000.0f;
        efiPrintf("  RISE(dt_ms)=%.3f, FSM before=%d", dt_ms, (int)nextPulse);
        
        // Определяем тип следующего импульса
        if (dt_ms > 95.0f && dt_ms < 600.0f) {
            nextPulse = NextPulse::Temp;
            efiPrintf("  FSM set to TEMP: nextPulse=%d", (int)nextPulse);
        } else if (dt_ms > 35.0f && dt_ms < 95.0f) {
            nextPulse = NextPulse::Diag;  // или другая логика
            efiPrintf("  FSM set to DIAG: nextPulse=%d", (int)nextPulse);
        } else {
            nextPulse = NextPulse::None;
            efiPrintf("  FSM out of bounds, set NONE: nextPulse=%d", (int)nextPulse);
        }
    } 
    else { // FALL
        float width = callback_ms - prevFall;
        prevFall = callback_ms;
        efiPrintf("CB #%d FALL @ %.3f ms, width=%.3f ms, FSM=%d", cb_num, callback_ms, width, (int)nextPulse);
        
        float ms = 1000.0f * pulseTimer.getElapsedSeconds(nowNt);
        efiPrintf("  FALL(ms_width)=%.3f, FSM before=%d", ms, (int)nextPulse);
        
        if (ms < 1.0f || ms > 700.0f) {
            efiPrintf("  Impulse width %.3f out of range (1–700 ms), ignore!", ms);
            nextPulse = NextPulse::None;
            return;
        }
        
        if (nextPulse == NextPulse::Temp) {
            // HIGH time зависит от температуры масла (по документации)
            lastPulseWidthTempUs = static_cast<uint32_t>(ms * 1000.0f);
            lastTempC = interpolateClamped(
                engineConfiguration->hellaOilLevel.minPulseUsTemp / 1000.0f,
                engineConfiguration->hellaOilLevel.minTempC,
                engineConfiguration->hellaOilLevel.maxPulseUsTemp / 1000.0f,
                engineConfiguration->hellaOilLevel.maxTempC,
                ms);
            efiPrintf("  TEMP: HIGH=%.3f ms, temp=%.3fC", ms, lastTempC);
            
            tempValid = true;
            tempSensor.setValidValue(lastTempC, nowNt);
            rawTempSensor.setValidValue(static_cast<float>(lastPulseWidthTempUs), nowNt);
            engineConfiguration->hellaOilLevel.tempC = lastTempC;
            
            // Запоминаем время окончания TEMP импульса для расчета уровня
            lastTempImpulseEnd = callback_ms;
        } 
        else if (nextPulse == NextPulse::Diag) {
            efiPrintf("  DIAG: ms=%.3f (diagnostic)", ms);
        }
        else {
            efiPrintf("  Fall with FSM=%d: not recognized pulse, skip.", (int)nextPulse);
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
