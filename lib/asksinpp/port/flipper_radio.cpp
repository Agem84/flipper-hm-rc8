//- ---------------------------------------------------------------------------
//  FlipperRadio -- AskSin++/BidCos auf dem internen CC1101
//- ---------------------------------------------------------------------------
#include "flipper_radio.h"

#include <furi_hal_gpio.h>
#include <stdio.h>
#include <string.h>

#define TAG ASKSIN_LOG_TAG

namespace as {

// ---------------------------------------------------------------------------
//  RadioTrace -- Diagnose ohne Logging im Funk-Zeitfenster (CLAUDE.md §8.1)
// ---------------------------------------------------------------------------

RadioTrace::Entry RadioTrace::s_ring[RadioTrace::CAPACITY];
uint8_t RadioTrace::s_head = 0;
uint8_t RadioTrace::s_count = 0;
uint16_t RadioTrace::s_lost = 0;

RadioTrace::Entry& RadioTrace::acquire() {
    Entry& e = s_ring[s_head];
    s_head = (uint8_t)((s_head + 1) % CAPACITY);
    if(s_count < CAPACITY) {
        s_count++;
    } else {
        // Puffer voll -- der aelteste Eintrag wird ueberschrieben. Bewusst
        // so herum: der juengste Funkverkehr ist der interessante.
        s_lost++;
    }
    e.tick = furi_get_tick();
    return e;
}

void RadioTrace::frame(char dir, const uint8_t* buf, uint8_t len, int16_t rssi_dbm, uint8_t lqi) {
    Entry& e = acquire();
    e.what = NULL;
    e.a = 0;
    e.b = 0;
    e.dir = dir;
    e.rssi = rssi_dbm;
    e.lqi = lqi;
    e.len = len;

    uint8_t copy = (len > sizeof(e.data)) ? (uint8_t)sizeof(e.data) : len;
    if(buf != NULL && copy > 0) memcpy(e.data, buf, copy);
    if(copy < sizeof(e.data)) memset(e.data + copy, 0, sizeof(e.data) - copy);
}

void RadioTrace::note(const char* what, uint32_t a, uint32_t b) {
    Entry& e = acquire();
    e.what = what;
    e.a = a;
    e.b = b;
    e.dir = 0;
    e.rssi = 0;
    e.lqi = 0;
    e.len = 0;
}

void RadioTrace::drain() {
    if(s_count == 0 && s_lost == 0) return;

    uint16_t lost = s_lost;
    s_lost = 0;

    if(lost != 0) {
        FURI_LOG_W(TAG, "trace overflow, %u entries lost", lost);
    }

    // Vom aeltesten zum juengsten ausgeben.
    uint8_t idx = (uint8_t)((s_head + CAPACITY - s_count) % CAPACITY);
    uint8_t n = s_count;
    s_count = 0;

    for(uint8_t i = 0; i < n; i++) {
        const Entry& e = s_ring[idx];
        idx = (uint8_t)((idx + 1) % CAPACITY);

        if(e.what != NULL) {
            FURI_LOG_I(TAG, "[%lu] %s %lu %lu", (unsigned long)e.tick, e.what, (unsigned long)e.a, (unsigned long)e.b);
            continue;
        }

        // Layout ab Byte 0: cnt, flag, typ, from[3], to[3], payload...
        // Das Laengenbyte ist nicht Teil des Puffers (Message::buffer()).
        // 3 Zeichen je Byte (Entry::data fasst 24), dazu "..." und die Null.
        char hex[3 * 24 + 8];
        size_t p = 0;
        uint8_t show = (e.len > sizeof(e.data)) ? (uint8_t)sizeof(e.data) : e.len;
        for(uint8_t k = 0; k < show && p + 4 < sizeof(hex); k++) {
            p += (size_t)snprintf(hex + p, sizeof(hex) - p, "%02X ", e.data[k]);
        }
        if(show < e.len && p + 4 < sizeof(hex)) {
            p += (size_t)snprintf(hex + p, sizeof(hex) - p, "...");
        }
        hex[p] = '\0';

        if(e.dir == 'R') {
            FURI_LOG_I(
                TAG,
                "[%lu] RX len=%u rssi=%d lqi=%u | %s",
                (unsigned long)e.tick,
                e.len,
                e.rssi,
                e.lqi,
                hex);
        } else {
            FURI_LOG_I(TAG, "[%lu] TX len=%u | %s", (unsigned long)e.tick, e.len, hex);
        }
    }
}

namespace {

// ---------------------------------------------------------------------------
//  CC1101-Registersatz fuer BidCos
//
//  QUELLE: upstream/Radio-CC1101.h, initVal[] im SIMPLE_CC1101_INIT-Zweig
//  (dort ist der Array-Index identisch mit der Registeradresse, siehe die
//  Schleife 'initReg(i, initVal[i])' bei Radio-CC1101.h:313). Jeder Wert ist
//  von dort uebernommen -- nichts abgeleitet, nichts geraten
//  (CLAUDE.md §5.3.1).
//
//  Warum die vollstaendige Tabelle 0x00..0x26 und nicht nur die Register aus
//  dem #else-Zweig: dort verlaesst sich upstream darauf, dass die
//  uebrigen Register auf CC1101-Reset-Defaults stehen. Der Flipper hat den
//  Chip aber vor uns schon konfiguriert. Alles explizit zu setzen ist
//  strikt sicherer und benutzt trotzdem ausschliesslich Upstream-Werte.
//
//  Format von furi_hal_subghz_load_custom_preset: (reg, value)-Paare,
//  terminiert durch (0x00, 0x00), danach genau 8 PATABLE-Bytes.
//  VERIFIZIERT gegen firmware.elf 1.4.3, furi_hal_subghz_load_custom_preset
//  @0x0800f734 (Registerschleife + load_patable ab preset[i+2]).
//
//  ACHTUNG -- REGISTER 0x00 (IOCFG2) DARF HIER NICHT VORKOMMEN:
//
//    800f75a: ldrb r4, [r5, r6]   ; r4 = preset[i]
//    800f75c: adds r6, #2
//    800f75e: cbnz r4, ...        ; r4 == 0  ->  Schleife ENDE
//
//  Die Firmware bricht die Liste beim ersten Registerbyte 0x00 ab. Stand
//  IOCFG2 an Position 0, wurde KEIN EINZIGES Register geschrieben und die
//  8 PATABLE-Bytes kamen aus den Folgebytes der Tabelle -- der CC1101 blieb
//  auf Reset-Defaults (115 kBaud, Syncword D391, kein Whitening) und die
//  Zentrale konnte uns unmoeglich hoeren.
//
//  Die firmwareeigenen Presets lassen 0x00 aus demselben Grund weg, z. B.
//  subghz_device_cc1101_preset_gfsk_9_99kb_async_regs @0x080a3f8e beginnt
//  mit "02 06". Ein einzelnes Register ist aus einem FAP nicht setzbar:
//  furi_hal_subghz_write_reg ist nicht exportiert, und
//  furi_hal_subghz_load_registers hat dieselbe Terminator-Semantik.
//
//  Folge: IOCFG2 bleibt auf dem Wert, den furi_hal_subghz_reset() setzt
//  (CC1101-Default 0x29, CHIP_RDY). Upstream wollte dort 0x2E (tri-state).
//  Das ist folgenlos -- GDO2 ist auf dem Flipper unbeschaltet, und AskSin++
//  wertet den Pin nirgends aus.
// ---------------------------------------------------------------------------
uint8_t asksin_preset[] = {
    // KEIN 0x00/IOCFG2 -- siehe Kopf oben.
    0x01, 0x2E, // IOCFG1   - GDO1 tri-state (ungenutzt)
    0x02, 0x06, // IOCFG0   - GDO0: high ab Sync-Wort, low am Paketende
    0x03, 0x0D, // FIFOTHR  - TX FIFO 9 / RX FIFO 56 Byte
    0x04, 0xE9, // SYNC1    - BidCos Sync-Wort MSB
    0x05, 0xCA, // SYNC0    - BidCos Sync-Wort LSB
    0x06, 0xFF, // PKTLEN
    0x07, 0x0C, // PKTCTRL1 - CRC autoflush + append status
    0x08, 0x45, // PKTCTRL0 - Whitening, CRC, variable Paketlaenge
    0x09, 0x00, // ADDR
    0x0A, 0x00, // CHANNR
    0x0B, 0x06, // FSCTRL1
    0x0C, 0x00, // FSCTRL0
    0x0D, 0x21, // FREQ2    - 868.299866 MHz
    0x0E, 0x65, // FREQ1
    0x0F, 0x6A, // FREQ0
    0x10, 0xC8, // MDMCFG4  - Kanalbandbreite
    0x11, 0x93, // MDMCFG3  - Symbolrate ~10 kBaud
    0x12, 0x03, // MDMCFG2  - 2-FSK, 30 von 32 Sync-Bits muessen passen
    0x13, 0x22, // MDMCFG1
    0x14, 0xF8, // MDMCFG0
    0x15, 0x34, // DEVIATN  - 19.042969 kHz
    0x16, 0x07, // MCSM2
    0x17, 0x03, // MCSM1    - RX nach TX (kein CCA; upstream ohne USE_CCA)
    0x18, 0x18, // MCSM0    - Autokalibrierung IDLE->RX/TX
    0x19, 0x16, // FOCCFG
    0x1A, 0x6C, // BSCFG
    0x1B, 0x43, // AGCCTRL2
    0x1C, 0x40, // AGCCTRL1
    0x1D, 0x91, // AGCCTRL0
    0x1E, 0x2F, // WOREVT1
    0x1F, 0x65, // WOREVT0
    0x20, 0x78, // WORCTRL
    0x21, 0x56, // FREND1
    0x22, 0x10, // FREND0   - PA_POWER = 0 -> nur PATABLE[0] wird benutzt
    0x23, 0xE9, // FSCAL3
    0x24, 0x2A, // FSCAL2
    0x25, 0x1F, // FSCAL1
    0x26, 0x11, // FSCAL0
    0x00, 0x00, // Ende der Registerliste
    // ---------------------------------------------------------------------
    //  PATABLE -- ebenfalls der Upstream-Wert.
    //
    //  Die 0x03 in initVal[] (Radio-CC1101.h:384) ist NICHT der Endwert:
    //  direkt nach der Registerschleife ueberschreibt Radio-CC1101.h:409
    //  ihn mit
    //      initReg(CC1101_PATABLE, PA_MaxPower);   // PA_MaxPower == 0xC0
    //  0xC0 ist also das, was AskSin++ tatsaechlich in den Chip schreibt.
    //
    //  Bestaetigt wird das von der Flipper-Firmware selbst: ihr
    //  GFSK-9.99-kb-Preset (unsere Modulation und Symbolrate) benutzt
    //  dieselben acht Bytes -- subghz_device_cc1101_preset_gfsk_9_99kb_
    //  async_regs @0x080a3f8e endet auf "00 00 C0 00 00 00 00 00 00 00".
    //  Entspricht rund +10 dBm und damit der Groessenordnung echter
    //  Homematic-Geraete.
    //
    //  FREND0 oben steht auf 0x10 (PA_POWER = 0), es zaehlt also
    //  PATABLE[0]. Zum Vergleich: das OOK-Preset der Firmware nutzt
    //  "00 C0 ..." mit FREND0 = 0x11, weil OOK einen Null-Eintrag fuer den
    //  Aus-Zustand braucht.
    //
    //  868,0-868,6 MHz erlaubt 25 mW (+14 dBm) bei 1 % Duty Cycle. Gesendet
    //  wird nur auf bewusste Benutzeraktion (CLAUDE.md §12).
    // ---------------------------------------------------------------------
    0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// CC1101-Quarz des Flipper.
const uint32_t CC1101_XOSC_HZ = 26000000UL;

// Wartezeit auf das Sync-Wort nach dem TX-Strobe. Die Praeambel ist bei
// ~10 kBaud nur wenige Millisekunden lang; 15 ms sind reichlich. Bewusst
// knapp gehalten, weil dieser Wert bei unbrauchbarem GDO0 pro Sendevorgang
// verloren geht.
const uint32_t TX_SYNC_TIMEOUT_MS = 15;
const uint32_t TX_EXTRA_MARGIN_MS = 40;

// Obergrenze fuer das Warten auf ein vollstaendig eingelaufenes Telegramm.
// Das laengste BidCos-Telegramm sind 64 Byte; bei ~10 kBaud sind das rund
// 52 ms. 80 ms lassen Reserve fuer Praeambel und Jitter, begrenzen aber die
// Blockdauer im Radio-Thread, falls GDO0 dauerhaft high haengt (FIFO-
// Overflow). Nach Ablauf wird der FIFO verworfen und neu aufgesetzt.
const uint32_t RX_PACKET_TIMEOUT_MS = 80;

// Abstand, in dem der Empfaenger vorsorglich neu scharf gestellt wird.
// Begruendung an FlipperRadio::keepRxAlive(). 100 ms sind unkritisch
// gegenueber dem ~200 ms breiten ACK-Fenster und kosten nur rund zehn
// zusaetzliche SPI-Transaktionen pro Sekunde -- der Bus hat laut
// CLAUDE.md §7.1 erst bei einigen hundert pro Sekunde Probleme gemacht.
const uint32_t RX_REARM_INTERVAL_MS = 100;

} // namespace

FlipperRadio::FlipperRadio()
    : m_open(false)
    , m_gdo0(NULL)
    , m_idle(true)
    , m_alive(true)
    , m_gdo0_seen(false)
    , m_rssi(0)
    , m_next_send_tick(0)
    , m_next_rearm_tick(0)
    , m_tx_result(TxNone)
    , m_tx_count(0)
    , m_tx_expect_ack(false)
    , m_tx_saw_aes(false)
    , m_tx_to()
    , m_tx_frames(0)
    , m_rx_frames(0)
    , m_rx_crc_err(0)
    , m_rx_busy(0)
    , m_rx_oversized(0)
    , m_aes_challenges(0)
    , m_aes_keyindex(0xFF) {
}

// ---------------------------------------------------------------------------
//  Lebenszyklus
// ---------------------------------------------------------------------------

bool FlipperRadio::init() {
    // Idempotent: Device::init(hal) ruft intern AskSin<>::init(), das
    // radio.init() ein zweites Mal aufruft.
    if(m_open) {
        reset();
        return true;
    }

    // Kein subghz_devices_* mehr -- Begruendung im Header. Die Firmware hat
    // den CC1101 beim Boot ueber furi_hal_subghz_init() bereits aufgesetzt
    // (SPI-Handle, GPIO, Antennenpfad); wir setzen mit dem Preset darauf auf.
    // Ein eigener Init-Aufruf ist weder noetig noch moeglich:
    // furi_hal_subghz_init() steht in api_symbols.csv mit '-'.
    //
    // Kein separates furi_hal_subghz_reset() davor: load_custom_preset ruft
    // es als erstes selbst auf (firmware.elf @0x0800f744).
    furi_hal_subghz_load_custom_preset(asksin_preset);

    // Zaehlt die tatsaechlich uebertragenen Registerpaare. Die Firmware
    // bricht die Liste beim ersten Registerbyte 0x00 ab (siehe Kopf oben) --
    // steht hier eine ueberraschend kleine Zahl, ist genau diese Falle
    // wieder zugeschnappt und das komplette Preset wirkungslos.
    unsigned regs = 0;
    while(asksin_preset[2 * regs] != 0x00) regs++;
    FURI_LOG_I(TAG, "preset loaded: %u registers + 8 patable", regs);

    if(!furi_hal_subghz_is_frequency_valid(ASKSIN_FREQUENCY_HZ)) {
        FURI_LOG_E(TAG, "frequency %lu Hz rejected", (unsigned long)ASKSIN_FREQUENCY_HZ);
        furi_hal_subghz_sleep();
        return false;
    }

    // Muss NACH load_preset laufen -- setzt FREQ2/1/0 neu und waehlt
    // ausserdem den passenden Antennenpfad (868 MHz).
    uint32_t real = furi_hal_subghz_set_frequency_and_path(ASKSIN_FREQUENCY_HZ);
    FURI_LOG_I(TAG, "radio on %lu Hz (requested %lu)", (unsigned long)real, (unsigned long)ASKSIN_FREQUENCY_HZ);

    m_gdo0 = furi_hal_subghz_get_data_gpio();
    if(m_gdo0 == NULL) {
        // Ohne GDO0 laufen sowohl waitTxComplete() als auch waitRxComplete()
        // blind. Empfangen wird dann unzuverlaessig -- sichtbar machen.
        FURI_LOG_E(TAG, "no GDO0 pin - TX/RX timing runs blind");
    }
    enableGdo0();

    furi_hal_subghz_flush_rx();
    furi_hal_subghz_rx();
    m_open = true;
    m_idle = false;
    m_alive = true;

    return true;
}

void FlipperRadio::enableGdo0() {
    if(m_gdo0 == NULL) return;
    // ZWINGEND nach JEDEM furi_hal_subghz_reset() -- und das steckt auch in
    // furi_hal_subghz_load_custom_preset() drin. Die Firmware setzt den Pin
    // dort auf GpioModeAnalog (firmware.elf @0x0800f706, mode == 5):
    //
    //   furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeAnalog, GpioPullNo, ...)
    //   cc1101_switch_to_idle(); cc1101_reset();
    //   cc1101_write_reg(CC1101_IOCFG0, 0x2E);
    //
    // Im Analog-Modus ist der Schmitt-Trigger abgeschaltet, furi_hal_gpio_read()
    // liefert dann konstant 0 -- das Sync-Wort wird nie erkannt und jeder
    // Sendevorgang faellt in waitTxComplete() auf die rechnerische Dauer
    // zurueck. IOCFG0 = 0x06 kommt aus unserem Preset wieder rein.
    furi_hal_gpio_init(m_gdo0, GpioModeInput, GpioPullNo, GpioSpeedLow);
}

void FlipperRadio::deinit() {
    if(!m_open) return;
    // Den Chip schlafen legen, sonst zieht er weiter Strom und die naechste
    // App findet ihn in unserem BidCos-Registersatz vor.
    furi_hal_subghz_idle();
    furi_hal_subghz_sleep();

    if(m_gdo0 != NULL) {
        // GDO0 so hinterlassen, wie die Firmware ihn erwartet: Analog ist
        // der stromsparende Ruhezustand aus furi_hal_subghz_init().
        furi_hal_gpio_init(m_gdo0, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    }

    m_open = false;
    m_gdo0 = NULL;
    m_idle = true;
}

// ---------------------------------------------------------------------------
//  Empfang
// ---------------------------------------------------------------------------

bool FlipperRadio::waitRxComplete() {
    // ------------------------------------------------------------------
    //  WARUM DIESES WARTEN DER ENTSCHEIDENDE TEIL DES EMPFANGS IST
    //
    //  furi_hal_subghz_rx_pipe_not_empty() meldet NICHT "ein Telegramm ist
    //  da", sondern nur "NUM_RXBYTES > 0" -- also bereits das erste Byte
    //  eines noch laufenden Empfangs (firmware.elf bzw.
    //  targets/f7/furi_hal/furi_hal_subghz.c, liest CC1101_STATUS_RXBYTES).
    //
    //  Ein BidCos-Telegramm ist bei ~10 kBaud rund 20-30 ms lang, der
    //  Radio-Thread pollt aber alle 10 ms (CLAUDE.md §7.1). Der Poll trifft
    //  also fast immer MITTEN in ein einlaufendes Paket. Dann steht im
    //  LQI-Register das CRC_OK-Bit noch nicht, is_rx_data_crc_valid()
    //  liefert false -- und der alte Code hat an dieser Stelle flushrx()
    //  gerufen und damit das gerade einlaufende Telegramm weggeworfen.
    //
    //  Damit gingen praktisch alle Antworten der Zentrale verloren: das
    //  DEVINFO ging raus, die Konfigurationstelegramme der CCU wurden
    //  zerstoert, das Anlernen kam nie zustande.
    //
    //  Die Firmware macht es in ihrem eigenen Packet-Mode-Worker exakt so
    //  wie hier -- erst GDO0 abwarten, dann den FIFO auswerten
    //  (lib/subghz/subghz_tx_rx_worker.c, subghz_tx_rx_worker_rx()).
    //  IOCFG0 = 0x06 aus unserem Preset macht GDO0 genau dazu: high ab dem
    //  Sync-Wort, low am Paketende.
    // ------------------------------------------------------------------
    if(m_gdo0 == NULL) return true;
    if(!furi_hal_gpio_read(m_gdo0)) return true; // kein Empfang im Gange

    // Ab hier ist belegt, dass der Pin lebt (Regel 5.2.1 eingehalten).
    m_gdo0_seen = true;
    m_rx_busy++;

    uint32_t start = furi_get_tick();
    uint32_t limit = furi_ms_to_ticks(RX_PACKET_TIMEOUT_MS);

    while((uint32_t)(furi_get_tick() - start) < limit) {
        // furi_delay_ms() gibt die CPU ab -- furi_delay_us() waere ein
        // Busy-Wait und wuerde dem GUI-Service den SPI-Bus entziehen.
        furi_delay_ms(1);
        if(!furi_hal_gpio_read(m_gdo0)) return true;
    }

    // GDO0 haengt high: FIFO-Overflow oder Dauertraeger. Aufraeumen, sonst
    // bleibt der Empfaenger fuer immer stehen.
    RadioTrace::note("rx stuck, fifo reset", RX_PACKET_TIMEOUT_MS);
    flushrx();
    return false;
}

void FlipperRadio::keepRxAlive() {
    // ------------------------------------------------------------------
    //  MCSM1 = 0x03 bedeutet RXOFF_MODE = IDLE: nach JEDEM abgeschlossenen
    //  Empfang faellt der CC1101 in IDLE und hoert nicht weiter zu. Nach
    //  einem sauberen Telegramm ist das unkritisch, weil read() am Ende
    //  ohnehin flushrx() ruft und damit SRX strobt.
    //
    //  Der stille Fall ist ein Telegramm mit CRC-Fehler: PKTCTRL1 = 0x0C
    //  hat CRC_AUTOFLUSH gesetzt, der Chip raeumt den FIFO selbst ab und
    //  geht nach IDLE. Fuer uns sieht das aus wie "nichts empfangen" --
    //  rx_pipe_not_empty() ist false, read() kehrt zurueck, und der
    //  Empfaenger bleibt fuer immer in IDLE stehen. Erst der naechste
    //  eigene Sendevorgang weckt ihn wieder (MCSM1 schaltet nach TX zurueck
    //  nach RX).
    //
    //  Im 868-MHz-Band mit fremdem Funkverkehr und einer Sync-Erkennung,
    //  die schon bei 30 von 32 Bits anschlaegt (MDMCFG2 = 0x03), passiert
    //  das regelmaessig -- und danach ist das Geraet taub, ohne dass
    //  irgendetwas auffaellt.
    //
    //  Upstream hat dieses Problem nicht: rcvData() beendet JEDEN Aufruf
    //  mit flushrx() + SRX (Radio-CC1101.h:551), wird aber nur aus der
    //  GDO0-ISR heraus gerufen. Wir pollen (CLAUDE.md §5.4) und koennen uns
    //  ein flushrx() pro Durchlauf nicht leisten, deshalb hier entzerrt.
    // ------------------------------------------------------------------
    if((int32_t)(m_next_rearm_tick - furi_get_tick()) > 0) return;

    // Letzter Blick auf GDO0: faengt der Chip in genau diesem Augenblick
    // ein Sync-Wort, wird nichts angefasst -- der naechste Durchlauf holt
    // das Telegramm regulaer ab.
    if(m_gdo0 != NULL && furi_hal_gpio_read(m_gdo0)) return;

    // Der volle Weg idle -> flush -> rx, nicht bloss ein SRX-Strobe: aus
    // RXFIFO_OVERFLOW (moeglich nach einem Fehl-Sync mit absurdem
    // Laengenbyte) kommt der Chip mit SRX allein nicht heraus. Dann liefe
    // furi_hal_subghz_rx() in sein furi_check(wait_status_state(RX, ...))
    // und beendete die App mit einem Furi-Crash.
    // flushrx() zieht m_next_rearm_tick selbst nach.
    flushrx();
}

uint8_t FlipperRadio::read(Message& msg) {
    msg.length(0);
    if(!m_open || m_idle) return 0;

    // ZWINGEND VOR jedem Zugriff auf FIFO, CRC-Bit und RSSI -- Begruendung
    // in waitRxComplete(). Ohne dieses Warten wird jedes zweite Telegramm
    // mitten im Empfang verworfen.
    if(!waitRxComplete()) return 0;

    if(!furi_hal_subghz_rx_pipe_not_empty()) {
        keepRxAlive();
        return 0;
    }

    // Bei ungueltiger CRC nichts an die AskSin-Statemachine geben.
    // PKTCTRL1 = 0x0C hat CRC_AUTOFLUSH gesetzt, der Chip raeumt also
    // normalerweise selbst auf; hier landen wir nur bei Resten.
    if(!furi_hal_subghz_is_rx_data_crc_valid()) {
        m_rx_crc_err++;
        RadioTrace::note("rx crc invalid", m_rx_crc_err);
        flushrx();
        return 0;
    }

    // RSSI VOR dem Flush lesen. furi_hal_subghz_get_rssi() liefert den
    // Live-Pegel des Empfaengers -- nachdem flushrx() den Chip durch IDLE
    // geschickt und RX neu gestartet hat, steht dort der frische
    // Rauschpegel, nicht mehr der des empfangenen Pakets. Der Wert geht
    // ueber Device::sendAck() in ausgehende Telegramme an die Zentrale.
    float dbm = furi_hal_subghz_get_rssi();
#ifdef HMRC8_TRACE_TELEGRAMS
    // Ebenfalls VOR dem Flush -- danach gehoert der Wert zum naechsten,
    // noch gar nicht empfangenen Paket.
    uint8_t lqi = furi_hal_subghz_get_lqi();
#endif

    // raw[] ist exakt so gross wie der Firmware-Puffer: cc1101_read_fifo
    // (@0x0801d3c8) klemmt die Laenge hart auf 64 (cmp #64 / movcs #64),
    // ein Ueberlauf ist damit ausgeschlossen. 'len' ist reiner Ausgabewert.
    uint8_t raw[ASKSIN_MAX_RAW_LEN];
    uint8_t len = 0;
    furi_hal_subghz_read_packet(raw, &len);

    // PKTCTRL1 haengt zwei Statusbytes an, die read_packet nicht abholt.
    // Ohne Flush wuerde das naechste Telegramm daran verrutschen.
    // flushrx() kehrt selbst nach RX zurueck.
    flushrx();

    if(len == 0) return 0;

    if(len > m_buf.buffersize()) {
        // Jeder Treffer ist ein verlorenes Telegramm der Zentrale. Passiert
        // das beim Anlernen, ist MaxDataLen zu klein (siehe
        // flipper_config.h) -- deshalb mitzaehlen statt still verwerfen.
        m_rx_oversized++;
        RadioTrace::note("rx oversized, dropped", len, m_buf.buffersize());
        return 0;
    }

    m_alive = true;
    if(len >= 10) m_rx_frames++;

    // AskSin++ erwartet hier den BETRAG des Pegels in dBm, nicht den
    // CC1101-Rohwert: Radio-CC1101.h:469 rechnet
    //   rss = -1 * ((raw - (raw >= 128 ? 256 : 0)) / 2 - 74)
    // also raw = 28 -> rss = 60 fuer -60 dBm. Der Wert wandert ueber
    // Device::sendAck() und sendInfoActuatorStatus() in Telegramme an die
    // Zentrale. Vorher stand hier die Umkehrrechnung auf den Rohwert
    // ((dBm + 74) * 2) -- damit meldeten wir der CCU durchgaengig einen um
    // rund 40 dB zu guten Empfang.
    int32_t level = (int32_t)(-dbm + 0.5f);
    if(level < 0) level = 0;
    if(level > 255) level = 255;
    m_rssi = (uint8_t)level;

    // buffer() zeigt auf &cnt -- das Laengenbyte ist NICHT Teil davon, und
    // furi_hal_subghz_read_packet liefert es bereits separat in 'len'.
    memcpy(m_buf.buffer(), raw, len);
    m_buf.length(len);
    m_buf.decode();

    memcpy(msg.buffer(), m_buf.buffer(), len);
    msg.length(len);
    m_buf.clear();

#ifdef HMRC8_TRACE_TELEGRAMS
    // In den Ringpuffer, NICHT direkt ins Log: read() laeuft mitten im
    // ACK-Fenster (Device::waitResponse pollt alle 10 ms).
    RadioTrace::frame('R', msg.buffer(), len, (int16_t)dbm, lqi);
#endif

    // Signierte Antwort der Zentrale auf unsere eigene Challenge. Sie
    // gehoert NICHT zur Quittungsverfolgung unten (anderer Zaehler), wird
    // aber gebraucht, um "Zentrale hat gar nicht signiert" von "Signatur
    // kam an, passte aber nicht" zu unterscheiden.
    if(msg.isResponseAes()) {
        RadioTrace::note("rx aes signature from central", msg.length());
    }

    // Quittung zum zuletzt gesendeten Telegramm mitschneiden. Device::send()
    // wertet das zwar selbst aus, gibt das Ergebnis aber nur als bool an
    // RemoteChannel::state() zurueck -- und das ist void. Fuer die Anzeige
    // waere es damit verloren.
    //
    // Gleiche Pruefung wie Device::waitResponse(): Zaehler und Absender.
    if(m_tx_result == TxPending && msg.length() >= 10 && msg.count() == m_tx_count &&
       msg.from() == m_tx_to) {
        // REIHENFOLGE ZWINGEND: isChallengeAes() VOR isAck().
        //
        // Eine Challenge ist AS_MESSAGE_RESPONSE mit comm == 0x04, und
        // isAck() prueft nur '(comm & 0x80) == 0x00' -- das trifft auf
        // 0x04 zu. Andersherum geprueft meldete die App also einen
        // erfolgreichen Sendevorgang, waehrend die Zentrale noch auf die
        // Signatur wartet und der Tastendruck dort nie ankommt.
        if(msg.isChallengeAes()) {
            m_tx_result = TxAesChallenge;
            m_tx_saw_aes = true;
            m_aes_challenges++;
            m_aes_keyindex = msg.aesChallenge().keyindex();
            RadioTrace::note("rx aes challenge, key index", m_aes_keyindex, m_aes_challenges);
        } else if(msg.isAck()) {
            m_tx_result = TxAck;
        } else if(msg.isNack()) {
            m_tx_result = TxNack;
        }
    }

    return msg.length();
}

// ---------------------------------------------------------------------------
//  Senden
// ---------------------------------------------------------------------------

bool FlipperRadio::write(const Message& msg, uint8_t burst) {
    if(!m_open) return false;

    uint8_t len = msg.length();
    if(len == 0 || len > m_buf.buffersize()) {
        RadioTrace::note("tx refused, bad length", len, m_buf.buffersize());
        m_tx_result = TxFailed;
        return false;
    }

    // Merken, worauf eine Quittung passen wuerde. Device::send() wiederholt
    // bis zu transmitDevTryMax() mal -- jeder Versuch setzt das hier neu,
    // am Ende steht also das Ergebnis des letzten Versuchs.
    m_tx_count = msg.count();
    m_tx_to = msg.to();
    m_tx_expect_ack = msg.ackRequired() && msg.to().valid();
    m_tx_result = TxPending;

    if(burst != 0) {
        // Echter BidCos-Burst ist ein ~360 ms Dauertraeger vor dem Paket,
        // damit Wake-on-Radio-Empfaenger aufwachen. Ueber die
        // Packet-Mode-API der Firmware laesst sich das nicht erzeugen.
        // Fuer den Pfad Fernbedienung -> Zentrale ist Burst nicht noetig
        // (CLAUDE.md §5.5); das Telegramm geht ohne ihn raus.
        RadioTrace::note("burst requested, sent plain");
    }

#ifdef HMRC8_TRACE_TELEGRAMS
    // Ringpuffer statt Log: ab hier laeuft das Sendefenster (CLAUDE.md §8.1).
    RadioTrace::frame('T', msg.buffer(), len, 0, 0);
#endif

    // Die Gegenrichtung des Signierens sichtbar machen: mit aktivem
    // AES_ACTIVE verlangt AskSin++ auch von der ZENTRALE eine Signatur und
    // schickt ihr dafuer selbst eine Challenge
    // (upstream/Device.h::requestSignature). Dieser Pfad umgeht
    // Device::send() und laeuft direkt ueber radio().write() -- ohne die
    // Notiz hier waere er im Log nicht von einem normalen Telegramm zu
    // unterscheiden. upstream loggt ihn nur ueber DPRINT, und das ist
    // unter -DNDEBUG tot (CLAUDE.md §8).
    if(msg.isChallengeAes()) {
        RadioTrace::note("tx aes challenge to central");
    }

    waitSendSlot();

    // Sperrfrist SOFORT neu starten, nicht erst am Ende von write().
    //
    // upstream/Radio.h::MinSendTimeout::waitTimeout() macht es genauso: es
    // wartet die alte Frist ab und armiert die naechste unmittelbar danach,
    // also VOR der Aussendung. Die 100 ms ueberlappen dort die Sendedauer.
    //
    // Stand das hier am Ende von write(), addierten sich Sendedauer und die
    // 40 ms Reserve aus waitTxComplete() obendrauf -- effektiv rund 170 ms
    // statt 100 ms zwischen zwei Telegrammen. Waehrend der
    // Konfigurationsfolge beim Anlernen schickt die Zentrale mehrere
    // Telegramme kurz hintereinander, die jeweils binnen ~200 ms quittiert
    // werden wollen; die zusaetzliche Wartezeit frisst dieses Fenster auf.
    setSendTimeout(ASKSIN_SEND_DELAY_MS);

    wakeup(true);

    memcpy(m_buf.buffer(), msg.buffer(), len);
    m_buf.length(len);
    m_buf.encode();

    furi_hal_subghz_idle();
    furi_hal_subghz_flush_tx();
    furi_hal_subghz_write_packet(m_buf.buffer(), len);

    bool ok = furi_hal_subghz_tx();
    if(!ok) {
        // Regionssperre der Firmware. Muss sichtbar sein, nicht verschluckt
        // werden (CLAUDE.md §5.5). Hier ausnahmsweise direkt ins Log: es
        // gibt kein ACK-Fenster zu schuetzen, weil gar nichts rausging.
        FURI_LOG_E(TAG, "TX blocked by region setting - nothing was sent");
        RadioTrace::note("tx blocked by region");
        m_tx_result = TxBlocked;
        furi_hal_subghz_flush_tx();
        furi_hal_subghz_rx();
        m_idle = false;
        m_buf.clear();
        return false;
    }

    m_tx_frames++;

    if(!waitTxComplete(len)) {
        RadioTrace::note("tx did not complete in time", len);
        m_tx_result = TxFailed;
        ok = false;
    } else if(!m_tx_expect_ack) {
        // Broadcast oder Telegramm ohne BIDI-Flag -- es kommt keine Antwort,
        // TxPending wuerde sonst faelschlich als Timeout gewertet.
        m_tx_result = TxSent;
    }

    // MCSM1 = 0x03 schaltet nach TX selbstaendig zurueck nach RX. Hier NICHT
    // blind flush_rx() aufrufen: waitTxComplete() kehrt erst bis zu ~40 ms
    // nach dem Paketende zurueck (bei totem GDO0 sogar spaeter), ein frueh
    // eintreffendes ACK liegt dann schon im FIFO und wuerde geloescht.
    // Nur wenn nichts anliegt, wird aufgeraeumt.
    if(!furi_hal_subghz_rx_pipe_not_empty()) {
        furi_hal_subghz_idle();
        furi_hal_subghz_flush_rx();
        furi_hal_subghz_rx();
    }
    m_idle = false;

    m_buf.clear();
    // Kein setSendTimeout() mehr an dieser Stelle -- die Frist laeuft seit
    // dem Beginn der Aussendung (siehe oben).
    return ok;
}

bool FlipperRadio::waitTxComplete(uint8_t payload_len) {
    // Rechnerische Sendedauer bei ~10 kBaud: Praeambel, Sync, Laengenbyte,
    // Nutzdaten und CRC sind rund 8 Byte Overhead, dazu Reserve.
    uint32_t airtime_ms = ((uint32_t)(payload_len + 8) * 8UL) / 10UL + TX_EXTRA_MARGIN_MS;

    if(m_gdo0 == NULL) {
        furi_delay_ms(airtime_ms);
        return true;
    }

    // IOCFG0 = 0x06: GDO0 geht mit dem Sync-Wort auf high und am Paketende
    // wieder auf low.
    //
    // GEWARTET WIRD MIT furi_delay_ms(), NICHT furi_delay_us().
    // furi_delay_us() ist auf dem Flipper ein Busy-Wait ohne Yield. Der
    // CC1101 haengt am selben internen SPI-Bus wie das Display; ohne Yield
    // verhungert der GUI-Service und die Firmware meldet "ViewPort lockup".
    uint32_t start = furi_get_tick();
    uint32_t sync_timeout = furi_ms_to_ticks(TX_SYNC_TIMEOUT_MS);
    bool saw_sync = false;

    while((uint32_t)(furi_get_tick() - start) < sync_timeout) {
        if(furi_hal_gpio_read(m_gdo0)) {
            saw_sync = true;
            // Beweis, dass der Pin nicht im Analogmodus haengt (Regel 5.2.1).
            m_gdo0_seen = true;
            break;
        }
        furi_delay_ms(1);
    }

    if(!saw_sync) {
        RadioTrace::note("tx: no gdo0 sync, using computed airtime", airtime_ms);
        // GDO0 liefert nichts Verwertbares. Das heisst NICHT, dass nicht
        // gesendet wurde -- der Strobe war erfolgreich, nur die
        // Rueckmeldeleitung schweigt. Auf die rechnerische Dauer
        // zurueckfallen, statt den Sendevorgang als gescheitert zu melden
        // und AskSin++ in eine Wiederholschleife zu schicken.
        furi_delay_ms(airtime_ms);
        return true;
    }

    start = furi_get_tick();
    uint32_t tx_timeout = furi_ms_to_ticks(airtime_ms);

    while((uint32_t)(furi_get_tick() - start) < tx_timeout) {
        if(!furi_hal_gpio_read(m_gdo0)) return true;
        furi_delay_ms(1);
    }
    return false;
}

// ---------------------------------------------------------------------------
//  Sendeabstand -- ueber furi-Ticks, NICHT ueber sysclock (siehe Header)
// ---------------------------------------------------------------------------

void FlipperRadio::setSendTimeout(uint16_t ms) {
    m_next_send_tick = furi_get_tick() + furi_ms_to_ticks(ms);
}

void FlipperRadio::waitSendSlot() {
    // Vorzeichenbehaftete Differenz -- furi_get_tick() laeuft nach ~49,7
    // Tagen ueber (CLAUDE.md §4.2).
    while((int32_t)(m_next_send_tick - furi_get_tick()) > 0) {
        furi_delay_ms(1);
    }
}

void FlipperRadio::waitTimeout(uint16_t ms) {
    setSendTimeout(ms);
    waitSendSlot();
}

// ---------------------------------------------------------------------------
//  Zustand
// ---------------------------------------------------------------------------

void FlipperRadio::setIdle() {
    if(!m_open || m_idle) return;
    furi_hal_subghz_idle();
    m_idle = true;
}

void FlipperRadio::wakeup(bool flush) {
    if(!m_open || !m_idle) return;
    if(flush) furi_hal_subghz_flush_rx();
    furi_hal_subghz_rx();
    m_idle = false;
}

void FlipperRadio::tuneFreq(uint8_t freq2, uint8_t freq1, uint8_t freq0) {
    if(!m_open) return;

    // CC1101: f = (FREQ / 2^16) * f_xosc
    uint32_t reg = ((uint32_t)freq2 << 16) | ((uint32_t)freq1 << 8) | freq0;
    uint32_t hz = (uint32_t)(((uint64_t)reg * CC1101_XOSC_HZ) >> 16);

    if(!furi_hal_subghz_is_frequency_valid(hz)) {
        FURI_LOG_W(TAG, "tuneFreq %lu Hz rejected - keeping current", (unsigned long)hz);
        return;
    }
    uint32_t real = furi_hal_subghz_set_frequency_and_path(hz);
    FURI_LOG_I(TAG, "retuned to %lu Hz", (unsigned long)real);
    furi_hal_subghz_rx();
}

bool FlipperRadio::detectBurst() {
    // Carrier-Sense ueber RSSI. Nur fuer Wake-on-Radio-Geraete relevant;
    // eine Fernbedienung braucht es nicht.
    //
    // Der RSSI-Wert ist nur im Empfangsbetrieb gueltig -- in IDLE liefert
    // das Register einen Altwert.
    if(!m_open || m_idle) return false;
    return furi_hal_subghz_get_rssi() > -90.0f;
}

void FlipperRadio::flushrx() {
    if(!m_open) return;
    // cc1101_flush_rx setzt IDLE voraus -- danach MUSS wieder RX aktiviert
    // werden. Fehlte das, war der Empfaenger nach dem ersten CRC-Fehler
    // dauerhaft taub: m_idle blieb false, read() lief also weiter durch,
    // aber der Chip stand in IDLE und rx_pipe_not_empty() konnte nie mehr
    // true werden. Erst der naechste Sendevorgang haette ihn geweckt.
    furi_hal_subghz_idle();
    furi_hal_subghz_flush_rx();
    furi_hal_subghz_rx();
    m_idle = false;
    // RX ist frisch gestrobt -- der vorsorgliche Refresh darf sich Zeit
    // lassen (siehe keepRxAlive).
    m_next_rearm_tick = furi_get_tick() + furi_ms_to_ticks(RX_REARM_INTERVAL_MS);
}

uint8_t FlipperRadio::getGDO0() {
    if(m_gdo0 == NULL) return 0;
    return furi_hal_gpio_read(m_gdo0) ? 1 : 0;
}

uint8_t FlipperRadio::reset() {
    if(!m_open) return 0;
    // load_custom_preset() resettet intern und legt GDO0 dabei auf Analog --
    // deshalb MUSS enableGdo0() danach stehen, genau wie in init(). Fehlte
    // das hier, war GDO0 nach dem zweiten radio.init() aus AskSin<>::init()
    // dauerhaft tot.
    furi_hal_subghz_load_custom_preset(asksin_preset);
    furi_hal_subghz_set_frequency_and_path(ASKSIN_FREQUENCY_HZ);
    enableGdo0();
    furi_hal_subghz_flush_rx();
    furi_hal_subghz_rx();
    m_idle = false;
    return 0;
}

} // namespace as
