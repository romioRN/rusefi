# Чек-лист проверки мультивпрыска

## ✅ Расчет МАСС (injectionMass)
- [x] Масса считается для каждого цилиндра независимо
- [x] Масса уже включает trim по цилиндру и банку
- [x] Это базовое значение для расчета впрыска

## ✅ Расчет ДЛИТЕЛЬНОСТИ Pulse 0
- [x] Используется стандартная логика `computeInjectionAngle()`
- [x] Рассчитывается как: `openingAngle = injectionOffset - durationAngle`
- [x] `injectionOffset` берется из обычной таблицы timing
- [x] Результат - начало впрыска

## ✅ Расчет ДЛИТЕЛЬНОСТИ Pulse 1
- [x] Угол берется из таблицы `secondInjectionAngleTable` (НЕЗАВИСИМО)
- [x] Применяется timing mode correction (START/CENTER/END)
- [x] Per-cylinder firing order offset добавляется

## ❌ ПРОБЛЕМЫ КОТОРЫЕ ИСПРАВЛЕНЫ

### Проблема 1: Длительность впрыска (FIXED)
**Было:** Использовались масштабированные значения из `injectionDuration`
```cpp
baseFuelMs = getEngineState()->injectionDuration;  // ✗ НЕПРАВИЛЬНО
pulse0Ms = baseFuelMs * 0.70;
pulse1Ms = baseFuelMs * 0.30;
```

**Исправлено:** Используются реальные массы с независимым расчетом
```cpp
baseMass = getEngineState()->injectionMass[cyl];  // ✓ ПРАВИЛЬНО
pulse0Ms = injectorModel->getInjectionDuration(baseMass * 0.70);
pulse1Ms = injectorModel->getInjectionDuration(baseMass * 0.30);
```

### Потенциальная проблема 2: Timing Mode Correction для Pulse 1

**Текущий код:**
```cpp
if (mode == InjectionTimingMode::Center) {
    correctedAngle -= durationAngle * 0.5f;  // Shift BACK
} else if (mode == InjectionTimingMode::End) {
    correctedAngle -= durationAngle;  // Shift BACK
}
```

**Вопрос:** Это правильно применяет timing mode correction для Pulse 1?

Давайте разберемся:
- Если mode = CENTER, то угол из таблицы считается ЦЕНТРОМ впрыска
  - Поэтому нужно сдвинуть НАЧАЛО на -duration/2
  - Это правильно: `correctedAngle -= durationAngle * 0.5f` ✓

- Если mode = END, то угол из таблицы считается КОНЦОМ впрыска
  - Поэтому нужно сдвинуть НАЧАЛО на -duration
  - Это правильно: `correctedAngle -= durationAngle` ✓

**Вывод:** Timing mode correction выглядит правильно ✓

### Потенциальная проблема 3: Дwell валидация

**Текущий код:**
```cpp
float pulse0End = getPulse(0).startAngle + getPulse(0).durationAngle;
float pulse1Start = getPulse(1).startAngle;
float dwell = pulse1Start - pulse0End;
if (dwell < 0) dwell += 720.0f;
return (dwell >= minDwell);
```

**Анализ:**
- `pulse0End` = начало + длительность ✓
- `dwell` = расстояние между концом pulse0 и началом pulse1 ✓
- При отрицательном значении добавляется 720° (переход через границу) ✓
- Проверка минимума ✓

**Вывод:** Валидация выглядит правильно ✓

## Что нужно проверить после исправления

1. **Компиляция**
   - [ ] Нет ошибок компиляции
   - [ ] Нет warning'ов про injectorModel

2. **Логи консоли**
   ```
   mi_angles
   ```
   Должны видеть:
   - [ ] Pulse 0 duration ~3-4 ms
   - [ ] Pulse 1 duration ~1-2 ms
   - [ ] Total duration ~5 ms (не 9.72 ms!)

3. **Осцилоскоп**
   - [ ] Pulse 0 видна на графике Injection 1
   - [ ] Pulse 1 видна на графике Injection 2
   - [ ] Длительности меньше чем было
   - [ ] Сумма примерно равна обычному впрыску

4. **Двигатель**
   - [ ] Двигатель работает нормально
   - [ ] Нет пропусков зажигания
   - [ ] Расход топлива разумный

## Возможные дополнительные оптимизации

1. **Улучшение таблицы Pulse 1**
   - Может быть нужна таблица "смещения от Pulse 0" вместо абсолютных углов?
   - Это упростило бы конфигурацию и понимание

2. **Улучшение валидации Dwell**
   - Может быть добавить логику для автоматического сдвига Pulse 1 если перекрытие?
   - Или уведомление пользователю что нужно изменить таблицу

3. **Логирование**
   - Добавить отладку в console для вывода рассчитанных масс для каждого импульса
   - Помогло бы при отладке конфигурации

## Краткий итог

✅ **Исправлено:** Длительность мультивпрыска теперь рассчитывается правильно через injectorModel

✅ **Углы:** Рассчитываются правильно (Pulse 0 по обычной логике, Pulse 1 независимо из таблицы)

✅ **Валидация:** Dwell проверяется корректно

⏳ **Ожидание:** Тестирования на железе и осцилоскопе
