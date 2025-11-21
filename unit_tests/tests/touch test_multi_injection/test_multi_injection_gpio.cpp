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
    
    InjectorOutputPin pin;
    pin.injectorIndex = 0;
    
    efitick_t nowNt = getTimeNowNt();
    
    // First open
    pin.open(nowNt, 5000); // 5ms duration
    
    // Counter should be 1
    EXPECT_EQ(pin.overlappingCounter, 1);
    
    // Second open (overlapping)
    pin.open(nowNt + US2NT(1000), 5000);
    
    // Counter should be 2
    EXPECT_EQ(pin.overlappingCounter, 2);
}

TEST(MultiInjectionGPIO, OverlappingCounterDecrement) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    InjectorOutputPin pin;
    pin.injectorIndex = 0;
    
    efitick_t nowNt = getTimeNowNt();
    
    // Open twice
    pin.open(nowNt, 5000);
    pin.open(nowNt + US2NT(1000), 5000);
    
    EXPECT_EQ(pin.overlappingCounter, 2);
    
    // Close once
    pin.close(nowNt + US2NT(5000));
    
    EXPECT_EQ(pin.overlappingCounter, 1);
    
    // Close again
    pin.close(nowNt + US2NT(6000));
    
    EXPECT_EQ(pin.overlappingCounter, 0);
}

TEST(MultiInjectionGPIO, InvalidDurationIgnored) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    InjectorOutputPin pin;
    pin.injectorIndex = 0;
    
    efitick_t nowNt = getTimeNowNt();
    
    // Try to open with invalid duration
    pin.open(nowNt, -100); // Negative duration
    
    // Counter should stay at 0
    EXPECT_EQ(pin.overlappingCounter, 0);
    
    // Try with too large duration
    pin.open(nowNt, 200000); // > 100ms
    
    EXPECT_EQ(pin.overlappingCounter, 0);
}

TEST(MultiInjectionGPIO, SchedulerCancellation) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    InjectorOutputPin pin;
    pin.injectorIndex = 0;
    
    efitick_t nowNt = getTimeNowNt();
    
    // First injection
    pin.open(nowNt, 5000);
    
    // Second injection overlapping - should cancel first close event
    pin.open(nowNt + US2NT(2000), 3000);
    
    // Counter should still be 2 (both opens counted)
    EXPECT_EQ(pin.overlappingCounter, 2);
}
