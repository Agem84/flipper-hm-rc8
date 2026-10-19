//- ---------------------------------------------------------------------------
//  Status-LED-Adapter
//
//  AskSin<> erwartet einen Led-Typ mit init()/set(). upstream/Led.h ist
//  Arduino-Pin-basiert und damit auf dem Flipper wirkungslos, deshalb dieser
//  Ersatz auf furi_hal_light.
//
//  Bewusst zustandslos und nicht-blockierend: set() wird aus dem Radio-Thread
//  aufgerufen, teils direkt vor einer Aussendung. Blinkmuster mit delay()
//  wuerden BidCos-Timings zerstoeren (CLAUDE.md §8.1).
//- ---------------------------------------------------------------------------
#pragma once

#include <furi_hal_light.h>
#include <stdint.h>

namespace as {

class FlipperLed {
public:
    // Spiegelt upstream/Led.h LedStates::Mode
    enum {
        MODE_NOTHING = 0,
        MODE_PAIRING = 1,
        MODE_SEND = 2,
        MODE_ACK = 3,
        MODE_NACK = 4,
        MODE_BAT_LOW = 5,
        MODE_WELCOME = 6,
        MODE_KEY_LONG = 7,
        MODE_FAILURE = 8,
    };

    void init() {
        furi_hal_light_set(LightRed, 0);
        furi_hal_light_set(LightGreen, 0);
        furi_hal_light_set(LightBlue, 0);
        m_mode = MODE_NOTHING;
    }

    // upstream/MultiChannelDevice.h:196 schaltet die LED um das Senden von
    // Statusmeldungen herum voruebergehend ab und stellt den alten Zustand
    // danach wieder her. Deshalb zwei Ueberladungen: Getter und Setter.
    bool enable() const {
        return m_enabled;
    }

    void enable(bool value) {
        m_enabled = value;
        if(!value) {
            furi_hal_light_set(LightRed, 0);
            furi_hal_light_set(LightGreen, 0);
            furi_hal_light_set(LightBlue, 0);
        }
    }

    void set(uint8_t mode) {
        m_mode = mode;
        if(!m_enabled) return;
        uint8_t r = 0, g = 0, b = 0;
        switch(mode) {
        case MODE_PAIRING:
        case MODE_WELCOME:
            b = 0x40;
            break;
        case MODE_SEND:
        case MODE_KEY_LONG:
            g = 0x20;
            break;
        case MODE_ACK:
            g = 0x60;
            break;
        case MODE_NACK:
        case MODE_FAILURE:
            r = 0x60;
            break;
        case MODE_BAT_LOW:
            r = 0x30;
            g = 0x20;
            break;
        default:
            break;
        }
        furi_hal_light_set(LightRed, r);
        furi_hal_light_set(LightGreen, g);
        furi_hal_light_set(LightBlue, b);
    }

    uint8_t mode() const {
        return m_mode;
    }

    // Von AskSin<> im Idle-/Wakeup-Pfad erwartet.
    void ledOn(uint8_t ticks = 0) {
        (void)ticks;
    }
    void ledOff() {
        set(MODE_NOTHING);
    }
    void invert(bool value) {
        (void)value;
    }

private:
    uint8_t m_mode = MODE_NOTHING;
    bool m_enabled = true;
};

} // namespace as
