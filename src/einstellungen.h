// rawsnes_config.json neben der Exe.
#pragma once

#include <map>
#include <string>

struct Einstellungen {
    std::string raUser;
    std::string raToken;       // Anmeldeschluessel von RetroAchievements, nie das Passwort
    bool rememberLogin = true;
    std::string language = "de";
    bool autostart = true;     // Ueberwachung startet, sobald ein Spiel laeuft
    bool hardcore = false;
    // ROM-Pfad|Header-Pruefsumme -> RA-Hash; erspart das erneute Lesen der Datei.
    std::map<std::string, std::string> hashes;

    // Fehlende oder kaputte Datei: Standardwerte.
    void Laden(const std::string& pfad);
    void Speichern(const std::string& pfad) const;
};
