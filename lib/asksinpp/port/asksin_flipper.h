//- ---------------------------------------------------------------------------
//  Sammel-Header des AskSin++-Flipper-Ports
//
//  Anwendungscode inkludiert AUSSCHLIESSLICH diese Datei, niemals AskSinPP.h
//  direkt. Grund ist die Reihenfolge: as::FlipperEeprom muss vollstaendig
//  deklariert sein, bevor upstream/Storage.h daraus
//  'class Storage : public StorageWrapper<STORAGEDRIVER>' bildet
//  (upstream/Storage.h:709). Wer AskSinPP.h zuerst zieht, bekommt einen
//  unverstaendlichen Template-Fehler.
//- ---------------------------------------------------------------------------
#pragma once

// 1) Compile-Time-Schalter.
//    Die global wirksamen (ARDUINO, NORTC, TICKS_PER_SECOND, E2END,
//    STORAGEDRIVER) kommen zusaetzlich aus application.fam, damit JEDE
//    Uebersetzungseinheit der Bibliothek dieselben Werte sieht -- auch die
//    upstream-.cpp-Dateien, die diesen Header nie sehen. Waeren sie nur
//    hier gesetzt, haetten AlarmClock.cpp und der Rest der App
//    unterschiedliche Tickraten und Storage-Layouts.
#include "flipper_config.h"

// 2) Arduino-Ersatz. Muss vor allen AskSin++-Headern stehen.
#include <Arduino.h>

// 3) Storage-Treiber -- vor upstream/Storage.h.
#include "flipper_eeprom.h"

// 4) Die Bibliothek selbst.
#include <AskSinPP.h>
#include <MultiChannelDevice.h>
#include <Remote.h>

// 5) Flipper-Implementierungen der HAL-Bausteine.
#include "flipper_led.h"
#include "flipper_radio.h"
#include "flipper_sysclock.h"

namespace as {

/** Die HAL-Instanziierung dieses Projekts.
 *
 *  NoBattery: der Flipper meldet keinen Batteriestatus an die Zentrale.
 *  Sollte das spaeter gewuenscht sein, ist das ein eigener Adapter auf
 *  furi_hal_power_get_battery_voltage() -- kein Grund, jetzt schon einen
 *  Platzhalter einzubauen.
 */
typedef AskSin<FlipperLed, NoBattery, FlipperRadio> FlipperHal;

} // namespace as
