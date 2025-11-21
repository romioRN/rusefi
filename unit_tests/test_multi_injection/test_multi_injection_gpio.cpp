/**
 * @file test_multi_injection_gpio.cpp
 * @brief Unit tests for multi-injection GPIO control
 */

#include "pch.h"
#include "gtest/gtest.h"
#include "injection_gpio.h"
#include "engine_test_helper.h"

TEST(MultiInjectionGPIO, OverlappingCounterIncrement) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    // Use existing engine pin instead of creating new one
    auto& pin = enginePins.injectors[0];
    
    efitick_t nowNt = getTimeNowNt();
    
    int initialCounter = pin.getOverlappingCounter();
    
    pin.open(nowNt, 5000);
    
    int counter1 = pin.getOverlappingCounter();
    EXPECT_GT(counter1, initialCounter);
    
    pin.open(nowNt + US2NT(1000), 5000);
    
    int counter2 = pin.getOverlappingCounter();
    EXPECT_GT(counter2, counter1);
}

TEST(MultiInjectionGPIO, OverlappingCounterDecrement) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    auto& pin = enginePins.injectors[0];
    
    efitick_t nowNt = getTimeNowNt();
    
    // Open twice
    pin.open(nowNt, 5000);
    pin.open(nowNt + US2NT(1000), 5000);
    
    int counterBefore = pin.getOverlappingCounter();
    EXPECT_GE(counterBefore, 2);
    
    // Close first
    pin.close(nowNt + US2NT(5000));
    
    int counterAfter1 = pin.getOverlappingCounter();
    EXPECT_LT(counterAfter1, counterBefore);
    
    // Close second
    pin.close(nowNt + US2NT(6000));
    
    int counterAfter2 = pin.getOverlappingCounter();
    EXPECT_LE(counterAfter2, counterAfter1);
}

TEST(MultiInjectionGPIO, InvalidDurationIgnored) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    auto& pin = enginePins.injectors[0];
    
    efitick_t nowNt = getTimeNowNt();
    
    int initialCounter = pin.getOverlappingCounter();
    
    // Negative duration - should be ignored
    pin.open(nowNt, -100);
    EXPECT_EQ(pin.getOverlappingCounter(), initialCounter);
    
    // Too large duration - should be ignored
    pin.open(nowNt, 200000);
    EXPECT_EQ(pin.getOverlappingCounter(), initialCounter);
}
