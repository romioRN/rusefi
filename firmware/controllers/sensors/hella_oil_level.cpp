#include "pch.h"
#include "hella_oil_level.h"

#if EFI_HELLA_OIL

// ========== СЕНСОРЫ (как в flex sensor) ==========
static StoredValueSensor hellaOilLevelSensor(SensorType::HellaOilLevel, MS2NT(2000));
static StoredValueSensor hellaOilTempSensor(SensorType::HellaOilTemperature, MS2NT(2000));
static StoredValueSensor hellaOilLevelRawSensor(SensorType::HellaOilLevelRawPulse, MS2NT(2000));
static StoredValueSensor hellaOilTempRawSensor(SensorType::HellaOilTempRawPulse, MS2NT(2000));

// ========== ТАЙМЕРЫ (как в frequency sensor) ==========
static Timer hellaPulseTimer, hellaBetweenPulseTimer;

// ========== СОСТОЯНИЕ ==========
static enum class NextPulse { None, Temp, Level, Diag } nextPulse = NextPulse::None;
static float lastTempC = 0.0f;
static float lastLevelMm = 0.0f;
static uint32_t lastPulseWidthTempUs = 0;
static uint32_t lastPulseWidthLevelUs = 0;
static bool tempValid = false;
static bool levelValid = false;

// ========== СТАТИСТИКА ==========
static int hellaCallbackCounter = 0;
static int validTempPulseCounter = 0;
static int validLevelPulseCounter = 0;
static efitick_t latestCallbackTime = 0;

#if EFI_PROD_CODE
// ========== ОБРАБОТКА ИМПУЛЬСОВ (как в flex sensor) ==========
static void hellaOilCallback(efitick_t nowNt, bool value) {
    latestCallbackTime = nowNt;
    hellaCallbackCounter++;
    
    if (value) {
        // Rising edge: начинаем измерение импульса
        hellaPulseTimer.reset(nowNt);
        
        // Измеряем время между импульсами
        float timeBetweenPulses = hellaBetweenPulseTimer.getElapsedSecondsAndReset(nowNt);
        
        // Определяем тип импульса по паузе (по спецификации Hella)
        if (timeBetweenPulses > 0.89f * 0.780f && timeBetweenPulses < 1.11f * 0.780f) {
            nextPulse = NextPulse::Temp;
        } else if (timeBetweenPulses > 0.89f * 0.110f && timeBetweenPulses < 1.11f * 0.110f) {
            switch (nextPulse) {
                case NextPulse::Temp: nextPulse = NextPulse::Level; break;
                case NextPulse::Level: nextPulse = NextPulse::Diag; break;
                default: nextPulse = NextPulse::None; break;
            }
        } else {
            nextPulse = NextPulse::None;
        }
    } else {
        // Falling edge: конец импульса, измеряем длительность
        float lastPulseMs = 1000.0f * hellaPulseTimer.getElapsedSeconds(nowNt);
        
        // Валидация импульса (20-100ms по спецификации)
        if (lastPulseMs < 20.0f || lastPulseMs > 100.0f) {
            nextPulse = NextPulse::None;
            return;
        }
        
        if (nextPulse == NextPulse::Diag) {
            // Диагностический импульс - игнорируем
            return;
        } else if (nextPulse == NextPulse::Temp) {
            // ========== ТЕМПЕРАТУРНЫЙ ИМПУЛЬС ==========
            lastPulseWidthTempUs = static_cast<uint32_t>(lastPulseMs * 1000.0f);
            validTempPulseCounter++;
            
            // Калибровка из конфигурации (как в термисторах)
            lastTempC = interpolateClamped(
                engineConfiguration->hellaOilLevel.minPulseUsTemp / 1000.0f,
                engineConfiguration->hellaOilLevel.minTempC,
                engineConfiguration->hellaOilLevel.maxPulseUsTemp / 1000.0f, 
                engineConfiguration->hellaOilLevel.maxTempC,
                lastPulseMs);
                
            // Валидация диапазона
            if (lastTempC < -50.0f || lastTempC > 200.0f) {
                hellaOilTempSensor.invalidate(UnexpectedCode::Range);
                tempValid = false;
            } else {
                tempValid = true;
                hellaOilTempSensor.setValidValue(lastTempC, nowNt);
            }
            
            hellaOilTempRawSensor.setValidValue(static_cast<float>(lastPulseWidthTempUs), nowNt);
            
        } else if (nextPulse == NextPulse::Level) {
            // ========== ИМПУЛЬС УРОВНЯ МАСЛА ==========
            lastPulseWidthLevelUs = static_cast<uint32_t>(lastPulseMs * 1000.0f);
            validLevelPulseCounter++;
            
            // Калибровка из конфигурации
            lastLevelMm = interpolateClamped(
                engineConfiguration->hellaOilLevel.minPulseUsLevel / 1000.0f,
                engineConfiguration->hellaOilLevel.minLevelMm, 
                engineConfiguration->hellaOilLevel.maxPulseUsLevel / 1000.0f,
                engineConfiguration->hellaOilLevel.maxLevelMm,
                lastPulseMs);
                
            // Валидация диапазона
            if (lastLevelMm < 0.0f || lastLevelMm > 300.0f) {
                hellaOilLevelSensor.invalidate(UnexpectedCode::Range);
                levelValid = false;
            } else {
                levelValid = true;
                hellaOilLevelSensor.setValidValue(lastLevelMm, nowNt);
            }
            
            hellaOilLevelRawSensor.setValidValue(static_cast<float>(lastPulseWidthLevelUs), nowNt);
            
            // ========== СИНХРОНИЗАЦИЯ С КОНФИГУРАЦИЕЙ ==========
            engineConfiguration->hellaOilLevel.levelMm = lastLevelMm;
            engineConfiguration->hellaOilLevel.tempC = lastTempC;
            engineConfiguration->hellaOilLevel.rawPulseUsLevel = lastPulseWidthLevelUs;
            engineConfiguration->hellaOilLevel.rawPulseUsTemp = lastPulseWidthTempUs;
        }
    }
}

