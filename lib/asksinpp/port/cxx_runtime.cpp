//- ---------------------------------------------------------------------------
//  Minimale C++-Runtime fuer die FAP-Umgebung
//
//  Die Firmware exportiert operator new/delete nicht. Gebraucht werden sie
//  trotzdem: AskSin++ hat virtuelle Destruktoren (Alarm, Channel,
//  RemoteChannel, ...), und GCC legt fuer jede solche Klasse einen
//  "deleting destructor" (D0) in die vtable, der operator delete aufruft.
//  Dieser Slot wird referenziert, auch wenn wir nie 'delete' schreiben --
//  ohne Definition bleibt beim Linken '_ZdlPvj' offen und der Loader
//  verweigert die App.
//
//  Die Weiterleitung geht auf die Furi-Heapfunktionen. Der D0-Pfad wird bei
//  uns nie durchlaufen (alle AskSin++-Objekte liegen in statischen Arenas
//  und werden per placement new konstruiert, CLAUDE.md §3.1) -- die
//  Definitionen sind also reine Linkerbefriedigung, nicht der uebliche Weg.
//
//  Heap im Funkpfad bleibt verboten (CLAUDE.md §3.2). Diese Datei aendert
//  daran nichts.
//- ---------------------------------------------------------------------------
#include <furi.h>

#include <stdlib.h>
#include <stddef.h>

void* operator new(size_t size) {
    return malloc(size);
}

void* operator new[](size_t size) {
    return malloc(size);
}

void operator delete(void* ptr) noexcept {
    free(ptr);
}

void operator delete[](void* ptr) noexcept {
    free(ptr);
}

// Sized deallocation (C++14). Genau dieses Symbol (_ZdlPvj) fehlte.
void operator delete(void* ptr, size_t size) noexcept {
    UNUSED(size);
    free(ptr);
}

void operator delete[](void* ptr, size_t size) noexcept {
    UNUSED(size);
    free(ptr);
}

extern "C" {

// Wird aufgerufen, wenn eine rein virtuelle Funktion ueber ein noch nicht
// fertig konstruiertes Objekt erreicht wird. Das ist ein Programmierfehler
// -- sauber crashen statt weiterlaufen.
void __cxa_pure_virtual(void) {
    furi_crash("pure virtual call");
}

} // extern "C"
