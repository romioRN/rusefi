#include "pch.h"
#include "hella_oil_level.h"
#include "digital_input_exti.h"  

#if EFI_HELLA_OIL

//static int cb_num = 0;
//static float prevRise = 0;
//static float prevFall = 0;

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

static int cb_num = 0;
static float lastRiseMs = 0;
static float lastFallMs = 0;
static float lastTempEndMs = 0;  // время окончания последнего TEMP импульса
static bool waitingForNextTemp = false;

static void hellaOilCallback(efitick_t nowNt, bool value) {
    cb_num++;
    float callback_ms = nowNt / 1000.0f;

    if (value) { // RISE
        lastRiseMs = callback_ms;
        efiPrintf("CB #%d RISE @ %.3f ms", cb_num, callback_ms);
        
        // Если ждали следующий TEMP и пришел подходящий фронт
        if (waitingForNextTemp && lastTempEndMs > 0) {
            float level_interval = callback_ms - lastTempEndMs;
            efiPrintf("  Potential LEVEL interval: %.3f ms", level_interval);
            
            // Проверяем, что это действительно TEMP (следующий импульс должен быть ~10.6 мс)
            // Обработка LEVEL будет в FALL, если ширина подтвердит TEMP
        }
    }
    else { // FALL
        float width_ms = callback_ms - lastRiseMs;
        lastFallMs = callback_ms;
        
        efiPrintf("CB #%d FALL @ %.3f ms, width=%.3f ms", cb_num, callback_ms, width_ms);

        // Проверяем, является ли это TEMP импульсом
        if (width_ms > 3.0f && width_ms < 20.0f) {
            // Это TEMP импульс!
            float temp = interpolateClamped(
                engineConfiguration->hellaOilLevel.minPulseUsTemp / 1000.0f,  // 10000 us → 10 ms
                engineConfiguration->hellaOilLevel.minTempC,                   // например -40°C
                engineConfiguration->hellaOilLevel.maxPulseUsTemp / 1000.0f,  // 11000 us → 11 ms
                engineConfiguration->hellaOilLevel.maxTempC,                   // например +120°C
                width_ms
            );
            efiPrintf("  TEMP: width=%.3f ms, temp=%.3fC", width_ms, temp);
            
            // Сохраняем температуру
            tempValid = true;
            tempSensor.setValidValue(temp, nowNt);
            rawTempSensor.setValidValue(width_ms, nowNt);
            engineConfiguration->hellaOilLevel.tempC = temp;

            // Если был предыдущий TEMP, вычисляем LEVEL по интервалу
            if (waitingForNextTemp && lastTempEndMs > 0) {
                float level_interval = lastRiseMs - lastTempEndMs;  // интервал от конца пред. TEMP до начала этого
                
                efiPrintf("  LEVEL: interval=%.3f ms between TEMP impulses", level_interval);
                
                // Интерполируем уровень по интервалу
                float level = interpolateClamped(
                    engineConfiguration->hellaOilLevel.minPulseUsLevel / 1000.0f,  // 201000 us → 201 ms
                    engineConfiguration->hellaOilLevel.minLevelMm,                  // например 0 мм
                    engineConfiguration->hellaOilLevel.maxPulseUsLevel / 1000.0f,  // 203000 us → 203 ms
                    engineConfiguration->hellaOilLevel.maxLevelMm,                  // например 100 мм
                    level_interval
                );
                efiPrintf("  LEVEL: interval=%.3f ms, level=%.3fmm", level_interval, level);
                
                // Сохраняем уровень
                levelValid = true;
                levelSensor.setValidValue(level, nowNt);
                rawLevelSensor.setValidValue(level_interval, nowNt);
                engineConfiguration->hellaOilLevel.levelMm = level;
            }
            
            // Обновляем время окончания TEMP для следующего расчета LEVEL
            lastTempEndMs = callback_ms;
            waitingForNextTemp = true;
        }
        else if (width_ms > 21.0f && width_ms < 60.0f) {
            // Это DIAG импульс
            efiPrintf("  DIAG: width=%.3f ms (diagnostic)", width_ms);
            // DIAG не влияет на расчет LEVEL/TEMP
        }
        else {
            efiPrintf("  Unknown impulse: width=%.3f ms", width_ms);
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
