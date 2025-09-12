#pragma once

// Простые функции доступа (НЕ НУЖЕН КЛАСС)
void initHellaOilLevelSensor(bool isFirstTime);
void deInitHellaOilLevelSensor();

#if EFI_HELLA_OIL
// Диагностические функции
float getHellaOilLevelMm();
float getHellaOilTempC();
bool isHellaOilLevelValid();
bool isHellaOilTempValid();
uint32_t getHellaOilLevelRawUs();
uint32_t getHellaOilTempRawUs();
#endif
