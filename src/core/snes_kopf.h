// Interner SNES-ROM-Header: Lage erkennen und Felder lesen.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace snes {

enum class Abbildung { Unbekannt, LoRom, HiRom, ExHiRom };

struct Kopf {
    bool gefunden = false;
    Abbildung abbildung = Abbildung::Unbekannt;
    uint32_t lage = 0;        // Dateiversatz des Headers (xxFFC0 bzw. 7FC0)
    std::string titel;        // 21 Zeichen, Leerzeichen rechts entfernt
    uint8_t modus = 0;        // Map-Mode-Byte
    uint8_t chip = 0;
    uint32_t romBytes = 0;    // laut Header
    uint32_t sramBytes = 0;   // laut Header
    uint8_t region = 0;
    uint8_t version = 0;
    uint16_t pruefsumme = 0;
    uint16_t komplement = 0;
};

const char* Name(Abbildung a);

// Moegliche Lagen des Headers im ROM ohne Kopierer-Vorspann.
constexpr uint32_t kLagen[] = {0x007FC0, 0x00FFC0, 0x40FFC0};
constexpr uint32_t kKopfLaenge = 0x40;

// Bewertet einen 64-Byte-Ausschnitt ab `lage`. Hoeher = plausibler; < 0 =
// sicher kein Header.
int Bewerte(const uint8_t* p, uint32_t lage);

Kopf Deute(const uint8_t* p, uint32_t lage);

// Waehlt unter den Kandidaten (je kKopfLaenge Byte, Reihenfolge wie kLagen,
// leere Eintraege = nicht verfuegbar) den plausibelsten.
Kopf Waehle(const std::vector<std::vector<uint8_t>>& kandidaten);

}  // namespace snes
