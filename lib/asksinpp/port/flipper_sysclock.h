//- ---------------------------------------------------------------------------
//  Taktgeber fuer die AskSin++-Systemuhr
//
//  upstream/AlarmClock.h erwartet, dass eine Hardware-Timer-ISR periodisch
//  '--sysclock' aufruft (upstream/AlarmClock.cpp:19, callback()). Da wir
//  keinen der ARDUINO_ARCH_*-Zweige aktivieren, uebersetzen SysClock::init(),
//  enable() und disable() zu leeren Rumpfen -- die Uhr steht also still,
//  bis jemand sie taktet. Genau das macht diese Klasse.
//
//  Bewusst KEIN eigener FuriTimer/ISR: dadurch laeuft die komplette
//  Alarmverarbeitung im Radio-Thread-Kontext und braucht keinerlei
//  Synchronisation gegen AskSin++-Zustand (CLAUDE.md §4.2, §7.1).
//- ---------------------------------------------------------------------------
#pragma once

#include "flipper_config.h"

#include <AlarmClock.h>
#include <furi.h>

namespace as {

class FlipperSysClock {
public:
    FlipperSysClock()
        : m_last(0)
        , m_period(0) {
    }

    /** Aus dem Radio-Thread aufrufen, bevor die Schleife startet. */
    void begin() {
        m_period = furi_ms_to_ticks(ASKSIN_TICK_PERIOD_MS);
        if(m_period == 0) m_period = 1;
        m_last = furi_get_tick();
    }

    /** Einmal pro Schleifendurchlauf aufrufen. Holt so viele AskSin-Ticks
     *  nach, wie seit dem letzten Aufruf verstrichen sind. */
    void pump() {
        uint32_t now = furi_get_tick();
        // Differenzbildung, nicht Vergleich: der Tickzaehler laeuft ueber.
        while((uint32_t)(now - m_last) >= m_period) {
            m_last += m_period;
            --sysclock;
        }
    }

    /** Empfohlene maximale Blockdauer, damit kein Tick verloren geht. */
    uint32_t periodTicks() const {
        return m_period;
    }

private:
    uint32_t m_last;
    uint32_t m_period;
};

} // namespace as
