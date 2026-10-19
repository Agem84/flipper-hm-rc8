//- ---------------------------------------------------------------------------
//  Arduino-Kompatibilitaetsschicht fuer den AskSin++-Flipper-Port
//
//  Warum ARDUINO definiert wird statt "kein Arduino":
//
//    1) upstream/Atomic.h liefert bei (ARDUINO && !AVR && __arm__) eine echte
//       Cortex-M-Implementierung von ATOMIC_BLOCK ueber PRIMASK. Ohne ARDUINO
//       wird ATOMIC_BLOCK zu einem NO-OP -- und AlarmClock::add/cancel waeren
//       ungeschuetzt.
//    2) upstream/Debug.h faellt ohne ARDUINO in einen <iostream>-Zweig. Auf
//       dem Flipper ist das nicht verfuegbar.
//
//  Da upstream '#include <Arduino.h>' bzw. '#include "Arduino.h"' benutzt und
//  im upstream-Verzeichnis keine solche Datei liegt, greift der Include-Pfad
//  und landet hier.
//
//  WICHTIG -- die Pin-Funktionen sind bewusst wirkungslos:
//  Dieser Port nutzt weder AskSin++' Pin-, Button- noch Led-Layer. Der CC1101
//  wird ueber subghz_devices_* angesprochen, die Tasten kommen aus der
//  Flipper-GUI. pinMode/digitalRead/digitalWrite/attachInterrupt existieren
//  nur, damit generischer Upstream-Code uebersetzt. Sie fassen KEINE Hardware
//  an -- ein Flipper-GPIO blind zu schalten koennte externe Beschaltung
//  beschaedigen.
//- ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <furi.h>
#include <furi_hal_gpio.h>

// ---------------------------------------------------------------------------
//  Arduino-Basistypen
// ---------------------------------------------------------------------------
typedef uint8_t byte;
typedef uint16_t word;

// upstream/AskSinPP.h typedef't das nur in seinen ARDUINO_ARCH_*-Zweigen,
// upstream/Button.h:59 benutzt es aber unbedingt als Template-Parametertyp.
typedef uint32_t WiringPinMode;
#ifndef __cplusplus
typedef uint8_t boolean;
#endif

// ---------------------------------------------------------------------------
//  Digital-IO-Konstanten
// ---------------------------------------------------------------------------
#define HIGH 0x1
#define LOW  0x0

#define INPUT        0x0
#define OUTPUT       0x1
#define INPUT_PULLUP 0x2

#define LSBFIRST 0
#define MSBFIRST 1

#define CHANGE  1
#define FALLING 2
#define RISING  3

#ifndef NOT_AN_INTERRUPT
#define NOT_AN_INTERRUPT (-1)
#endif

// Zahlenbasen fuer Serial.print(v, base)
#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

#ifndef _BV
#define _BV(bit) (1UL << (bit))
#endif

// ---------------------------------------------------------------------------
//  PROGMEM
//
//  ARM ist von-Neumann: Flash ist normal adressierbar. Alle *_P-Varianten
//  sind daher schlichte Direktzugriffe.
// ---------------------------------------------------------------------------
#define PROGMEM
#define PGM_P const char*
#define PSTR(s) (s)

#define pgm_read_byte(addr)      (*(const uint8_t*)(addr))
#define pgm_read_byte_near(addr) (*(const uint8_t*)(addr))
#define pgm_read_word(addr)      (*(const uint16_t*)(addr))
#define pgm_read_word_near(addr) (*(const uint16_t*)(addr))
#define pgm_read_dword(addr)     (*(const uint32_t*)(addr))
#define pgm_read_ptr(addr)       (*(void* const*)(addr))

#define memcpy_P(d, s, n) memcpy((d), (s), (n))
#define memcmp_P(a, b, n) memcmp((a), (b), (n))
#define strcmp_P(a, b)    strcmp((a), (b))
#define strlen_P(s)       strlen((s))
#define strcpy_P(d, s)    strcpy((d), (s))

#ifndef F
#define F(str) (str)
#endif

// ---------------------------------------------------------------------------
//  Zeit
//
//  furi_get_tick() laeuft mit der FreeRTOS-Tickrate. Nie annehmen, dass
//  1 Tick == 1 ms -- deshalb ueberall furi_ms_to_ticks()/Umrechnung.
//  Zeitvergleiche IMMER als vorzeichenbehaftete Differenz, der Zaehler ist
//  uint32_t und laeuft nach ~49,7 Tagen ueber.
// ---------------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t millis(void) {
    // Ticks -> ms. Bei der Flipper-Standardrate (1 kHz) ist das die Identitaet.
    return (furi_get_tick() * 1000UL) / furi_kernel_get_tick_frequency();
}

// Grob: Aufloesung ist die Tickperiode, nicht 1 us. AskSin++ nutzt micros()
// auf dem hier aktiven Pfad nicht; die Funktion existiert nur zur
// Vollstaendigkeit. Nichts Timing-Kritisches darauf aufbauen.
static inline uint32_t micros(void) {
    return millis() * 1000UL;
}

static inline void delay(uint32_t ms) {
    furi_delay_ms(ms);
}

