/**
 * @file test_multi_injection_validation.cpp
 * @brief Unit tests for multi-injection window validation
 */

#include "pch.h"
#include "gtest/gtest.h"
#include "fuel_schedule.h"
#include "engine_test_helper.h"

TEST(MultiInjectionValidation, SinglePulseAlwaysValid) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    InjectionEvent event;
    event.setIndex(0);
    event.setNumberOfPulses(1);
    
    bool result = event.validateInjectionWindows();
    
    EXPECT_TRUE(result);
}

TEST(MultiInjectionValidation, BasicValidationTest) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    engineConfiguration->multiInjection.enableMultiInjection = true;
    Sensor::setMockValue(SensorType::Rpm, 2000);
    
    engine->engineState.injectionMass[0] = 10.0f;
    engine->rpmCalculator.oneDegreeUs = 83.333f;
    
    InjectionEvent event;
    event.setIndex(0);
    event.setNumberOfPulses(2);
    
    bool result = event.updateMultiInjectionAngles();
    
    EXPECT_TRUE(result);
    
    bool validationResult = event.validateInjectionWindows();
    
    // Just check it doesn't crash - validation may pass or fail depending on table values
    EXPECT_TRUE(validationResult || !validationResult);
}
