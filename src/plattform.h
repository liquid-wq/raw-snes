// Windows-Anbindung fuer die Oberflaeche: Programmordner, Datei- und
// Ordnerauswahl, Webseiten oeffnen.
//
// Alle Pfade sind UTF-8, so wie std::filesystem sie mit u8path liest.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace plattform {

std::string Utf8(const std::wstring& w);
std::wstring Breit(const std::string& s);

// Ordner der laufenden Exe.
std::string ExeOrdner();

// Ordnerauswahl. Leer bei Abbruch.
std::string OrdnerWaehlen(void* fenster, const std::string& titel, const std::string& start = std::string());

// Dateiauswahl. filter: Paare aus
// Beschreibung und Muster ("*.sfc;*.smc"). Leer bei Abbruch.
std::string DateiWaehlen(void* fenster, const std::string& titel,
                         const std::vector<std::pair<std::string, std::string>>& filter);

// Webseite im Standardbrowser oeffnen.
void UrlOeffnen(const std::string& url);

}  // namespace plattform
