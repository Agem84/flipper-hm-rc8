//- ---------------------------------------------------------------------------
//  Bildschirm der Fernbedienung
//
//  Der Draw-Callback laeuft im GUI-Service-Thread, NICHT im App-Thread
//  (CLAUDE.md §7.1). Er darf ausschliesslich aus dem Modell-Snapshot lesen.
//  Deshalb haelt dieses Modul eine mutexgeschuetzte Kopie: der App-Thread
//  schreibt sie mit hm_view_set_model(), der Draw-Callback kopiert sie unter
//  Lock und rendert danach OHNE gehaltenen Mutex.
//- ---------------------------------------------------------------------------
#pragma once

#include <hm_service.h>

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_port.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HM_CHANNEL_COUNT 8

typedef struct {
    HmStatus radio; // Momentaufnahme aus dem Radio-Thread
    uint8_t selected; // aktuell gewaehlter Kanal, 1..8
    bool long_active; // OK wird gerade lang gehalten
    bool radio_started; // Radio-Thread hat seine Init abgeschlossen
} HmViewModel;

typedef struct HmView HmView;

HmView* hm_view_alloc(void);
void hm_view_free(HmView* v);

/** ViewPort zum Anmelden am GUI-Record. Bleibt im Besitz des HmView. */
ViewPort* hm_view_viewport(HmView* v);

/** Modell aktualisieren und Neuzeichnen anstossen. Aus dem App-Thread. */
void hm_view_set_model(HmView* v, const HmViewModel* model);

#ifdef __cplusplus
}
#endif
