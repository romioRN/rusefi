#include "pch.h"
#include "hella_oil_level.h"

#if EFI_HELLA_OIL

#include "digital_input_exti.h"

static void hellaSensorExtiCallback(void* arg, efitick_t nowNt) {
    reinterpret_cast<HellaOilLevelSensor*>(arg)->onEdge(nowNt);
}

void HellaOilLevelSensor::init(brain_pin_e pin) {
    if (!isBrainPinValid(pin)) {
        return;
    }

#if EFI_PROD_CODE
    if (efiExtiEnablePin(getSensorName(), pin, PAL_EVENT_MODE_BOTH_EDGES,
        hellaSensorExtiCallback, reinterpret_cast<void*>(this)) < 0) {
        return;
    }
#endif

    m_pin = pin;

    Register();
}

void HellaOilLevelSensor::deInit() {
    if (!isBrainPinValid(m_pin)) {
        return;
    }

#if EFI_PROD_CODE
    efiExtiDisablePin(m_pin);
#endif

    m_pin = Gpio::Unassigned;
}

void HellaOilLevelSensor::onEdge(efitick_t nowNt) {
    if (efiReadPin(m_pin)) {
        // На фронте подъёма — начинаем измерение длительности импульса
        m_pulseTimer.reset(nowNt);

        float timeBetweenPulses = m_betweenPulseTimer.getElapsedSecondsAndReset(nowNt);

        if (timeBetweenPulses > 0.89f * 0.780f && timeBetweenPulses < 1.11f * 0.780f) {
            // Большой интервал (~780ms) — следующий импульс температурный
            m_nextPulse = NextPulse::Temp;
        } else if (timeBetweenPulses > 0.89f * 0.110f && timeBetweenPulses < 1.11f * 0.110f) {
            // Короткий интервал (~110ms) — следующее состояние в последовательности
            switch (m_nextPulse) {
            case NextPulse::Temp:
                m_nextPulse = NextPulse::Level;
                break;
            case NextPulse::Level:
                m_nextPulse = NextPulse::Diag;
                break;
            default:
                m_nextPulse = NextPulse::None;
                break;
            }
        } else {
            // Пауза слишком длинная — сброс состояния
            m_nextPulse = NextPulse::None;
        }
    } else {
        // На спаде — измеряем длительность импульса
        float lastPulseMs = 1000.0f * m_pulseTimer.getElapsedSeconds(nowNt);

        if (lastPulseMs > 100.0f || lastPulseMs < 20.0f) {
            m_nextPulse = NextPulse::None;
            return;
        }

        if (m_nextPulse == NextPulse::Diag) {
            // TODO: обработать диагностический импульс, пока игнорируем
            return;
        } else if (m_nextPulse == NextPulse::Temp) {
            // Запоминаем длительность импульса температуры в микросекундах
            lastPulseWidthTempUs = lastPulseMs * 1000.0f;

            if (lastPulseMs < 22.8f) {
                // Короткое замыкание (Low)
                // invalidate(UnexpectedCode::Low);
            } else if (lastPulseMs > 87.2f) {
                // Датчик неисправен (High)
                // invalidate(UnexpectedCode::High);
            } else {
                float tempC = interpolateClamped(23.f, -40.f, 87.f, 160.f, lastPulseMs);
                // Записать валидное значение температуры
                // setValidValue(tempC, nowNt);
            }
        } else if (m_nextPulse == NextPulse::Level) {
            // Запоминаем длительность импульса уровня в микросекундах
            lastPulseWidthLevelUs = lastPulseMs * 1000.0f;

            if (lastPulseMs < 22.8f) {
                // Ненадёжный сигнал
                // invalidate(UnexpectedCode::Low);
            } else {
                float levelMm = interpolateClamped(23.f, 0.f, 87.86f, 150.f, lastPulseMs);
                // Записать валидное значение уровня
                setValidValue(levelMm, nowNt);
            }
        }
    }
}

#endif // EFI_HELLA_OIL
