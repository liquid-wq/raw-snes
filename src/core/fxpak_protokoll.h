// USB-Protokoll des FXPak: Befehle bauen, Antworten auswerten.
//
// Ohne jeden Betriebssystem-Bezug, damit dieselbe Datei auch auf anderen Geraeten
// laeuft.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace fxpak {

constexpr uint16_t kVid = 0x1209;
constexpr uint16_t kPid = 0x5A22;

constexpr int kBlock = 512;       // Befehle, Antworten, normale Datenbloecke
constexpr int kKleinBlock = 64;   // Mehrbereichs-Befehle, kleine Datenbloecke
constexpr int kMaxBereiche = 8;   // je Mehrbereichs-Befehl
constexpr int kMaxBereichLaenge = 255;

enum class Befehl : uint8_t {
    Lesen = 0,
    Schreiben = 1,
    LesenMehrfach = 2,
    SchreibenMehrfach = 3,
    Loeschen = 6,
    Umbenennen = 7,
    Zuruecksetzen = 8,   // Konsole zuruecksetzen (laufende ROM bleibt geladen)
    Starten = 9,         // ROM von der Karte laden und die Konsole damit neu starten
    Info = 11,
    Antwort = 15,
};

enum class Raum : uint8_t {
    Datei = 0,
    Snes = 1,
    Befehlspuffer = 3,   // $2A00-$2BFF im FPGA, auch fuer die Konsole sichtbar
    Konfiguration = 5,   // Werte aus der config.yml der SD-Karte, als Text
};

enum Schalter : uint8_t {
    kNichtLoslassen = 1,    // Starten: ROM laden, Konsole im Reset halten
    kNurLoslassen = 2,      // Starten: nichts laden, Konsole aus dem Reset lassen
    kKeineAntwort = 64,
    kKleineBloecke = 128,
};

// Adressen im Raum Snes.
constexpr uint32_t kRom = 0x000000;
constexpr uint32_t kSram = 0xE00000;
constexpr uint32_t kWram = 0xF50000;
constexpr uint32_t kWramGroesse = 0x20000;

struct Bereich {
    uint32_t adresse = 0;  // 24 Bit
    uint8_t laenge = 0;    // 1..255
};

// Ein Befehl, fertig zum Senden (512 bzw. 64 Byte).
using Paket = std::vector<uint8_t>;

Paket BaueInfo();
Paket BaueLesen(Raum raum, uint32_t adresse, uint32_t laenge);
// Hoechstens kMaxBereiche Eintraege; die Daten kommen in 64-Byte-Bloecken.
Paket BaueLesenMehrfach(Raum raum, const std::vector<Bereich>& bereiche);
// Schreiben ohne Antwort: die Nutzdaten folgen direkt in 64-Byte-Bloecken,
// der letzte mit Nullen aufgefuellt; uebernommen werden genau `laenge` Byte.
Paket BaueSchreiben(Raum raum, uint32_t adresse, uint32_t laenge);
// Ganze Datei von der SD-Karte; die Antwort nennt ihre Groesse.
Paket BaueDateiLesen(const std::string& pfad);
// Datei auf die SD-Karte schreiben (ersetzt sie): Antwort, danach die Daten in
// 512-Byte-Bloecken, der letzte aufgefuellt.
Paket BaueDateiSchreiben(const std::string& pfad, uint32_t laenge);
// Datei umbenennen; `neuerName` ohne Ordner, sie bleibt im selben Ordner.
Paket BaueUmbenennen(const std::string& pfad, const std::string& neuerName);
Paket BaueLoeschen(const std::string& pfad);
// ROM von der SD-Karte starten (setzt die Konsole zurueck, laedt ROM und Kern).
// Mit kNichtLoslassen bleibt die Konsole danach im Reset, bis ein Starten mit
// kNurLoslassen sie freigibt -- dazwischen laesst sich die ROM aendern.
Paket BaueStarten(const std::string& pfad, uint8_t schalter = 0);
Paket BaueZuruecksetzen();
// Ein Wert der Modul-Konfiguration, z. B. "EnableCheats".
Paket BaueKonfigLesen(const std::string& schluessel);

// Laeuft gerade das Menue des Moduls statt eines Spiels?
bool IstMenue(const std::string& romPfad);
// Endung einer SNES-ROM (.sfc .smc .swc .fig .bs)?
bool IstSnesRom(const std::string& romPfad);

struct Antwort {
    bool gueltig = false;   // Kennung und Antwort-Befehl stimmen
    uint8_t fehler = 0;     // 0 = in Ordnung
    uint32_t laenge = 0;    // Nutzdaten, die noch folgen
};
// `block` muss kBlock Byte lang sein.
Antwort DeuteAntwort(const uint8_t* block);

// Byte-Zahl, die nach der Antwort tatsaechlich ueber die Leitung kommt.
uint32_t GesendeteBytes(uint32_t nutzLaenge, int blockGroesse);

struct GeraeteInfo {
    uint32_t firmwareNr = 0;
    std::string version;
    std::string geraet;
    std::string rom;          // laufende ROM laut Modul
    uint32_t funktionen = 0;
    uint16_t einstellungen = 0;   // siehe kEinst...
};

// Bits in GeraeteInfo::einstellungen.
constexpr uint16_t kEinstHook = 0x0001;         // In-Game-Hook an
constexpr uint16_t kEinstSavestates = 0x0002;
GeraeteInfo DeuteInfo(const uint8_t* block);

}  // namespace fxpak
