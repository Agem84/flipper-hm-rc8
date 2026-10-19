//- ---------------------------------------------------------------------------
//  Ersatz fuer <avr/pgmspace.h>
//
//  WOFUER DAS DA IST:
//  upstream/aes.cpp inkludiert ungeguarded '#include <avr/pgmspace.h>' (nur
//  der ESP32-Zweig darueber nimmt <pgmspace.h>). Genau deshalb war die Datei
//  bisher aus der Quellenliste in application.fam ausgeschlossen -- und damit
//  auch das AES-Signing.
//
//  Statt upstream zu patchen (CLAUDE.md §2.2.1) reicht dieser Shim: das
//  Verzeichnis port/arduino_compat/ steht in fap_include_paths, also loest
//  <avr/pgmspace.h> genau hierher auf.
//
//  Die eigentlichen Makros (PROGMEM, pgm_read_byte, ...) stehen in Arduino.h
//  und sind auf ARM schlichte Direktzugriffe -- von-Neumann, Flash ist normal
//  adressierbar (CLAUDE.md §4.1). Hier wird nur weitergereicht, damit es
//  genau eine Definition gibt.
//- ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
