//- ---------------------------------------------------------------------------
//  Radio-Thread -- Implementierung
//- ---------------------------------------------------------------------------
#include "hm_radio_thread.h"

#include <furi.h>

#define TAG "HmRadio"

// Kommandotiefe. Ein langer Tastendruck erzeugt eine Folge aus
// LongStart + mehreren Repeats + LongEnd; 16 Plaetze fangen auch hektisches
// Tippen ab, ohne nennenswert Speicher zu kosten.
#define HM_CMD_QUEUE_DEPTH 16

typedef enum {
    HmCmdButton,
    HmCmdPairing,
} HmCmdType;

typedef struct {
    HmCmdType type;
    uint8_t channel;
    HmButtonEvent ev;
} HmCmd;

struct HmRadioThread {
    FuriThread* thread;
    FuriMessageQueue* queue;
    Storage* storage;
    volatile bool running;
    volatile bool started;
};

static int32_t hm_radio_thread_body(void* context) {
    HmRadioThread* t = (HmRadioThread*)context;

    bool ok = hm_service_init(t->storage);
    t->started = true;

    if(!ok) {
        FURI_LOG_E(TAG, "service init failed - thread idling");
        // Nicht sofort zurueckkehren: die GUI soll den Fehlerzustand
        // anzeigen koennen, statt dass die App wortlos verschwindet.
        while(t->running) {
            furi_delay_ms(50);
        }
        return -1;
    }

    const uint32_t poll_ticks = furi_ms_to_ticks(hm_service_poll_interval_ms());

    while(t->running) {
        HmCmd cmd;
        // Wartezeit ist zugleich der Polltakt: kommt kein Kommando, laeuft
        // nach poll_ticks trotzdem ein Durchlauf.
        if(furi_message_queue_get(t->queue, &cmd, poll_ticks) == FuriStatusOk) {
            switch(cmd.type) {
            case HmCmdButton:
                hm_service_button(cmd.channel, cmd.ev);
                break;
            case HmCmdPairing:
                hm_service_start_pairing();
                break;
            default:
                break;
            }
        }

        hm_service_poll();
    }

    hm_service_flush();
    hm_service_deinit();
    return 0;
}

HmRadioThread* hm_radio_thread_alloc(Storage* storage) {
    HmRadioThread* t = (HmRadioThread*)malloc(sizeof(HmRadioThread));
    t->storage = storage;
    t->running = true;
    t->started = false;
    t->queue = furi_message_queue_alloc(HM_CMD_QUEUE_DEPTH, sizeof(HmCmd));

    t->thread = furi_thread_alloc_ex("HmRadio", 4 * 1024, hm_radio_thread_body, t);

    // Normal, NICHT High.
    //
    // CLAUDE.md §7.1 hatte urspruenglich eine hoehere Prioritaet vorgesehen,
    // damit das ACK-Fenster nicht auf einen Bildaufbau wartet. Auf der
    // Hardware zeigte sich das Gegenteil: der CC1101 teilt sich den internen
    // SPI-Bus mit dem Display, und ein hoeher priorisierter Pollthread
    // entzieht dem GUI-Service den Bus ("ViewPort lockup").
    //
    // Die BidCos-Timings brauchen die Prioritaet nicht: die Systemuhr laeuft
    // mit 10 ms, empfangene Telegramme warten im RX-FIFO, und das
    // ACK-Fenster ist ~200 ms breit.
    furi_thread_set_priority(t->thread, FuriThreadPriorityNormal);
    furi_thread_start(t->thread);

    return t;
}

void hm_radio_thread_free(HmRadioThread* t) {
    if(t == NULL) return;

    t->running = false;
    furi_thread_join(t->thread);
    furi_thread_free(t->thread);
    furi_message_queue_free(t->queue);
    free(t);
}

bool hm_radio_thread_is_started(HmRadioThread* t) {
    return t != NULL && t->started;
}

static void hm_radio_thread_post(HmRadioThread* t, const HmCmd* cmd) {
    if(t == NULL) return;
    // Timeout 0 -- der App-Thread darf hier unter keinen Umstaenden haengen.
    if(furi_message_queue_put(t->queue, cmd, 0) != FuriStatusOk) {
        FURI_LOG_W(TAG, "command queue full, dropping");
    }
}

void hm_radio_thread_button(HmRadioThread* t, uint8_t channel, HmButtonEvent ev) {
    HmCmd cmd;
    cmd.type = HmCmdButton;
    cmd.channel = channel;
    cmd.ev = ev;
    hm_radio_thread_post(t, &cmd);
}

void hm_radio_thread_pairing(HmRadioThread* t) {
    HmCmd cmd;
    cmd.type = HmCmdPairing;
    cmd.channel = 0;
    cmd.ev = HmButtonShort;
    hm_radio_thread_post(t, &cmd);
}

void hm_radio_thread_status(HmRadioThread* t, HmStatus* out) {
    if(out == NULL) return;
    if(t == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    hm_service_status(out);
}
