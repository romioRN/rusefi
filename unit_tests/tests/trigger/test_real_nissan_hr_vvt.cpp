#include "pch.h"

#include "logicdata_csv_reader.h"

TEST(nissan, realFromFileVVTIN) {
	CsvReader reader(1, /* vvtCount */ 0);

	reader.open("rusefi/unit_tests/tests/trigger/resources/hr12-vvt-in.csv");
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
//	engineConfiguration->isFasterEngineSpinUpEnabled = true;
//	engineConfiguration->alwaysInstantRpm = true;

	eth.setTriggerType(trigger_type_e::TT_NISSAN_HR_CAM_IN);


	while (reader.haveMore()) {
		reader.processLine(&eth);
	}

	ASSERT_EQ( 1, eth.recentWarnings()->getCount())<< "warningCounter#nissanRealCranking";
	ASSERT_EQ(1,  engine->triggerCentral.triggerState.totalTriggerErrorCounter);
	ASSERT_EQ(179, round(Sensor::getOrZero(SensorType::Rpm)))<< reader.lineIndex();
}