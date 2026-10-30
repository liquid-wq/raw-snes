#include "snes_kopf.h"

namespace snes {

namespace {

uint16_t LiesLE16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// Abbildung, die das Map-Mode-Byte fuer eine bestimmte Lage erwarten laesst.
Abbildung Erwartet(uint32_t lage) {
    switch (lage) {
    case 0x007FC0: return Abbildung::LoRom;
    case 0x00FFC0: return Abbildung::HiRom;
    case 0x40FFC0: return Abbildung::ExHiRom;
    default: return Abbildung::Unbekannt;
    }
}

Abbildung AusModus(uint8_t modus) {
    switch (modus & 0x0F) {
    case 0x0: case 0x2: case 0x3: return Abbildung::LoRom;  // 3 = SA-1, LoROM-artig
    case 0x1: case 0xA: return Abbildung::HiRom;            // A = SPC7110
    case 0x5: return Abbildung::ExHiRom;
    default: return Abbildung::Unbekannt;
    }
}

}  // namespace

const char* Name(Abbildung a) {
    switch (a) {
    case Abbildung::LoRom: return "LoROM";
    case Abbildung::HiRom: return "HiROM";
    case Abbildung::ExHiRom: return "ExHiROM";
    default: return "?";
    }
}

int Bewerte(const uint8_t* p, uint32_t lage) {
    int punkte = 0;
    const uint8_t modus = p[0x15];
    if ((modus & 0xE0) != 0x20) return -1;  // Map-Mode hat immer die Form 001x xxxx
    if (AusModus(modus) == Erwartet(lage)) punkte += 4;

    if ((uint16_t)(LiesLE16(&p[0x1C]) ^ LiesLE16(&p[0x1E])) == 0xFFFF) punkte += 8;

    int lesbar = 0;
    for (int i = 0; i < 21; ++i)
        if (p[i] >= 0x20 && p[i] < 0x7F) ++lesbar;
    punkte += lesbar / 4;

    if (p[0x17] >= 0x07 && p[0x17] <= 0x0D) punkte += 2;  // 128 KiB .. 8 MiB
    if (p[0x18] <= 0x08) punkte += 1;
    return punkte;
}

Kopf Deute(const uint8_t* p, uint32_t lage) {
    Kopf k;
    k.gefunden = true;
    k.lage = lage;
    k.modus = p[0x15];
    k.abbildung = AusModus(k.modus);
    if (k.abbildung == Abbildung::Unbekannt) k.abbildung = Erwartet(lage);

    std::string t;
    for (int i = 0; i < 21; ++i) t.push_back((p[i] >= 0x20 && p[i] < 0x7F) ? (char)p[i] : '.');
    while (!t.empty() && t.back() == ' ') t.pop_back();
    k.titel = t;

    k.chip = p[0x16];
    k.romBytes = p[0x17] ? (1024u << (p[0x17] & 0x0F)) : 0;
    k.sramBytes = p[0x18] ? (1024u << (p[0x18] & 0x0F)) : 0;
    k.region = p[0x19];
    k.version = p[0x1B];
    k.komplement = LiesLE16(&p[0x1C]);
    k.pruefsumme = LiesLE16(&p[0x1E]);
    return k;
}

Kopf Waehle(const std::vector<std::vector<uint8_t>>& kandidaten) {
    int beste = -1;
    size_t index = 0;
    for (size_t i = 0; i < kandidaten.size() && i < sizeof(kLagen) / sizeof(kLagen[0]); ++i) {
        if (kandidaten[i].size() < kKopfLaenge) continue;
        const int b = Bewerte(kandidaten[i].data(), kLagen[i]);
        if (b > beste) { beste = b; index = i; }
    }
    if (beste < 0) return Kopf{};
    return Deute(kandidaten[index].data(), kLagen[index]);
}

}  // namespace snes
