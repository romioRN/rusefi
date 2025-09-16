#pragma once

void initHellaOilLevelSensor(bool isFirstTime);
void deInitHellaOilLevelSensor();

#if EFI_HELLA_OIL_BMW
float getHellaOilLevelMm();
float getHellaOilTempC();
bool  isHellaOilLevelValid();
bool  isHellaOilTempValid();
uint32_t getHellaOilLevelRawUs();
uint32_t getHellaOilTempRawUs();
#else
void   initHellaOilLevelSensor(bool);
void   deInitHellaOilLevelSensor();
float  getHellaOilLevelMm();
float  getHellaOilTempC();
bool   isHellaOilLevelValid();
bool   isHellaOilTempValid();
uint32_t getHellaOilLevelRawUs();
uint32_t getHellaOilTempRawUs();
#endif
