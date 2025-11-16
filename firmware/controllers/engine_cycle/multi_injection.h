#pragma once

#include <cstdint>
#include "pch.h"

/**
 * @file multi_injection.h
 * @brief Multi-injection (split injection) support for RusEFI
 */

#if EFI_ENGINE_CONTROL

// Structure for individual pulse data
struct InjectionPulse {
  float startAngle;        // Угол начала впрыска (0-720°)
  float durationAngle;     // Длительность импульса в градусах
  floatms_t fuelMs;        // Топливо в миллисекундах
  float splitRatio;        // Процент от общего топлива (0-100%)
  bool isActive;           // Активен ли этот импульс
};

/**
 * @brief Multi-injection event handler
 * Manages split injection with up to 2 pulses per cycle
 */
class InjectionEvent {
public:
  /**
   * @brief Initialize multi-injection for this cylinder
   */
  void configureMultiInjection(uint8_t pulseCount);
  
  /**
   * @brief Update all pulse parameters based on current engine state
   */
  bool updateMultiInjectionAngles();
  
  /**
   * @brief Get split ratio for specified pulse
   */
  float computeSplitRatio(uint8_t pulseIndex) const;
  
  /**
   * @brief Get start angle for specified pulse
   */
  float computeSecondaryInjectionAngle(uint8_t pulseIndex) const;
  
  /**
   * @brief Validate injection windows (no overlaps)
   */
  bool validateInjectionWindows() const;
  
  /**
   * @brief Schedule one pulse opening
   */
  void schedulePulse(uint8_t pulseIndex, efitick_t nowNt, float currentPhase);

private:
  uint8_t numberOfPulses;           // 1 или 2
  InjectionPulse pulses[2];         // Данные для каждого импульса
  float injectionStartAngle;        // Основной угол впрыска
};

/**
 * @brief Fuel schedule multi-injection management
 */
class FuelSchedule {
public:
  /**
   * @brief Configure all cylinders for multi-injection
   */
  void configureMultiInjectionForAllCylinders();

private:
  InjectionEvent elements[MAX_CYLINDERS];
};

#endif // EFI_ENGINE_CONTROL
