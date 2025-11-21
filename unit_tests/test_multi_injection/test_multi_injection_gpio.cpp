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
    
    pin.open(nowNt, 5000);
    EXPECT_EQ(pin.getOverlappingCounter(), 1);
    
    pin.open(nowNt + US2NT(1000), 5000);
    EXPECT_EQ(pin.getOverlappingCounter(), 2);
}

TEST(MultiInjectionGPIO, OverlappingCounterDecrement) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    InjectorOutputPin pin;
    pin.injectorIndex = 0;
    
    efitick_t nowNt = getTimeNowNt();
    
    pin.open(nowNt, 5000);
    pin.open(nowNt + US2NT(1000), 5000);
    
    EXPECT_EQ(pin.getOverlappingCounter(), 2);
    
    pin.close(nowNt + US2NT(5000));
    EXPECT_EQ(pin.getOverlappingCounter(), 1);
    
    pin.close(nowNt + US2NT(6000));
    EXPECT_EQ(pin.getOverlappingCounter(), 0);
}

TEST(MultiInjectionGPIO, InvalidDurationIgnored) {
    EngineTestHelper eth(engine_type_e::TEST_ENGINE);
    
    InjectorOutputPin pin;
    pin.injectorIndex = 0;
    
    efitick_t nowNt = getTimeNowNt();
    
    pin.open(nowNt, -100);
    EXPECT_EQ(pin.getOverlappingCounter(), 0);
    
    pin.open(nowNt, 200000);
    EXPECT_EQ(pin.getOverlappingCounter(), 0);
}
