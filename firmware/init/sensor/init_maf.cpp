#include "pch.h"

#include "init.h"
#include "adc_subscription.h"
#include "functional_sensor.h"
#include "table_func.h"

static FunctionalSensor maf (SensorType::Maf , /* timeout = */ MS2NT(50));
static FunctionalSensor maf2(SensorType::Maf2, /* timeout = */ MS2NT(50));


// extract the type of the elements in the bin/value arrays
using BinType = std::remove_extent_t<decltype(config->mafDecodingBins)>;
using ValueType = std::remove_extent_t<decltype(config->mafDecoding)>;

// This function converts volts -> kg/h
static TableFunc mafCurve(config->mafDecodingBins, config->mafDecoding);

static void initMaf(adc_channel_e channel, FunctionalSensor& m) {
	if (!isAdcChannelValid(channel)) {
		return;
	}

	m.setFunction(mafCurve);

	AdcSubscription::SubscribeSensor(m, channel, /*lowpassCutoff =*/ 50);
	m.Register();
}

void initMaf() {
	initMaf(engineConfiguration->mafAdcChannel, maf);
	initMaf(engineConfiguration->maf2AdcChannel, maf2);
}
