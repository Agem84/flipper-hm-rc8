//- ---------------------------------------------------------------------------
//  Homematic RC-8 -- Entry-Point und Eingabeschleife
//
//  Diese Datei inkludiert bewusst KEINEN AskSin++-Header (CLAUDE.md §2.2.2).
//  Der Funk laeuft im Radio-Thread, der Zugang dorthin ueber
//  src/radio/hm_radio_thread.h.
//
//  Threadaufteilung (CLAUDE.md §7.1):
//    GUI-Service-Thread  zeichnet nur, liest den Modell-Snapshot
//    App-Thread (hier)   Eingaben -> Kommandos, Modellpflege
//    Radio-Thread        AskSin++ exklusiv
//- ---------------------------------------------------------------------------
#include "radio/hm_radio_thread.h"
#include "ui/hm_view.h"

#include <furi.h>
#include <furi_hal_power.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>

#define TAG "HmRc8"

#define INPUT_QUEUE_DEPTH 8

// Auffrischtakt der Anzeige. Der Funk laeuft unabhaengig davon im
// Radio-Thread; 100 ms sind fuer die Statusanzeige mehr als genug.
#define UI_REFRESH_MS 100

namespace {

/** Laeuft im GUI-Service-Thread -- nur weiterreichen, nichts rechnen.
 *
 *  Begrenzte Wartezeit statt FuriWaitForever: haengt der App-Thread, darf
 *  der GUI-Service nicht mit ihm haengen. 50 ms sind grosszuegig genug, dass
 *  bei einer alle 100 ms geleerten Schlange der Tiefe 8 praktisch nie ein
 *  Ereignis verloren geht -- verlorene Release-Ereignisse waeren
 *  unangenehm, weil dann ein langer Tastendruck nie beendet wuerde. */
void input_callback(InputEvent* event, void* context) {
    FuriMessageQueue* queue = (FuriMessageQueue*)context;
    furi_message_queue_put(queue, event, furi_ms_to_ticks(50));
}

/** Kanalauswahl im 4x2-Raster verschieben. Bewusst begrenzend statt
 *  umlaufend: bei acht Tasten ist ein Anschlag vorhersehbarer als ein
 *  Sprung ans andere Ende. */
uint8_t move_selection(uint8_t selected, InputKey key) {
    uint8_t idx = selected - 1;
    uint8_t col = idx % 4;
    uint8_t row = idx / 4;

    switch(key) {
    case InputKeyLeft:
        if(col > 0) col--;
        break;
    case InputKeyRight:
        if(col < 3) col++;
        break;
    case InputKeyUp:
        row = 0;
        break;
    case InputKeyDown:
        row = 1;
        break;
    default:
        break;
    }
    return row * 4 + col + 1;
}

} // namespace

extern "C" int32_t hm_rc8_app(void* p) {
    UNUSED(p);

    // Build-Kennung ins Log. Eine bereits laufende FAP-Instanz lebt im RAM
    // weiter, auch wenn 'ufbt launch' die Datei auf der SD-Karte schon
    // ersetzt hat -- ohne diese Zeile ist im Log nicht zu unterscheiden,
    // welcher Stand gerade laeuft.
    FURI_LOG_I(TAG, "starting (build " __DATE__ " " __TIME__ ")");

    // Der Flipper darf waehrend des Funkbetriebs nicht einschlafen.
    furi_hal_power_insomnia_enter();

    Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
    Gui* gui = (Gui*)furi_record_open(RECORD_GUI);

    FuriMessageQueue* input_queue = furi_message_queue_alloc(INPUT_QUEUE_DEPTH, sizeof(InputEvent));

    HmView* view = hm_view_alloc();
    view_port_input_callback_set(hm_view_viewport(view), input_callback, input_queue);
    gui_add_view_port(gui, hm_view_viewport(view), GuiLayerFullscreen);

    // Kein mkdir fuer /ext/apps_data/<appid>/: das erledigt der
    // Storage-Service beim Aufloesen des /data-Alias.
    HmRadioThread* radio = hm_radio_thread_alloc(storage);

    HmViewModel model;
    memset(&model, 0, sizeof(model));
    model.selected = 1;

    bool running = true;

    while(running) {
        InputEvent event;
        FuriStatus status =
            furi_message_queue_get(input_queue, &event, furi_ms_to_ticks(UI_REFRESH_MS));

        if(status == FuriStatusOk) {
            switch(event.key) {
            case InputKeyBack:
                if(event.type == InputTypeShort) {
                    running = false;
                } else if(event.type == InputTypeLong) {
                    FURI_LOG_I(TAG, "pairing requested by user");
                    hm_radio_thread_pairing(radio);
                }
                break;

            case InputKeyOk:
                // Der lange Druck ist in BidCos eine Folge: LongStart,
                // beliebig viele Repeats, dann LongEnd. Ein einzelnes
                // Telegramm wuerde die CCU nicht als PRESS_CONT werten.
                if(event.type == InputTypeShort) {
                    hm_radio_thread_button(radio, model.selected, HmButtonShort);
                } else if(event.type == InputTypeLong) {
                    model.long_active = true;
                    hm_radio_thread_button(radio, model.selected, HmButtonLongStart);
                } else if(event.type == InputTypeRepeat) {
                    hm_radio_thread_button(radio, model.selected, HmButtonLongRepeat);
                } else if(event.type == InputTypeRelease) {
                    if(model.long_active) {
                        model.long_active = false;
                        hm_radio_thread_button(radio, model.selected, HmButtonLongEnd);
                    }
                }
                break;

            case InputKeyUp:
            case InputKeyDown:
            case InputKeyLeft:
            case InputKeyRight:
                // Waehrend eines laufenden langen Drucks nicht umschalten --
                // sonst ginge das abschliessende LongEnd an einen anderen
                // Kanal als der Druckbeginn.
                if(!model.long_active &&
                   (event.type == InputTypeShort || event.type == InputTypeRepeat)) {
                    model.selected = move_selection(model.selected, event.key);
                }
                break;

            default:
                break;
            }
        }

        model.radio_started = hm_radio_thread_is_started(radio);
        hm_radio_thread_status(radio, &model.radio);
        hm_view_set_model(view, &model);
    }

    // Abbau spiegelbildlich (CLAUDE.md §7.4).
    hm_radio_thread_free(radio);

    gui_remove_view_port(gui, hm_view_viewport(view));
    hm_view_free(view);
    furi_message_queue_free(input_queue);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    furi_hal_power_insomnia_exit();

    FURI_LOG_I(TAG, "stopped");
    return 0;
}
