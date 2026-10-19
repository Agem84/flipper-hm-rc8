//- ---------------------------------------------------------------------------
//  Radio-Thread
//
//  Besitzt die gesamte AskSin++-Welt exklusiv (CLAUDE.md §7.1). Weder der
//  App- noch der GUI-Thread ruft hm_service_* direkt auf -- der Zugang
//  laeuft ueber die Kommandoschlange dieses Moduls.
//
//  Warum ein eigener Thread und nicht die App-Schleife: hm_service_poll()
//  muss die AskSin-Systemuhr im 10-ms-Takt nachziehen (TICKS_PER_SECOND),
//  und hm_service_button() blockiert waehrend Device::send() bis zu rund
//  zwei Sekunden -- inklusive aller Wiederholungen und ACK-Fenster. Der
//  App-Thread blockiert dagegen in furi_message_queue_get() auf Eingaben
//  und muss dafuer jederzeit verfuegbar bleiben.
//- ---------------------------------------------------------------------------
#pragma once

#include <hm_service.h>

#include <furi.h>
#include <storage/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HmRadioThread HmRadioThread;

/** Thread starten. Der Storage-Record bleibt im Besitz des Aufrufers und
 *  muss den Thread ueberleben.
 *
 *  Kehrt immer mit einem gueltigen Zeiger zurueck: malloc() und
 *  furi_thread_alloc_ex() der Furi-Runtime crashen bei OOM sauber, statt
 *  NULL zu liefern. Ob der FUNK hochkam, sagt dagegen erst
 *  hm_radio_thread_status().radio_ok, sobald hm_radio_thread_is_started()
 *  true meldet. */
HmRadioThread* hm_radio_thread_alloc(Storage* storage);

/** Thread beenden, EEPROM sichern und Ressourcen freigeben.
 *
 *  Blockiert bis der Thread seinen laufenden Durchlauf beendet hat. Steckt
 *  er gerade in Device::send(), kann das bis zu rund zwei Sekunden dauern --
 *  ein Sendevorgang wird bewusst nicht mittendrin abgebrochen. */
void hm_radio_thread_free(HmRadioThread* t);

/** true, sobald der Thread seine Initialisierung abgeschlossen hat --
 *  unabhaengig davon, ob sie erfolgreich war. */
bool hm_radio_thread_is_started(HmRadioThread* t);

/** Tastenereignis einreihen. Nicht blockierend: ist die Schlange voll,
 *  wird verworfen und geloggt. Ein verlorener Tastendruck ist besser als
 *  ein haengender UI-Thread (CLAUDE.md §7.2). */
void hm_radio_thread_button(HmRadioThread* t, uint8_t channel, HmButtonEvent ev);

/** Anlernmodus starten. */
void hm_radio_thread_pairing(HmRadioThread* t);

/** Aktuellen Funkzustand kopieren. Threadsicher. */
void hm_radio_thread_status(HmRadioThread* t, HmStatus* out);

#ifdef __cplusplus
}
#endif
