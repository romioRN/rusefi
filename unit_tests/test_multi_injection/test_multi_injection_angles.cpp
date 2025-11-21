/**
 * @file test_multi_injection_angles.cpp
 * @brief Unit tests for multi-injection angle calculations
 */

#include "pch.h"
#include "gtest/gtest.h"
#include "fuel_schedule.h"
#include "engine_test_helper.h"

TEST(MultiInjectionAngles, SplitRatioCalculation) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    // Enable multi-injection
    engineConfiguration->multiInjection.enableMultiInjection = true;
    
    // Setup test conditions
    Sensor::setMockValue(SensorType::Rpm, 2000);
    
    InjectionEvent event;
    event.setIndex(0);
    event.setNumberOfPulses(2);
    
    // Test split ratio calculation
    float ratio0 = event.computeSplitRatio(0);
    float ratio1 = event.computeSplitRatio(1);
    
    // Split ratios should add up to 100%
    EXPECT_NEAR(ratio0 + ratio1, 100.0f, 0.1f);
    
    // Each ratio should be positive
    EXPECT_GT(ratio0, 0.0f);
    EXPECT_GT(ratio1, 0.0f);
    
    // First pulse should get 60% by default
    EXPECT_NEAR(ratio0, 60.0f, 5.0f);
    EXPECT_NEAR(ratio1, 40.0f, 5.0f);
}

TEST(MultiInjectionAngles, SecondaryAngleCalculation) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    engineConfiguration->multiInjection.enableMultiInjection = true;
    Sensor::setMockValue(SensorType::Rpm, 2000);
    
    // Setup engine rotation state
    engine->rpmCalculator.oneDegreeUs = 83.333f; // 2000 RPM
    
    InjectionEvent event;
    event.setIndex(0);
    event.setNumberOfPulses(2);
    
    // Calculate angles
    float angle0 = event.computeSecondaryInjectionAngle(0);
    float angle1 = event.computeSecondaryInjectionAngle(1);
    
    // Angles should be in valid range (0-720°)
    EXPECT_GE(angle0, 0.0f);
    EXPECT_LT(angle0, 720.0f);
    EXPECT_GE(angle1, 0.0f);
    EXPECT_LT(angle1, 720.0f);
    
    // Angles should be different
    EXPECT_NE(angle0, angle1);
}

TEST(MultiInjectionAngles, UpdateMultiInjectionAngles) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    engineConfiguration->multiInjection.enableMultiInjection = true;
    Sensor::setMockValue(SensorType::Rpm, 2000);
    
    // Setup injection mass
    engine->engineState.injectionMass[0] = 10.0f; // 10mg
    engine->rpmCalculator.oneDegreeUs = 83.333f;
    
    InjectionEvent event;
    event.setIndex(0);
    event.setNumberOfPulses(2);
    
    // Update angles
    bool result = event.updateMultiInjectionAngles();
    
    EXPECT_TRUE(result);
    
    // Check that pulses were filled
    const auto& pulse0 = event.getPulse(0);
    const auto& pulse1 = event.getPulse(1);
    
    EXPECT_TRUE(pulse0.isActive);
    EXPECT_TRUE(pulse1.isActive);
    EXPECT_GT(pulse0.fuelMs, 0.0f);
    EXPECT_GT(pulse1.fuelMs, 0.0f);
    EXPECT_GT(pulse0.durationAngle, 0.0f);
    EXPECT_GT(pulse1.durationAngle, 0.0f);
}

TEST(MultiInjectionAngles, FallbackToSingleInjection) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    // Disable multi-injection
    engineConfiguration->multiInjection.enableMultiInjection = false;
    
    Sensor::setMockValue(SensorType::Rpm, 2000);
    engine->engineState.injectionMass[0] = 10.0f;
    engine->rpmCalculator.oneDegreeUs = 83.333f;
    
    InjectionEvent event;
    event.setIndex(0);
    event.setNumberOfPulses(1); // Start with single pulse
    
    // With multi disabled, regular update() should work
    bool result = event.update();
    
    EXPECT_TRUE(result);
    EXPECT_EQ(event.getNumberOfPulses(), 1);
}

TEST(MultiInjectionAngles, InvalidRPMHandling) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    engineConfiguration->multiInjection.enableMultiInjection = true;
    Sensor::setMockValue(SensorType::Rpm, 0); // Invalid RPM
    
    InjectionEvent event;
    event.setIndex(0);
    event.setNumberOfPulses(2);
    
    bool result = event.updateMultiInjectionAngles();
    
    // Should return false for invalid RPM
    EXPECT_FALSE(result);
}

TEST(MultiInjectionAngles, ZeroFuelMassHandling) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    engineConfiguration->multiInjection.enableMultiInjection = true;
    Sensor::setMockValue(SensorType::Rpm, 2000);
    
    // Zero fuel mass
    engine->engineState.injectionMass[0] = 0.0f;
    
    InjectionEvent event;
    event.setIndex(0);
    event.setNumberOfPulses(2);
    
    bool result = event.updateMultiInjectionAngles();
    
    // Should return false for zero fuel
    EXPECT_FALSE(result);
}
