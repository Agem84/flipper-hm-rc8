//- ---------------------------------------------------------------------------
//  Bildschirm der Fernbedienung -- Implementierung
//
//  Layout auf 128x64:
//    y  0..10   Titelbalken (invertiert): Geraetetyp links, eigene ID rechts
//    y 13..29   Kanaele 1-4
//    y 31..47   Kanaele 5-8
//    y 50..56   Zustandszeile (Zentrale / letztes Ereignis)
//    y 57..63   Kontexthinweis zur Bedienung
//- ---------------------------------------------------------------------------
#include "hm_view.h"

#include <furi.h>
#include <gui/gui.h>

#include <stdio.h>
#include <string.h>

struct HmView {
    ViewPort* viewport;
    FuriMutex* mutex;
    HmViewModel model;
};

// Rasterabmessungen. 4 Spalten a 30 px mit 1 px Fuge passen genau in 128.
#define CELL_W    30
#define CELL_H    16
#define CELL_X0   2
#define CELL_DX   31
#define ROW1_Y    13
#define ROW2_Y    31

static void hm_view_draw_grid(Canvas* canvas, const HmViewModel* m) {
    char label[4];

    for(uint8_t ch = 1; ch <= HM_CHANNEL_COUNT; ch++) {
        uint8_t idx = ch - 1;
        uint8_t col = idx % 4;
        uint8_t row = idx / 4;

        int x = CELL_X0 + col * CELL_DX;
        int y = (row == 0) ? ROW1_Y : ROW2_Y;

        bool selected = (m->selected == ch);

        if(selected) {
            canvas_draw_rbox(canvas, x, y, CELL_W, CELL_H, 2);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_rframe(canvas, x, y, CELL_W, CELL_H, 2);
            canvas_set_color(canvas, ColorBlack);
        }

        snprintf(label, sizeof(label), "%u", ch);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(
            canvas, x + CELL_W / 2, y + CELL_H / 2, AlignCenter, AlignCenter, label);

        // Langer Druck wird nur an der gewaehlten Taste markiert -- ein
        // zusaetzliches Symbol an allen Zellen wuerde das Raster zumuellen.
        if(selected && m->long_active) {
            canvas_set_font(canvas, FontSecondary);
            canvas_draw_str(canvas, x + 3, y + CELL_H - 3, "L");
        }

        canvas_set_color(canvas, ColorBlack);
    }
}

static void hm_view_draw_callback(Canvas* canvas, void* context) {
    HmView* v = (HmView*)context;

    // Unter Lock NUR kopieren. Gezeichnet wird danach ohne gehaltenen
    // Mutex, sonst blockiert der GUI-Thread den App-Thread (CLAUDE.md §7.2).
    HmViewModel m;
    furi_mutex_acquire(v->mutex, FuriWaitForever);
    memcpy(&m, &v->model, sizeof(m));
    furi_mutex_release(v->mutex);

    canvas_clear(canvas);

    char buf[40];

    // --- Titelbalken ------------------------------------------------------
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, 128, 11);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 9, "HM-RC-X");

    // Funkzaehler in der Mitte des Titelbalkens. Sie beantworten die Frage,
    // an der ein fehlgeschlagenes Anlernen sonst haengenbleibt: geht
    // ueberhaupt etwas raus, und hoeren wir irgendjemanden?
    //   T>0 R=0 -> wir senden, aber niemand antwortet
    //   T=0     -> es geht nichts raus (Region-Sperre / Funk-Init)
    //   R>0     -> der Funkweg steht in beide Richtungen
    snprintf(buf, sizeof(buf), "T%u R%u", m.radio.tx_frames, m.radio.rx_frames);
    canvas_draw_str_aligned(canvas, 64, 9, AlignCenter, AlignBottom, buf);

    snprintf(
        buf,
        sizeof(buf),
        "%02X%02X%02X",
        m.radio.device_id[0],
        m.radio.device_id[1],
        m.radio.device_id[2]);
    canvas_draw_str_aligned(canvas, 126, 9, AlignRight, AlignBottom, buf);
    canvas_set_color(canvas, ColorBlack);

    // --- Fehlerfall: kein Funk -------------------------------------------
    if(m.radio_started && !m.radio.radio_ok) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignCenter, "Funk nicht bereit");
        canvas_set_font(canvas, FontSecondary);
        // Kurz halten: FontSecondary passt bei 128 px nur rund 25 Zeichen.
        canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignCenter, "CC1101-Init gescheitert");
        canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignCenter, "Log: ufbt cli -> log");
        return;
    }

    // --- Startphase -------------------------------------------------------
    if(!m.radio_started) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignCenter, "Funk startet ...");
        return;
    }

    // --- Tastenraster -----------------------------------------------------
    hm_view_draw_grid(canvas, &m);

    // --- Zustandszeile ----------------------------------------------------
    canvas_set_font(canvas, FontSecondary);

    switch(m.radio.pair_state) {
    case HmPairStatePaired:
        snprintf(
            buf,
            sizeof(buf),
            "CCU %02X%02X%02X",
            m.radio.master_id[0],
            m.radio.master_id[1],
            m.radio.master_id[2]);
        break;
    case HmPairStatePairing:
        snprintf(buf, sizeof(buf), "Anlernen laeuft ...");
        break;
    default:
        snprintf(buf, sizeof(buf), "Nicht angelernt");
        break;
    }
    canvas_draw_str(canvas, 2, 56, buf);

    // Letztes Ereignis rechts: K = kurz, L = lang, Kanalnummer, Quittung.
    // last_channel == 0 heisst "noch nichts gesendet".
    if(m.radio.last_channel >= 1) {
        const char* ack;
        switch(m.radio.tx_result) {
        case HmTxSending:
            ack = "...";
            break;
        case HmTxAck:
            ack = "ACK";
            break;
        case HmTxNack:
            ack = "NAK";
            break;
        case HmTxTimeout:
            ack = "T/O";
            break;
        case HmTxBlocked:
            ack = "RGN";
            break;
        case HmTxAesReq:
            // Die Zentrale hat eine Signatur verlangt und die Antwort nicht
            // quittiert. Bewusst NICHT "NAK": das Telegramm ist angekommen,
            // nur der Schluessel passt nicht.
            ack = "AES";
            break;
        default:
            // Broadcast bzw. Telegramm ohne BIDI -- es gibt nichts zu
            // quittieren, ein "T/O" waere hier schlicht falsch.
            ack = "ok";
            break;
        }
        snprintf(
            buf,
            sizeof(buf),
            "%c%u %s",
            m.radio.last_longpress ? 'L' : 'K',
            m.radio.last_channel,
            ack);
        canvas_draw_str_aligned(canvas, 126, 56, AlignRight, AlignBottom, buf);
    }

    // --- Kontexthinweis + RSSI --------------------------------------------
    //
    // Beide Texte teilen sich 128 px in FontSecondary (grob 5 px/Zeichen).
    // Die Hinweise sind deshalb bewusst kurz gehalten -- der frueher hier
    // stehende Satz "OK senden - BACK lang: neu anlernen" war rund 180 px
    // breit und lief rechts aus dem Bild.
    const char* hint;
    if(m.radio.tx_result == HmTxBlocked) {
        hint = "TX gesperrt (Region)";
    } else if(m.radio.tx_result == HmTxAesReq) {
        hint = "AES-Schluessel falsch";
    } else if(m.radio.pair_state == HmPairStatePaired) {
        hint = "OK sendet Kanal";
    } else {
        hint = "BACK lang: anlernen";
    }
    canvas_draw_str(canvas, 2, 63, hint);

    // RSSI des letzten Empfangs. 0 = seit dem Start nichts gehoert; dann
    // bleibt der Platz leer statt "-74dBm" vorzutaeuschen.
    if(m.radio.rssi_raw != 0) {
        snprintf(buf, sizeof(buf), "%ddBm", hm_rssi_to_dbm(m.radio.rssi_raw));
        canvas_draw_str_aligned(canvas, 126, 63, AlignRight, AlignBottom, buf);
    }
}

