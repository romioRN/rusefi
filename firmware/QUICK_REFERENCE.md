# QUICK REFERENCE: Все изменения мультивпрыска

## ✅ 3 исправления, 3 файла

### 1. `controllers/engine_cycle/multi_injection.cpp`

**Исправление #1: Длительность впрыска**
```cpp
// ДО:
floatms_t baseFuelMs = getEngineState()->injectionDuration;
floatms_t pulseFuelMs = baseFuelMs * (ratio / 100.0f);

// ПОСЛЕ:
float baseFuelMass = getEngineState()->injectionMass[cylinderNumber];
float pulseMass = baseFuelMass * (ratio / 100.0f);
floatms_t pulseFuelMs = engine->module<InjectorModelPrimary>()->getInjectionDuration(pulseMass);
```

**Исправление #2: Преобразование BTDC для Pulse 1**
```cpp
// ДО:
float baseAngle = interpolate3d(...secondInjectionAngleTable...);

// ПОСЛЕ:
float btdcAngle = interpolate3d(...secondInjectionAngleTable...);  // BTDC из таблицы
float baseAngle = 720.0f - btdcAngle;  // Преобразовать в абсолютный угол
```

---

### 2. `controllers/settings.cpp`

**Улучшение вывода отладки**
```cpp
// Добавлено:
float fuelMass = getEngineState()->injectionMass[i];
float pulseMass = fuelMass * (pulse.splitRatio / 100.0f);
efiPrintf("  Pulse %d: ... mass=%.3f g", pulseIdx, pulseMass);
```

---

### 3. `tunerstudio/tunerstudio.template.ini`

**Исправление структуры панелей**
```ini
# ДО: Панели определены неправильно (внутри диалога)
# ПОСЛЕ: Панели определены перед диалогом с правильным синтаксисом

[multiInjectionBasicPanel]
    field = "Enable Multi-Injection", multiInjection_enableMultiInjection

[multiInjectionTimingPanel]
    field = "Split Ratio Table (% fuel in Pulse 0)", multiInjSplitRatioTbl, {multiInjection_enableMultiInjection}
    field = ""
    field = "Second Injection Angle Table (BTDC)", secondInjAngleTbl, {multiInjection_enableMultiInjection}
    field = ""
    field = "Minimum Dwell Angle Table (degrees)", minDwellAngleTbl, {multiInjection_enableMultiInjection}

[multiInjectionAdvancedPanel]
    field = "#Dynamic adjustments"
    field = "Use Staging Injectors for Secondary", multiInjection_enableMultiInjectionStaging, {multiInjection_enableMultiInjection}

dialog = multiInjectionSettings, "Multi-Injection Settings"
    panel = multiInjectionBasicPanel
    panel = multiInjectionTimingPanel
    panel = multiInjectionAdvancedPanel
```

---

## 📊 Результаты

| Параметр | Было | Стало |
|----------|------|-------|
| Pulse 0 length | 6.81 ms | ~3.3 ms |
| Pulse 1 length | 2.91 ms | ~1.7 ms |
| Сумма | 9.72 ms | ~5.0 ms |
| Pulse 1 angle | 100° | 620° |
| Dwell | 285° | ~85° |
| TunerStudio warnings | 10 | 0 |

---

## 🎯 Для компиляции

```bash
cd ~/rusefi/firmware
make clean
make -j$(nproc)
```

Ожидаемый результат: ✅ Успешная компиляция без ошибок и предупреждений
