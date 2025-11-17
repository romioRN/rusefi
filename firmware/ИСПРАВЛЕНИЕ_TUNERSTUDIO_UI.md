# Исправление TunerStudio UI для мультивпрыска

## Проблема

TunerStudio выдавал предупреждения:
```
Warning: Nested Panel multiInjectionBasicPanel not found in UI structure. 
Either it is not defined in this ini file or it is being referenced before it was defined.
```

## Причина

Панели были **определены неправильно**. Синтаксис был:

```ini
; ❌ НЕПРАВИЛЬНО - панель определяется прямо в диалоге без имени
panel = multiInjectionBasicPanel, Center, yAxis
    field = "Enable Multi-Injection", ...
```

TunerStudio интерпретировал это как создание вложенной панели ВНУТРИ диалога, а затем пытался ее найти как отдельную панель.

## Решение

Правильный синтаксис TunerStudio:

```ini
; ✅ ПРАВИЛЬНО - сначала определяем именованную панель
[multiInjectionBasicPanel]
    field = "Enable Multi-Injection", multiInjection_enableMultiInjection

; ✅ Потом используем ее в диалоге
dialog = multiInjectionSettings, "Multi-Injection Settings"
    panel = multiInjectionBasicPanel  ; ← просто имя, без координат
    panel = multiInjectionTimingPanel
    panel = multiInjectionAdvancedPanel
```

## Ключевые отличия

### Синтаксис определения панели

```ini
[panelName]                  ; ← Используйте квадратные скобки []
    field = ...              ; ← Поля внутри
    field = ...

; НЕ используйте:
; panel = panelName, Center, yAxis    ← ЭТО не определение!
```

### Синтаксис использования панели в диалоге

```ini
dialog = myDialog, "My Dialog"
    panel = panelName        ; ← Только имя панели, БЕЗ координат
    panel = anotherPanel     ; ← Координаты автоматически управляются
```

## Что было изменено

### Было (НЕПРАВИЛЬНО):
```ini
    panel = multiInjectionBasicPanel, Center, yAxis
        field = "Enable Multi-Injection", ...
    panel = multiInjectionTimingPanel, Center, yAxis
        field = "Split Ratio Table", ...
    panel = multiInjectionAdvancedPanel, Center, yAxis
        field = "Use Staging Injectors", ...

    dialog = multiInjectionSettings, "Multi-Injection Settings"
        panel = multiInjectionBasicPanel
        panel = multiInjectionTimingPanel
        panel = multiInjectionAdvancedPanel
```

### Стало (ПРАВИЛЬНО):
```ini
[multiInjectionBasicPanel]
    field = "Enable Multi-Injection", ...

[multiInjectionTimingPanel]
    field = "Split Ratio Table", ...

[multiInjectionAdvancedPanel]
    field = "Use Staging Injectors", ...

dialog = multiInjectionSettings, "Multi-Injection Settings"
    panel = multiInjectionBasicPanel
    panel = multiInjectionTimingPanel
    panel = multiInjectionAdvancedPanel
```

## Файлы изменены

- `tunerstudio/tunerstudio.template.ini`
  - Перемещены определения панелей перед диалогом
  - Добавлены квадратные скобки для панелей
  - Удалены координаты (Center, yAxis) из определений панелей

## Результат

✅ Все предупреждения TunerStudio должны исчезнуть

## Примечания

1. **Порядок имеет значение**: Панели должны быть определены ПЕРЕД их использованием в диалоге
2. **Квадратные скобки**: Определение панели ВСЕГДА использует `[panelName]`, а не `panel = panelName`
3. **Координаты**: Координаты (Center, yAxis) автоматически управляются TunerStudio при встраивании панели в диалог
4. **Условия**: Условия {multiInjection_enableMultiInjection} все еще работают в полях