HmView* hm_view_alloc(void) {
    HmView* v = (HmView*)malloc(sizeof(HmView));
    memset(v, 0, sizeof(HmView));

    v->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    v->model.selected = 1;

    v->viewport = view_port_alloc();
    view_port_draw_callback_set(v->viewport, hm_view_draw_callback, v);

    return v;
}

void hm_view_free(HmView* v) {
    if(v == NULL) return;
    view_port_free(v->viewport);
    furi_mutex_free(v->mutex);
    free(v);
}

ViewPort* hm_view_viewport(HmView* v) {
    return v->viewport;
}

void hm_view_set_model(HmView* v, const HmViewModel* model) {
    if(v == NULL || model == NULL) return;

    // ------------------------------------------------------------------
    //  NUR BEI ECHTER AENDERUNG NEU ZEICHNEN
    //
    //  Grund ist die Warnung "[W][ViewPort] ViewPort lockup" im Log.
    //  view_port_update() nimmt den ViewPort-Mutex mit nur 2 ms Timeout
    //  und meldet beim Scheitern genau diese Zeile
    //  (applications/services/gui/view_port.c:187). Gehalten wird derselbe
    //  Mutex vom GUI-Service, solange der Draw-Callback laeuft -- und der
    //  zeichnet acht gerundete Kacheln mit Schriftwechseln, was laenger
    //  als 2 ms dauert.
    //
    //  Die App-Schleife rief hier bisher bei JEDEM Durchlauf auf, also
    //  mindestens alle UI_REFRESH_MS und zusaetzlich bei jedem
    //  Eingabeereignis -- waehrend eines langen Tastendrucks feuert
    //  InputTypeRepeat dicht hintereinander. Die meisten dieser Aufrufe
    //  trugen keine neue Information und kollidierten nur mit dem
    //  laufenden Bildaufbau.
    //
    //  memcmp ist hier zulaessig, weil das Modell reines POD ist und der
    //  Aufrufer es einmalig genullt hat -- Fuellbytes sind damit stabil.
    //
    //  Das ist kein Ersatz fuer §7.1: gezeichnet wird weiterhin
    //  ausschliesslich im GUI-Thread aus dem Snapshot.
    // ------------------------------------------------------------------
    furi_mutex_acquire(v->mutex, FuriWaitForever);
    bool changed = (memcmp(&v->model, model, sizeof(v->model)) != 0);
    if(changed) {
        memcpy(&v->model, model, sizeof(v->model));
    }
    furi_mutex_release(v->mutex);

    if(changed) {
        view_port_update(v->viewport);
    }
}