static inline void delayMicroseconds(uint32_t us) {
    furi_delay_us(us);
}

#ifdef __cplusplus
}
#endif

// AVR-util/delay-Ersatz. upstream/Radio.h und Storage.h benutzen diese direkt.
#define _delay_ms(ms) furi_delay_ms((uint32_t)(ms))
#define _delay_us(us) furi_delay_us((uint32_t)(us))

// ---------------------------------------------------------------------------
//  Digital-IO -- bewusst wirkungslos, siehe Dateikopf
// ---------------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif

static inline void pinMode(uint8_t pin, uint8_t mode) {
    UNUSED(pin);
    UNUSED(mode);
}

static inline int digitalRead(uint8_t pin) {
    UNUSED(pin);
    return LOW;
}

static inline void digitalWrite(uint8_t pin, uint8_t value) {
    UNUSED(pin);
    UNUSED(value);
}

static inline int analogRead(uint8_t pin) {
    UNUSED(pin);
    return 0;
}

// Von upstream/Pins.h:38 (setPWM) referenziert.
static inline void analogWrite(uint8_t pin, int value) {
    UNUSED(pin);
    UNUSED(value);
}

static inline int digitalPinToInterrupt(uint8_t pin) {
    UNUSED(pin);
    return NOT_AN_INTERRUPT;
}

static inline void attachInterrupt(int irq, void (*cb)(void), int mode) {
    UNUSED(irq);
    UNUSED(cb);
    UNUSED(mode);
}

static inline void detachInterrupt(int irq) {
    UNUSED(irq);
}

#ifdef __cplusplus
}
#endif

// Bewusst KEIN enableInterrupt()/disableInterrupt() hier:
// upstream/AskSinPP.h:61 definiert diese Makros selbst und wuerde sich an
// unserer Definition mit -Werror die Zaehne ausbeissen ("redefined").
// Verwendet werden sie ohnehin nur von upstream/Radio<>, das dieser Port
// nicht instanziiert.

// ---------------------------------------------------------------------------
//  AVR-EEPROM-Symbole
//
//  Existieren NUR, damit upstream/InternalEprom (Storage.h:264ff) uebersetzt.
//  Diese Klasse wird nie benutzt -- STORAGEDRIVER zeigt auf as::FlipperEeprom
//  --, aber sie ist eine gewoehnliche Nicht-Template-Klasse und wird deshalb
//  trotzdem compiliert. Auf AVR kaemen die Symbole aus <avr/eeprom.h>.
//
//  Sollte hier je wider Erwarten etwas durchlaufen, liefert der Lesepfad
//  0xFF (leeres EEPROM) statt Datenmuell.
// ---------------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif

static inline void eeprom_read_block(void* buf, const void* addr, size_t size) {
    UNUSED(addr);
    memset(buf, 0xFF, size);
}

static inline void eeprom_write_block(const void* buf, void* addr, size_t size) {
    UNUSED(buf);
    UNUSED(addr);
    UNUSED(size);
}

#ifdef __cplusplus
}
#endif

// ---------------------------------------------------------------------------
//  Bit- und Zahlenhelfer
// ---------------------------------------------------------------------------
#define bitRead(value, bit)            (((value) >> (bit)) & 0x01)
#define bitSet(value, bit)             ((value) |= (1UL << (bit)))
#define bitClear(value, bit)           ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))

#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

// Arduino definiert min/max als Makros -- upstream verlaesst sich darauf und
// ruft sie mit gemischten Typen auf, z. B.
// MultiChannelDevice.h:61  min(num,(uint8_t)ChannelCount).
// Als Makro (nicht als Template), weil upstream sonst -fpermissive braeuchte.
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

// ---------------------------------------------------------------------------
//  Serial-Ersatz fuer upstream/Debug.h
//
//  Debug.h gibt zeichen- und haeppchenweise aus (DPRINT/DHEX/DDEC). Wuerde
//  jeder Aufruf einzeln geloggt, waere die Ausgabe unlesbar. Deshalb wird
//  zeilenweise gepuffert und erst bei '\n' an FURI_LOG_D uebergeben.
//
//  Siehe CLAUDE.md §8: im Funk-Zeitfenster wird nicht geloggt.
// ---------------------------------------------------------------------------
#ifdef __cplusplus

class FlipperSerial {
public:
    void begin(uint32_t baud = 0) {
        UNUSED(baud);
    }
    void end() {
        flush();
    }

    void print(const char* s);
    void print(char c);
    void println();

    // Numerisch, Standardbasis dezimal (DPRINT auf Zahlen).
    template <typename T>
    void print(T v) {
        printSigned((long)v, 10);
    }

    // Mit expliziter Basis (DHEX -> 16, DDEC -> 10).
    template <typename T>
    void print(T v, int base) {
        if(base == 16) {
            printUnsigned((unsigned long)v, 16);
        } else {
            printSigned((long)v, base == 0 ? 10 : base);
        }
    }

    void flush();

private:
    void put(char c);
    void printUnsigned(unsigned long v, int base);
    void printSigned(long v, int base);

    char m_buf[128];
    size_t m_len = 0;
};

extern FlipperSerial Serial;

#endif // __cplusplus
