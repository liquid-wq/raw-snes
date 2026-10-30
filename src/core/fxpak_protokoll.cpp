#include "fxpak_protokoll.h"

#include <cstring>

namespace fxpak {

namespace {

void SetzeBE32(uint8_t* p, uint32_t w) {
    p[0] = (uint8_t)(w >> 24);
    p[1] = (uint8_t)(w >> 16);
    p[2] = (uint8_t)(w >> 8);
    p[3] = (uint8_t)w;
}

uint32_t LiesBE32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// Text aus einem festen Feld, endet am ersten Nullbyte oder am Feldende.
std::string Feldtext(const uint8_t* p, size_t max) {
    size_t n = 0;
    while (n < max && p[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}

Paket Kopf(int groesse, Befehl befehl, Raum raum, uint8_t schalter) {
    Paket p((size_t)groesse, 0);
    p[0] = 'U'; p[1] = 'S'; p[2] = 'B'; p[3] = 'A';
    p[4] = (uint8_t)befehl;
    p[5] = (uint8_t)raum;
    p[6] = schalter;
    return p;
}

}  // namespace

Paket BaueInfo() {
    return Kopf(kBlock, Befehl::Info, Raum::Snes, 0);
}

Paket BaueLesen(Raum raum, uint32_t adresse, uint32_t laenge) {
    Paket p = Kopf(kBlock, Befehl::Lesen, raum, 0);
    SetzeBE32(&p[252], laenge);
    SetzeBE32(&p[256], adresse);
    return p;
}

Paket BaueSchreiben(Raum raum, uint32_t adresse, uint32_t laenge) {
    Paket p = Kopf(kBlock, Befehl::Schreiben, raum, kKeineAntwort | kKleineBloecke);
    SetzeBE32(&p[252], laenge);
    SetzeBE32(&p[256], adresse);
    return p;
}

Paket BaueLesenMehrfach(Raum raum, const std::vector<Bereich>& bereiche) {
    Paket p = Kopf(kKleinBlock, Befehl::LesenMehrfach, raum, kKleineBloecke);
    const size_t n = bereiche.size() < (size_t)kMaxBereiche ? bereiche.size() : (size_t)kMaxBereiche;
    for (size_t i = 0; i < n; ++i) {
        uint8_t* e = &p[32 + i * 4];
        e[0] = bereiche[i].laenge;
        e[1] = (uint8_t)(bereiche[i].adresse >> 16);
        e[2] = (uint8_t)(bereiche[i].adresse >> 8);
        e[3] = (uint8_t)bereiche[i].adresse;
    }
    return p;
}

namespace {
Paket MitText(Befehl befehl, Raum raum, const std::string& text, uint8_t schalter = 0) {
    Paket p = Kopf(kBlock, befehl, raum, schalter);
    const size_t n = text.size() < 255 ? text.size() : 255;  // Rest bleibt 0 (Abschluss)
    std::memcpy(&p[256], text.data(), n);
    return p;
}

std::string Klein(std::string s) {
    for (char& c : s) c = (char)((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    return s;
}

bool EndetMit(const std::string& s, const char* ende) {
    const size_t n = std::strlen(ende);
    return s.size() >= n && s.compare(s.size() - n, n, ende) == 0;
}
}  // namespace

Paket BaueDateiLesen(const std::string& pfad) {
    return MitText(Befehl::Lesen, Raum::Datei, pfad);
}

Paket BaueDateiSchreiben(const std::string& pfad, uint32_t laenge) {
    Paket p = MitText(Befehl::Schreiben, Raum::Datei, pfad);
    SetzeBE32(&p[252], laenge);
    return p;
}

Paket BaueUmbenennen(const std::string& pfad, const std::string& neuerName) {
    Paket p = MitText(Befehl::Umbenennen, Raum::Datei, pfad);
    // Ab Byte 8, vor dem Laengenfeld bei 252 abgeschlossen.
    const size_t n = neuerName.size() < 243 ? neuerName.size() : 243;
    std::memcpy(&p[8], neuerName.data(), n);
    return p;
}

Paket BaueLoeschen(const std::string& pfad) {
    return MitText(Befehl::Loeschen, Raum::Datei, pfad);
}

Paket BaueStarten(const std::string& pfad, uint8_t schalter) {
    return MitText(Befehl::Starten, Raum::Datei, pfad, schalter);
}

Paket BaueZuruecksetzen() {
    return MitText(Befehl::Zuruecksetzen, Raum::Datei, "");
}

Paket BaueKonfigLesen(const std::string& schluessel) {
    return MitText(Befehl::Lesen, Raum::Konfiguration, schluessel);
}

bool IstMenue(const std::string& romPfad) {
    const std::string p = Klein(romPfad);
    return p.empty() || EndetMit(p, "menu.bin") || EndetMit(p, "m3nu.bin");
}

bool IstSnesRom(const std::string& romPfad) {
    const std::string p = Klein(romPfad);
    for (const char* e : {".sfc", ".smc", ".swc", ".fig", ".bs"})
        if (EndetMit(p, e)) return true;
    return false;
}

Antwort DeuteAntwort(const uint8_t* block) {
    Antwort a;
    a.gueltig = std::memcmp(block, "USBA", 4) == 0 && block[4] == (uint8_t)Befehl::Antwort;
    a.fehler = block[5];
    a.laenge = LiesBE32(&block[252]);
    return a;
}

uint32_t GesendeteBytes(uint32_t nutzLaenge, int blockGroesse) {
    const uint32_t b = (uint32_t)blockGroesse;
    return (nutzLaenge + b - 1) / b * b;
}

GeraeteInfo DeuteInfo(const uint8_t* block) {
    GeraeteInfo g;
    g.funktionen = (uint32_t)block[6] | ((uint32_t)block[7] << 8) |
                   ((uint32_t)block[8] << 16) | ((uint32_t)block[9] << 24);
    g.einstellungen = (uint16_t)(block[10] | (block[11] << 8));
    g.rom = Feldtext(&block[16], 252 - 16);
    g.firmwareNr = LiesBE32(&block[256]);
    g.version = Feldtext(&block[260], 64);
    g.geraet = Feldtext(&block[324], 64);
    return g;
}

}  // namespace fxpak
