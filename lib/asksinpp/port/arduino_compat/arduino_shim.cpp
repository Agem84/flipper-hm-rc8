//- ---------------------------------------------------------------------------
//  Implementierung des Serial-Ersatzes (zeilengepuffertes Logging)
//- ---------------------------------------------------------------------------
#include "Arduino.h"
#include "../flipper_config.h"

#define TAG ASKSIN_LOG_TAG

FlipperSerial Serial;

void FlipperSerial::flush() {
    if(m_len == 0) return;
    m_buf[m_len] = '\0';
    FURI_LOG_D(TAG, "%s", m_buf);
    m_len = 0;
}

void FlipperSerial::put(char c) {
    // CR schlucken -- Debug.h sendet "\r\n", wir wollen eine Zeile pro Log.
    if(c == '\r') return;

    if(c == '\n') {
        flush();
        return;
    }

    // Ein Byte Reserve fuer die Nullterminierung in flush().
    if(m_len >= sizeof(m_buf) - 1) {
        flush();
    }
    m_buf[m_len++] = c;
}

void FlipperSerial::print(const char* s) {
    if(s == NULL) return;
    while(*s != '\0') {
        put(*s++);
    }
}

void FlipperSerial::print(char c) {
    put(c);
}

void FlipperSerial::println() {
    put('\n');
}

void FlipperSerial::printUnsigned(unsigned long v, int base) {
    if(base < 2) base = 10;

    // 32 Bit binaer ist der schlimmste Fall.
    char tmp[33];
    int i = 0;

    if(v == 0) {
        put('0');
        return;
    }
    while(v != 0 && i < (int)sizeof(tmp)) {
        unsigned long d = v % (unsigned long)base;
        tmp[i++] = (char)(d < 10 ? ('0' + d) : ('A' + (d - 10)));
        v /= (unsigned long)base;
    }
    while(i-- > 0) {
        put(tmp[i]);
    }
}

void FlipperSerial::printSigned(long v, int base) {
    if(v < 0 && base == 10) {
        put('-');
        // Negation ueber unsigned, damit LONG_MIN nicht ueberlaeuft.
        printUnsigned((unsigned long)(-(v + 1)) + 1UL, base);
        return;
    }
    printUnsigned((unsigned long)v, base);
}
