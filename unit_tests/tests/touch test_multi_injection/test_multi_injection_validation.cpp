/**
 * @file test_multi_injection_validation.cpp
 * @brief Unit tests for multi-injection window validation
 */

#include "pch.h"
#include "gtest/gtest.h"
#include "fuel_schedule.h"
#include "engine_test_helper.h"

TEST(MultiInjectionValidation, ValidInjectionWindows) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    engineConfiguration->multiInjection.enableMultiInjection = true;
    Sensor::setMockValue(SensorType::Rpm, 2000);
    
    engine->engineState.injectionMass[0] = 10.0f;
    engine->rpmCalculator.oneDegreeUs = 83.333f;
    
    InjectionEvent event;
    event.setIndex(0);
    event.setNumberOfPulses(2);
    
    // Setup valid pulses with sufficient dwell
    event.getPulse(0).startAngle = 300.0f;
    event.getPulse(0).durationAngle = 20.0f;
    event.getPulse(0).fuelMs = 3.0f;
    event.getPulse(0).isActive = true;
    
    event.getPulse(1).startAngle = 100.0f; // 100° after pulse 0 ends
    event.getPulse(1).durationAngle = 15.0f;
    event.getPulse(1).fuelMs = 2.0f;
    event.getPulse(1).isActive = true;
    
    bool result = event.validateInjectionWindows();
    
    EXPECT_TRUE(result);
}

TEST(MultiInjectionValidation, InsufficientDwell) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    engineConfiguration->multiInjection.enableMultiInjection = true;
    Sensor::setMockValue(SensorType::Rpm, 2000);
    
    InjectionEvent event;
    event.setIndex(0);
    event.setNumberOfPulses(2);
    
    // Setup pulses with insufficient dwell (overlap)
    event.getPulse(0).startAngle = 300.0f;
    event.getPulse(0).durationAngle = 50.0f;
    event.getPulse(0).fuelMs = 5.0f;
    event.getPulse(0).isActive = true;
    
    event.getPulse(1).startAngle = 260.0f; // Overlaps with pulse 0!
    event.getPulse(1).durationAngle = 15.0f;
    event.getPulse(1).fuelMs = 2.0f;
    event.getPulse(1).isActive = true;
    
    bool result = event.validateInjectionWindows();
    
    EXPECT_FALSE(result);
}

TEST(MultiInjectionValidation, SinglePulseAlwaysValid) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    InjectionEvent event;
    event.setIndex(0);
    event.setNumberOfPulses(1);
    
    // Single pulse should always pass validation
    bool result = event.validateInjectionWindows();
    
    EXPECT_TRUE(result);
}

TEST(MultiInjectionValidation, PulseTooShort) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    engineConfiguration->multiInjection.enableMultiInjection = true;
    Sensor::setMockValue(SensorType::Rpm, 2000);
    
    // Setup injector with 1.0ms dead time
    engine->module<InjectorModelPrimary>()->postState(1.0f);
    
    InjectionEvent event;
    event.setIndex(0);
    event.setNumberOfPulses(2);
    
    // Pulse shorter than dead time threshold
    event.getPulse(0).startAngle = 300.0f;
    event.getPulse(0).durationAngle = 10.0f;
    event.getPulse(0).fuelMs = 0.5f; // Too short!
    event.getPulse(0).isActive = true;
    
    event.getPulse(1).startAngle = 100.0f;
    event.getPulse(1).durationAngle = 15.0f;
    event.getPulse(1).fuelMs = 2.0f;
    event.getPulse(1).isActive = true;
    
    bool result = event.validateInjectionWindows();
    
    EXPECT_FALSE(result);
}
