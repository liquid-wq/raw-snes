// Uebersetzung Deutsch -> Englisch, im Code hinterlegt.
//
// Schluessel ist immer der deutsche Text; fehlt ein Eintrag, bleibt der
// deutsche Text stehen.
//
// Weil Dear ImGui jedes Bild neu zeichnet, wird T() bei jedem Bild frisch
// ausgewertet. Ein Sprachwechsel wirkt damit sofort auf alles Sichtbare; die
// zur Laufzeit zusammengesetzten Texte folgen ebenfalls. Bereits
// geschriebene Protokollzeilen bleiben, wie sie sind.
#pragma once

#include <initializer_list>
#include <string>

namespace i18n {

void SpracheSetzen(const std::string& kennung);  // "de" oder "en"
const std::string& Sprache();
bool Englisch();

// Deutscher Quelltext -> Text in der aktuellen Sprache.
const char* T(const char* deutsch);

// Ersetzt %1, %2, ... der Reihe nach.
std::string Fmt(const std::string& muster, std::initializer_list<std::string> werte);

// Zahl als Text, fuer Fmt().
std::string Zahl(long long n);
// Hex mit fester Breite, Kleinbuchstaben, fuehrende Nullen.
std::string Hex(unsigned long long n, int breite);

}  // namespace i18n

using i18n::T;
using i18n::Fmt;
