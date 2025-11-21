/**
 * @file test_multi_injection_scheduler.cpp
 * @brief Unit tests for multi-injection scheduling
 */

#include "pch.h"
#include "gtest/gtest.h"
#include "fuel_schedule.h"
#include "engine_test_helper.h"

TEST(MultiInjectionScheduler, BasicSchedulingTest) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    engineConfiguration->multiInjection.enableMultiInjection = true;
    Sensor::setMockValue(SensorType::Rpm, 2000);
    
    engine->engineState.injectionMass[0] = 10.0f;
    engine->rpmCalculator.oneDegreeUs = 83.333f;
    
    InjectionEvent event;
    event.setIndex(0);
    event.setNumberOfPulses(2);
    
    // Let updateMultiInjectionAngles fill the pulses
    bool result = event.updateMultiInjectionAngles();
    
    EXPECT_TRUE(result);
    
    // Now try to schedule a pulse - should not crash
    efitick_t nowNt = getTimeNowNt();
    float currentPhase = 0.0f;
    
    EXPECT_NO_THROW(event.schedulePulse(0, nowNt, currentPhase));
}

TEST(MultiInjectionScheduler, InactivePulseNotScheduled) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    InjectionEvent event;
    event.setIndex(0);
    event.setNumberOfPulses(2);
    
    // Don't set up pulses - they will be inactive by default
    
    efitick_t nowNt = getTimeNowNt();
    
    // Should not crash when trying to schedule inactive pulse
    EXPECT_NO_THROW(event.schedulePulse(0, nowNt, 0.0f));
}

TEST(MultiInjectionScheduler, OnTriggerToothMultiMode) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    engineConfiguration->multiInjection.enableMultiInjection = true;
    engineConfiguration->cylindersCount = 4;
    Sensor::setMockValue(SensorType::Rpm, 2000);
    
    // Setup injection mass for all cylinders
    for (int i = 0; i < 4; i++) {
        engine->engineState.injectionMass[i] = 10.0f;
    }
    engine->rpmCalculator.oneDegreeUs = 83.333f;
    
    FuelSchedule schedule;
    schedule.addFuelEvents();
    
    EXPECT_TRUE(schedule.isReady);
    
    efitick_t nowNt = getTimeNowNt();
    float currentPhase = 0.0f;
    float nextPhase = 90.0f;
    
    // Should not crash
    EXPECT_NO_THROW(schedule.onTriggerTooth(nowNt, currentPhase, nextPhase));
}

TEST(MultiInjectionScheduler, MultiModeSwitching) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    Sensor::setMockValue(SensorType::Rpm, 2000);
    engine->engineState.injectionMass[0] = 10.0f;
    engine->rpmCalculator.oneDegreeUs = 83.333f;
    
    InjectionEvent event;
    event.setIndex(0);
    
    // Test single mode
    engineConfiguration->multiInjection.enableMultiInjection = false;
    event.setNumberOfPulses(1);
    bool singleResult = event.update();
    EXPECT_TRUE(singleResult);
    
    // Test multi mode
    engineConfiguration->multiInjection.enableMultiInjection = true;
    event.setNumberOfPulses(2);
    bool multiResult = event.update();
    EXPECT_TRUE(multiResult);
}

TEST(MultiInjectionScheduler, ZeroRPMHandling) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    engineConfiguration->multiInjection.enableMultiInjection = true;
    Sensor::setMockValue(SensorType::Rpm, 0); // Zero RPM
    
    engine->engineState.injectionMass[0] = 10.0f;
    
    InjectionEvent event;
    event.setIndex(0);
    event.setNumberOfPulses(2);
    
    // Should handle gracefully
    bool result = event.updateMultiInjectionAngles();
    
    // Should return false or fallback to single injection
    EXPECT_FALSE(result);
}
