// Speicherkarte: RA-Adressen des SNES <-> Adressen im FXPak, und die
// Planung gebuendelter Lesezugriffe.
//
// RA-Adressraum SNES (rcheevos, MIT):
//   0x000000-0x01FFFF  System-RAM (WRAM 7E:0000-7F:FFFF)
//   0x020000-0x09FFFF  Cartridge-RAM (SRAM/BW-RAM)
//   0x0A0000-0x0A07FF  SA-1 I-RAM  -- nur ueber die Werte-Kopie (nmi_kopie)
// FXPak: WRAM-Spiegel ab 0xF50000, Spielstand-RAM ab 0xE00000.
#pragma once

#include "fxpak_protokoll.h"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace snes {

constexpr uint32_t kRaWramEnde = 0x020000;
constexpr uint32_t kRaSramEnde = 0x0A0000;
constexpr uint32_t kRaIramEnde = 0x0A0800;

// Liegt die Adresse im RA-Adressraum des SNES?
bool RaGueltig(uint32_t ra);
// Ist diese RA-Adresse am FXPak ueber USB lesbar (WRAM-Kopie, SRAM)?
bool RaLesbar(uint32_t ra);

// RA-Adresse -> FXPak-Adresse (Raum Snes); leer, wenn nicht lesbar.
std::optional<uint32_t> FxPakAdresse(uint32_t ra);

// Ein Lesevorgang mit bis zu acht Bereichen.
struct Lesung {
    std::vector<fxpak::Bereich> bereiche;
    uint32_t bytes = 0;
};

// Fasst die benoetigten RA-Adressen zu moeglichst wenigen Lesungen zusammen.
// Benachbarte Adressen mit hoechstens `luecke` Byte Abstand teilen sich einen
// Bereich (ein paar Byte mehr lesen ist billiger als eine weitere Abfrage).
class Leseplan {
public:
    Leseplan() = default;
    Leseplan(const std::vector<uint32_t>& raAdressen, uint32_t luecke = 24);

    const std::vector<Lesung>& Lesungen() const { return lesungen_; }
    size_t AnzahlAdressen() const { return adressen_.size(); }

    // Ordnet die Antworten (je Lesung lueckenlos) den RA-Adressen zu.
    // `antworten` hat dieselbe Reihenfolge wie Lesungen().
    std::map<uint32_t, uint8_t> Verteile(const std::vector<std::vector<uint8_t>>& antworten) const;

private:
    struct Stueck { uint32_t ra; uint32_t laenge; };  // zusammenhaengend im RA-Raum
    std::vector<uint32_t> adressen_;
    std::vector<Lesung> lesungen_;
    std::vector<std::vector<Stueck>> stuecke_;        // je Lesung, je Bereich
};

}  // namespace snes
