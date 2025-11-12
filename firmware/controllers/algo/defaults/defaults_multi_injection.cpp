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
	// Default split ratios (60/40 for double injection)
	engineConfiguration->multiInjection.splitRatio1 = 60;
	engineConfiguration->multiInjection.splitRatio2 = 40;
	engineConfiguration->multiInjection.splitRatio3 = 0;
	engineConfiguration->multiInjection.splitRatio4 = 0;
	engineConfiguration->multiInjection.splitRatio5 = 0;
	
	// Default number of injections
	engineConfiguration->multiInjection.numberOfInjections = 1;  // Single by default
	
	// Default angles
	engineConfiguration->multiInjection.injection1AngleOffset = 300;  // Intake stroke
	engineConfiguration->multiInjection.injection2AngleOffset = 130;  // Compression stroke
	engineConfiguration->multiInjection.injection3AngleOffset = 0;
	engineConfiguration->multiInjection.injection4AngleOffset = 0;
	engineConfiguration->multiInjection.injection5AngleOffset = 0;
	
	// Default dwell
	engineConfiguration->multiInjection.dwellAngleBetweenInjections = 20;
	
	// Disabled by default
	engineConfiguration->multiInjection.enableMultiInjection = false;
	engineConfiguration->multiInjection.enableLoadBasedSplit = false;
	engineConfiguration->multiInjection.enableRpmAngleCorrection = false;
	engineConfiguration->multiInjection.enableMultiInjectionStaging = false;
	
	// Initialize table axes
	static const float defaultRpmBins[16] = {
		1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500,
		5000, 5500, 6000, 6500, 7000, 7500, 8000, 8500
	};
	
	static const uint16_t defaultLoadBins[16] = {
		50, 70, 90, 110, 130, 150, 170, 190,
		210, 230, 250, 270, 290, 310, 330, 350
	};
	
	copyArray(engineConfiguration->multiInjectionRpmBins, defaultRpmBins);
	copyArray(engineConfiguration->multiInjectionLoadBins, defaultLoadBins);
	
	// Fill tables with reasonable defaults
	for (int loadIdx = 0; loadIdx < 16; loadIdx++) {
		for (int rpmIdx = 0; rpmIdx < 16; rpmIdx++) {
			// Split ratio: 100% (single injection by default)
			engineConfiguration->multiInjectionSplitRatioTable[loadIdx][rpmIdx] = 100;
			
			// Second injection angle: 130° BTDC
			engineConfiguration->secondInjectionAngleTable[loadIdx][rpmIdx] = 130;
			
			// Minimum dwell: 15°
			engineConfiguration->minDwellAngleTable[loadIdx][rpmIdx] = 15;
		}
	}
}

#endif // EFI_ENGINE_CONTROL
