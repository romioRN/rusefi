/**
 * @file defaults_multi_injection.cpp
 * @brief Default values for multi-injection tables
 */

#include "pch.h"
#include "defaults.h"

#if EFI_ENGINE_CONTROL

/**
 * Initialize multi-injection tables with default values
 * Called during ECU initialization
 */
void setDefaultMultiInjectionConfiguration() {
  // Disabled by default
  engineConfiguration->multiInjection.enableMultiInjection = false;
  
  // Default dwell angle between pulses
  engineConfiguration->multiInjection.dwellAngleBetweenInjections = 20;
  
  // Initialize RPM bins: 1000, 1500, 2000... 8500
  for (int i = 0; i < 16; i++) {
    engineConfiguration->multiInjectionRpmBins[i] = 1000.0f + i * 500.0f;
  }
  
  // Initialize Load bins: 50, 70, 90... 350%
  for (int i = 0; i < 16; i++) {
    engineConfiguration->multiInjectionLoadBins[i] = 50 + i * 20;
  }
  
  // Fill Split Ratio Table with defaults (60% / 40% split)
  for (int loadIdx = 0; loadIdx < 16; loadIdx++) {
    for (int rpmIdx = 0; rpmIdx < 16; rpmIdx++) {
      engineConfiguration->multiInjectionSplitRatioTable[loadIdx][rpmIdx] = 60;  // 60% Pulse 0, 40% Pulse 1
    }
  }
  
  // Fill Second Injection Angle Table with defaults (100° BTDC)
  for (int loadIdx = 0; loadIdx < 16; loadIdx++) {
    for (int rpmIdx = 0; rpmIdx < 16; rpmIdx++) {
      engineConfiguration->secondInjectionAngleTable[loadIdx][rpmIdx] = 100;
    }
  }
}

#endif // EFI_ENGINE_CONTROL
