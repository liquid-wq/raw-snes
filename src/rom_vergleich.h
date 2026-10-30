// Vergleicht die im Modul geladene ROM per USB mit einer ROM-Datei.
// Nur lesend; dient der Diagnose, um Ladefehler von der SD-Karte zu
// erkennen.
#pragma once

#include "fxpak_port.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace romvergleich {

struct Ergebnis {
    uint32_t verglichen = 0;          // Byte
    long abweichend = 0;              // Byte, die beim ersten Lesen abwichen
    long bleibend = 0;                // davon beim zweiten Lesen gleich falsch
    long kurz = 0;                    // Happen, die das Modul nicht vollstaendig lieferte
    std::vector<std::string> beispiele;   // die ersten Abweichungen als Text
};

// `datei` ist der Dateiinhalt; ein Kopierer-Vorspann (512 Byte) wird
// uebersprungen. `fortschritt` bekommt die bisher verglichene Byte-Zahl.
Ergebnis Vergleiche(FxPakPort& fx, Bytes datei, const std::function<void(uint32_t, long)>& fortschritt = {});

}  // namespace romvergleich
