// Intro -- Choreografie und Bildmasse (FMETA) wie bei RAW-NES; Text und
// Farben in der SNES-Fassung: aus "RAW" wird "RAWSNES", dann "RAW-SNES", dazu
// die vier Tastenfarben. Gezeichnet mit der Zeichenliste von Dear ImGui;
// radiale Verlaeufe und das Weichzeichnen des Textscheins sind angenaehert.
#pragma once

#include "imgui.h"

namespace intro {

// Einmal nach dem Anlegen des OpenGL-Kontexts: Katzenbilder hochladen.
// fett/kursiv: Courier New in fett und kursiv (nullptr = Standardschrift).
void Laden(ImFont* fett, ImFont* kursiv);

// Startet die Sequenz (600 ms Vorlauf, dann 6200 ms).
void Starten();

// Zeichnet ein Bild ueber das ganze Fenster. Liefert false, sobald die
// Sequenz durch ist oder uebersprungen wurde (Klick, Taste, SKIP).
bool Zeichnen(float skalierung);

void Freigeben();

}  // namespace intro
