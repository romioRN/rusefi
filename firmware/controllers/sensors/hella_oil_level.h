#pragma once

// Инициализация/деинициализация датчика Hella Oil Level
void initHellaOilLevelSensor(bool isFirstTime);
void deInitHellaOilLevelSensor();

#if EFI_HELLA_OIL
// Доступ к последним значениям
float getHellaOilLevelMm();
float getHellaOilTempC();
bool isHellaOilLevelValid();
bool isHellaOilTempValid();
uint32_t getHellaOilLevelRawUs();
uint32_t getHellaOilTempRawUs();
#else
inline void initHellaOilLevelSensor(bool) {}
inline void deInitHellaOilLevelSensor() {}
inline float getHellaOilLevelMm() { return 0; }
inline float getHellaOilTempC() { return 0; }
inline bool isHellaOilLevelValid() { return false; }
inline bool isHellaOilTempValid() { return false; }
inline uint32_t getHellaOilLevelRawUs() { return 0; }
inline uint32_t getHellaOilTempRawUs() { return 0; }
#endif

