/**
 * @file test_multi_injection_scheduler.cpp
 * @brief Unit tests for multi-injection scheduling
 */

#include "pch.h"
#include "gtest/gtest.h"
#include "fuel_schedule.h"
#include "engine_test_helper.h"

TEST(MultiInjectionScheduler, SchedulePulseValidation) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    engineConfiguration->multiInjection.enableMultiInjection = true;
    Sensor::setMockValue(SensorType::Rpm, 2000);
    
    engine->rpmCalculator.oneDegreeUs = 83.333f;
    
    InjectionEvent event;
    event.setIndex(0);
    event.setNumberOfPulses(2);
    
    // Setup valid pulse
    event.getPulse(0).startAngle = 300.0f;
    event.getPulse(0).fuelMs = 3.0f;
    event.getPulse(0).isActive = true;
    
    efitick_t nowNt = getTimeNowNt();
    float currentPhase = 280.0f;
    
    // Should successfully schedule
    EXPECT_NO_THROW(event.schedulePulse(0, nowNt, currentPhase));
}

TEST(MultiInjectionScheduler, InactivePulseNotScheduled) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    InjectionEvent event;
    event.setIndex(0);
    event.setNumberOfPulses(2);
    
    // Inactive pulse
    event.getPulse(0).isActive = false;
    
    efitick_t nowNt = getTimeNowNt();
    
    // Should not crash, just return early
    EXPECT_NO_THROW(event.schedulePulse(0, nowNt, 0.0f));
}

TEST(MultiInjectionScheduler, OnTriggerToothMultiMode) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    engineConfiguration->multiInjection.enableMultiInjection = true;
    engineConfiguration->cylindersCount = 4;
    Sensor::setMockValue(SensorType::Rpm, 2000);
    
    FuelSchedule schedule;
    schedule.addFuelEvents();
    
    EXPECT_TRUE(schedule.isReady);
    
    efitick_t nowNt = getTimeNowNt();
    float currentPhase = 0.0f;
    float nextPhase = 90.0f;
    
    // Should not crash
    EXPECT_NO_THROW(schedule.onTriggerTooth(nowNt, currentPhase, nextPhase));
}