static Gpio hellaPin = Gpio::Unassigned;

static void hellaExtiCallback(void*, efitick_t nowNt) {
    hellaOilCallback(nowNt, efiReadPin(hellaPin) ^ engineConfiguration->hellaOilLevelInverted);
}
#endif // EFI_PROD_CODE

// ========== ПУБЛИЧНЫЕ ФУНКЦИИ ==========
void initHellaOilLevelSensor(bool isFirstTime) {
#if EFI_PROD_CODE
    if (!isBrainPinValid(engineConfiguration->hellaOilLevelPin)) {
        return;
    }
    
    if (efiExtiEnablePin("hellaOil", engineConfiguration->hellaOilLevelPin,
                        PAL_EVENT_MODE_BOTH_EDGES, hellaExtiCallback, nullptr) < 0) {
        criticalError("Hella oil sensor: failed to enable EXTI");
        return;
    }
    
    hellaPin = engineConfiguration->hellaOilLevelPin;
    
    // ========== КОНСОЛЬНЫЕ КОМАНДЫ (как в flex sensor) ==========
    if (isFirstTime) {
        addConsoleAction("hellainfo", []() {
            efiPrintf("Hella Oil Sensor Info:");
            efiPrintf("Level: %.1f mm %s", lastLevelMm, levelValid ? "[VALID]" : "[INVALID]");
            efiPrintf("Temp: %.1f °C %s", lastTempC, tempValid ? "[VALID]" : "[INVALID]");
            efiPrintf("Raw Level: %d μs", lastPulseWidthLevelUs);
            efiPrintf("Raw Temp: %d μs", lastPulseWidthTempUs);
            efiPrintf("Callbacks: total=%d, validTemp=%d, validLevel=%d", 
                     hellaCallbackCounter, validTempPulseCounter, validLevelPulseCounter);
            efiPrintf("Latest callback: %lld", latestCallbackTime);
            efiPrintf("Calibration - Level: %.0f-%.0fμs -> %.0f-%.0fmm",
                     (float)engineConfiguration->hellaOilLevel.minPulseUsLevel,
                     (float)engineConfiguration->hellaOilLevel.maxPulseUsLevel,
                     (float)engineConfiguration->hellaOilLevel.minLevelMm,
                     (float)engineConfiguration->hellaOilLevel.maxLevelMm);
            efiPrintf("Calibration - Temp: %.0f-%.0fμs -> %.0f-%.0f°C",
                     (float)engineConfiguration->hellaOilLevel.minPulseUsTemp,
                     (float)engineConfiguration->hellaOilLevel.maxPulseUsTemp,
                     (float)engineConfiguration->hellaOilLevel.minTempC,
                     (float)engineConfiguration->hellaOilLevel.maxTempC);
        });
    }
#endif // EFI_PROD_CODE

    // ========== РЕГИСТРАЦИЯ СЕНСОРОВ ==========
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
    if (isBrainPinValid(hellaPin)) {
        efiExtiDisablePin(hellaPin);
    }
    hellaPin = Gpio::Unassigned;
#endif
}

// ========== ДИАГНОСТИЧЕСКИЕ ФУНКЦИИ ==========
float getHellaOilLevelMm() { return lastLevelMm; }
float getHellaOilTempC() { return lastTempC; }
bool isHellaOilLevelValid() { return levelValid; }
bool isHellaOilTempValid() { return tempValid; }
uint32_t getHellaOilLevelRawUs() { return lastPulseWidthLevelUs; }
uint32_t getHellaOilTempRawUs() { return lastPulseWidthTempUs; }

#else
// ========== ЗАГЛУШКИ ==========
void initHellaOilLevelSensor(bool /*isFirstTime*/) {}
void deInitHellaOilLevelSensor() {}
float getHellaOilLevelMm() { return 0; }
float getHellaOilTempC() { return 0; }
bool isHellaOilLevelValid() { return false; }
bool isHellaOilTempValid() { return false; }
uint32_t getHellaOilLevelRawUs() { return 0; }
uint32_t getHellaOilTempRawUs() { return 0; }
#endif // EFI_HELLA_OIL
