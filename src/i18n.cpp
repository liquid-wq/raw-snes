#include "i18n.h"

#include <cstdio>
#include <unordered_map>

namespace i18n {
namespace {

std::string g_sprache = "de";

const std::unordered_map<std::string, std::string>& TabelleEn() {
    static const std::unordered_map<std::string, std::string> t = {
        // --- Oberflaeche -----------------------------------------------------
        {"Optionen", "Options"},
        {"RetroAchievements", "RetroAchievements"},
        {"RA-Benutzername", "RA username"},
        {"RA-Passwort", "RA password"},
        {"Anmelden", "Log in"},
        {"Abmelden", "Log out"},
        {"Abgemeldet.", "Logged out."},
        {"Eingeloggt bleiben", "Stay logged in"},
        {"Benutzername und Anmeldeschluessel speichern (das Passwort wird nicht gespeichert)",
         "Save username and login token (your password is not stored)"},
        {"nicht angemeldet", "not logged in"},
        {"Bitte Benutzername und Passwort eingeben.", "Please enter username and password."},
        {"Login laeuft...", "Logging in..."},
        {"Login erfolgreich.", "Login successful."},
        {"Login fehlgeschlagen (falsche Zugangsdaten?).", "Login failed (wrong credentials?)."},
        {"Anmeldung abgelaufen -- bitte das Passwort eingeben.", "Login expired -- please enter your password."},
        {": Diese Fassung des Spiels unterstuetzt RetroAchievements nicht. Welche Fassungen passen, steht auf der Spielseite bei RetroAchievements (Supported Game Hashes).", ": RetroAchievements does not support this version of the game. The supported versions are listed on the game page at RetroAchievements (Supported Game Hashes)."},
        {"Nur teilweise unterstuetzt", "Only partly supported"},
        {"Nur teilweise unterstuetzt: %1 von %2 offenen Achievements werden ueberwacht.", "Only partly supported: %1 of %2 open achievements are monitored."},
        {"%1 von %2 offenen Achievements werden ueberwacht.", "%1 of %2 open achievements are monitored."},
        {"Bei diesem Spiel mit Zusatzchip reicht der Platz nicht fuer alle Achievements. Das ist eine Grenze der Originalfirmware des Moduls. Die uebrigen werden erst ueberwacht, wenn durch Freischaltungen Platz frei wird. Was du vorher bei ihnen erreichst, zaehlt nicht und wird auch nicht nachtraeglich freigeschaltet. In der Liste sind sie als \"nicht ueberwacht\" markiert.", "This enhancement chip game does not have room for all achievements. This is a limit of the cartridge's original firmware. The others are only monitored once unlocks free up room. Anything you achieve for them before that does not count and is not unlocked later. They are marked \"not monitored\" in the list."},
        {"Achievements mit \"nicht ueberwacht\" zaehlen gerade nicht.", "Achievements marked \"not monitored\" do not count right now."},
        {"nicht ueberwacht", "not monitored"},
        {"Fuer dieses Spiel ist bei RetroAchievements kein Achievement-Set vorhanden.", "There is no achievement set for this game at RetroAchievements."},
        {"Diese Fassung des Spiels unterstuetzt RetroAchievements nicht.", "RetroAchievements does not support this version of the game."},
        {"kein Achievement-Set vorhanden", "no achievement set"},
        {"Fassung nicht unterstuetzt", "version not supported"},
        {"%1 Achievement(s) passen gerade nicht in den Befehlspuffer und werden nicht ueberwacht. Sie kommen erst dazu, wenn durch Freischaltungen Platz frei wird; was vorher bei ihnen erreicht wird, zaehlt nicht.", "%1 achievement(s) do not fit into the command buffer right now and are not monitored. They are added once unlocks free up room; anything achieved for them before that does not count."},
        {"Volle Unterstuetzung gibt es in der Master-Version. Sie ist fertig, wurde aber aus Lizenzgruenden noch nicht veroeffentlicht.", "Full support is in the master version. It is finished, but has not been released yet for licensing reasons."},
        {"%1 Achievement(s) kann RAW-SNES hier nicht auswerten (Adressen nicht lesbar oder unbekannte Schreibweise).", "RAW-SNES cannot evaluate %1 achievement(s) here (addresses not readable or unknown syntax)."},
        {"Bitte zuerst anmelden.", "Please log in first."},

        {"SD2SNES / FXPak Pro", "SD2SNES / FXPak Pro"},
        {"verbunden", "connected"},
        {"SD2SNES/FXPak nicht verbunden", "SD2SNES/FXPak not connected"},
        {"suche SD2SNES/FXPak", "looking for SD2SNES/FXPak"},
        {"frage Modul...", "asking cartridge..."},
        {"Menue des SD2SNES/FXPak -- ein Spiel starten.", "SD2SNES/FXPak menu -- start a game."},
        {"erkenne Spiel...", "identifying game..."},
        {"Monitor starten", "Start monitor"},
        {"Monitor stoppen", "Stop monitor"},
        {"Erst bei RetroAchievements anmelden.", "Log in to RetroAchievements first."},
        {"Erst ein Spiel am SD2SNES/FXPak starten.", "Start a game on the SD2SNES/FXPak first."},
        {"Automatisch starten", "Start automatically"},
        {"Startet die Ueberwachung von selbst, sobald am SNES ein Spiel laeuft, und wechselt mit dem Spiel.",
         "Starts monitoring by itself as soon as a game is running on the SNES, and follows game changes."},
        {"Hardcore-Modus", "Hardcore mode"},
        {"(erfordert: Savestates und Cheats im SD2SNES/FXPak-Menue aus)",
         "(requires: savestates and cheats off in the SD2SNES/FXPak menu)"},
        {"(von RetroAchievements noch nicht anerkannt)", "(not yet accepted by RetroAchievements)"},
        {"RetroAchievements nimmt Hardcore von diesem Geraet noch nicht an. Freischaltungen werden als Softcore gebucht.",
         "RetroAchievements does not accept Hardcore from this device yet. Unlocks are recorded as Softcore."},

        {"lade Achievements...", "loading achievements..."},
        {"Noch kein Spiel geladen. Sobald am SNES ein Spiel laeuft,\nerscheinen hier seine Achievements.",
         "No game loaded yet. As soon as a game is running on the SNES,\nits achievements appear here."},
        {"%1 Pkt", "%1 pts"},
        {"nicht lesbar", "not readable"},
        {"Live-Werte", "Live values"},
        {"RAM: -", "RAM: -"},
        {"Log kopieren", "Copy log"},

        {"Leaderboards", "Leaderboards"},
        {"Kein SD2SNES/FXPak", "No SD2SNES/FXPak"},
        {"USB-Kabel einstecken, Konsole an", "plug in USB, switch console on"},
        {"Menue", "Menu"},
        {"Spiel am SNES starten", "start a game on the SNES"},
        {"noch nicht erkannt", "not identified yet"},
        {"Fortschritt", "Progress"},
        {"Konto", "Account"},
        {"noch kein Spiel geladen", "no game loaded yet"},
        {"Achievements", "Achievements"},
        {"Achievements %1/%2", "Achievements %1/%2"},
        {"%1 von %2 Punkten", "%1 of %2 points"},
        {"Protokoll", "Log"},
        {"Alle", "All"},
        {"Offen", "Open"},
        {"Freigeschaltet", "Unlocked"},
        {"Keine Achievements in dieser Auswahl.", "No achievements in this selection."},
        {"neu!", "new!"},
        {"wartet", "waiting"},
        {"Klick = Diagnose im Protokoll, Doppelklick = auf RetroAchievements oeffnen",
         "Click = diagnosis in the log, double-click = open on RetroAchievements"},
        {"Eingereicht wird nur im Hardcore-Modus -- der ist fuer dieses Geraet noch gesperrt. Laeufe werden hier trotzdem angezeigt.",
         "Entries are only submitted in Hardcore mode -- which is still locked for this device. Runs are shown here anyway."},
        {"Klick auf ein Achievement schreibt seine Diagnose hierher.",
         "Clicking an achievement writes its diagnosis here."},
        {"Eingereicht wird nur im Hardcore-Modus.", "Entries are only submitted in Hardcore mode."},
        {"keine Leaderboards", "no leaderboards"},
        {"Leaderboard #%1", "Leaderboard #%1"},
        {"wartet auf Start", "waiting for start"},
        {"laeuft", "running"},
        {"abgebrochen", "cancelled"},
        {"eingereicht: ", "submitted: "},

        // --- Dialoge ---------------------------------------------------------
        {"Ja", "Yes"},
        {"Nein", "No"},
        {"Abbrechen", "Cancel"},
        {"Schliessen", "Close"},
        {"Sprache / Language", "Language / Sprache"},
        {"Automatik", "Automation"},
        {"Ueberwachung automatisch starten, sobald ein Spiel laeuft",
         "Start monitoring automatically as soon as a game is running"},
        {"Freischalt-Anzeige", "Unlock notification"},
        {"Freischalt-Anzeige testen", "Test unlock notification"},
        {"Probe-Anzeige", "Test notification"},
        {"So sieht eine Freischaltung aus.", "This is what an unlock looks like."},
        {"Hilfe", "Help"},
        {"Fehler melden", "Report a bug"},
        {"Oeffnet die Fehlerliste auf GitHub im Browser.", "Opens the issue list on GitHub in the browser."},
        {"Fuer Fehlermeldungen: das komplette Log in die Zwischenablage.",
         "For bug reports: copies the complete log to the clipboard."},
        {"ueber die Katze", "about the cat"},
        {"aber falls du darueber nachdenkst,\nlies bitte zuerst 'ueber die Katze'",
         "but if you are thinking about it,\nplease read 'about the cat' first"},
        {"Spiel nicht erkannt", "Game not recognised"},
        {"Die laufende ROM konnte nicht vom SD2SNES/FXPak gelesen werden.\n\nDieselbe ROM jetzt am PC auswaehlen? Daraus wird der Hash fuer RetroAchievements gebildet.",
         "The running ROM could not be read from the SD2SNES/FXPak.\n\nSelect the same ROM on this PC now? The RetroAchievements hash is computed from it."},
        {"SNES-ROM waehlen", "Select SNES ROM"},
        {"Hardcore beendet", "Hardcore ended"},
        {"Hardcore beendet -- Grund: ", "Hardcore ended -- reason: "},
        {"Hardcore ist fuer diese Sitzung nicht moeglich: %1\n\nEs zaehlt jetzt Softcore. Fuer Hardcore im Menue des SD2SNES/FXPak Savestates (In-game Savestates) und Cheats ausschalten, dann die Ueberwachung neu starten.",
         "Hardcore is not possible for this session: %1\n\nSoftcore counts now. For Hardcore, switch off savestates (in-game savestates) and cheats in the SD2SNES/FXPak menu, then restart monitoring."},
        {"Das Programm ist abgestuerzt.\n\nEinzelheiten stehen in %1",
         "The program has crashed.\n\nDetails are in %1"},

        // --- Protokoll -------------------------------------------------------
        {"Update verfuegbar", "Update available"},
        {"Eine neue Version ist verfuegbar (Build %1, du hast Build %2).\n\nDownload-Seite oeffnen?",
         "A new version is available (build %1, you have build %2).\n\nOpen the download page?"},
        {"RAW-SNES %1 (Build %2) bereit -- suche SD2SNES/FXPak...", "RAW-SNES %1 (build %2) ready -- looking for SD2SNES/FXPak..."},
        {"Verbunden mit %1 auf %2 (Firmware %3).", "Connected to %1 on %2 (firmware %3)."},
        {"SD2SNES/FXPak antwortet nicht: ", "SD2SNES/FXPak does not respond: "},
        {"SD2SNES/FXPak-Suche: ", "SD2SNES/FXPak search: "},
        {"SD2SNES/FXPak ist beschaeftigt -- warte auf seine Antwort.", "SD2SNES/FXPak is busy -- waiting for its reply."},
        {"Verbindung zum SD2SNES/FXPak verloren: ", "Connection to the SD2SNES/FXPak lost: "},
        {"Verbindung zum SD2SNES/FXPak unterbrochen (z. B. beim Spielwechsel) -- verbinde neu: ", "Connection to the SD2SNES/FXPak interrupted (e.g. while switching games) -- reconnecting: "},
        {"Verbindung wiederhergestellt.", "Connection restored."},
        {"SD2SNES/FXPak: Menue.", "SD2SNES/FXPak: menu."},
        {"SD2SNES/FXPak: laufende ROM ", "SD2SNES/FXPak: running ROM "},
        {"Spiel erkannt -- Ueberwachung startet automatisch.", "Game detected -- monitoring starts automatically."},
        {"Kein SD2SNES/FXPak gefunden -- eingesteckt und Konsole an?", "No SD2SNES/FXPak found -- plugged in and console on?"},
        {"Kein SD2SNES/FXPak verbunden.", "No SD2SNES/FXPak connected."},
        {"Am SD2SNES/FXPak laeuft gerade kein SNES-Spiel -- erst ein Spiel starten.",
         "No SNES game is running on the SD2SNES/FXPak -- start a game first."},
        {"ROM-Header: \"%1\", %2, %3 KiB", "ROM header: \"%1\", %2, %3 KiB"},
        {"Hash bekannt: ", "Hash known: "},
        {"Lese %1 von der SD-Karte...", "Reading %1 from the SD card..."},
        {"Hash: %1 (%2 KiB)", "Hash: %1 (%2 KiB)"},
        {"Datei nicht lesbar: ", "File not readable: "},
        {"Nehme ersatzweise das ROM-Abbild im Modul.", "Using the ROM image in the cartridge instead."},
        {"Hash (ROM-Abbild): %1", "Hash (ROM image): %1"},
        {"ROM-Abbild nicht lesbar: ", "ROM image not readable: "},
        {": kein Achievement-Set.", ": no achievement set."},
        {"%1 Achievement(s) nicht auswertbar (Adressen hier nicht lesbar oder unbekannte Syntax).",
         "%1 achievement(s) cannot be evaluated (addresses not readable here or unknown syntax)."},
        {"Spiel mit Zusatzchip (%1): Das SD2SNES/FXPak fuehrt dafuer keine WRAM-Kopie. "
         "Die Werte kopiert die Konsole selbst, einmal je Bild ueber den NMI-Hook.",
         "Game with an enhancement chip (%1): the SD2SNES/FXPak keeps no WRAM copy for it. "
         "The console copies the values itself, once per frame through the NMI hook."},
        {"Ohne Werte-Kopie kann dieses Spiel nicht ueberwacht werden.",
         "Without the value copy this game cannot be monitored."},
        {"S-DD1: Die Werte werden erst nach dem IRQ des Spiels kopiert.",
         "S-DD1: the values are copied only after the game's IRQ."},
        {"Sonde: %1 Durchlaeufe in 0,4 s, davon %2 ueber den NMI -- %3.",
         "Probe: %1 runs in 0.4 s, %2 of them via NMI -- %3."},
        {"Kopie im NMI", "copy in the NMI"},
        {"Sonde: Der Hook laeuft noch nicht (Spiel laedt, oder In-Game-Hook aus?) -- warte.",
         "Probe: the hook is not running yet (game loading, or in-game hook off?) -- waiting."},
        {"Sonde: kein Ergebnis -- Kopie im NMI.", "Probe: no result -- copy in the NMI."},
        {"Der Hook laeuft bei diesem Spiel jetzt nur ueber IRQs -- neu messen.",
         "The hook now runs only via IRQs in this game -- measuring again."},
        {"NMI im Hook gesehen -- Werte-Kopie jetzt im NMI.", "NMI seen in the hook -- value copy now in the NMI."},
        {"Kopie in der Austastluecke", "copy in the vertical blank"},
        {"Kopie nach dem IRQ des Spiels", "copy after the game's IRQ"},
        {"NMI-Hook der Firmware nicht erkannt (andere Firmware-Version?) -- Werte-Kopie nicht moeglich.",
         "Firmware NMI hook not recognised (different firmware version?) -- value copy not possible."},
        {"WRAM-Cheats aktiv -- die Werte-Kopie braucht deren Platz. Cheats fuer dieses Spiel abschalten.",
         "WRAM cheats active -- the value copy needs their space. Switch off cheats for this game."},
        {"Werte-Kopie: Schreiben in den Befehlspuffer fehlgeschlagen (Rueckleseprobe).",
         "Value copy: writing to the command buffer failed (read-back check)."},
        {"Werte-Kopie: Einhaengen in den NMI-Hook fehlgeschlagen (Rueckleseprobe).",
         "Value copy: hooking into the NMI hook failed (read-back check)."},
        {"Werte-Kopie eingehaengt: %1 von %2 offenen Achievements, %3 Adressen, ca. %4 Takte je Bild.",
         "Value copy hooked in: %1 of %2 open achievements, %3 addresses, about %4 cycles per frame."},
        {"Werte-Kopie: ", "Value copy: "},
        {"Werte-Kopie ausgehaengt.", "Value copy unhooked."},
        {"NMI-Hook neu geschrieben (Reset?) -- Werte-Kopie wird wieder eingehaengt.",
         "NMI hook rewritten (reset?) -- hooking the value copy in again."},
        {"Seit 3 s kein Bild vom NMI-Hook. Ist der In-Game-Hook am SD2SNES/FXPak eingeschaltet "
         "(Menue: In-game hook)? Oder pausiert bzw. laedt das Spiel gerade?",
         "No frame from the NMI hook for 3 s. Is the in-game hook switched on on the SD2SNES/FXPak "
         "(menu: In-game hook)? Or is the game paused or loading?"},
        {"NMI-Hook liefert wieder Bilder.", "NMI hook delivers frames again."},
        {"Diagnose (10 s): %1 Bilder/s, %2 ausgewertet, %3 uebersprungen (Ruckeln bei diesem Chip nicht messbar)",
         "Diagnostics (10 s): %1 frames/s, %2 evaluated, %3 skipped (stutter not measurable with this chip)"},
        {"Bildtakt ueber die Austastluecke: Bei diesem Spiel laufen nur IRQs durch den Hook.",
         "Frame timing via vertical blanking: with this game only IRQs pass through the hook."},
        {"  Routine durchlaufen: %1 Mal, zuletzt gelesenes Vektor-Byte $%2.",
         "  Routine ran: %1 times, last vector byte read $%2."},
        {"Diagnose (10 s): %1 Bilder/s, %2 ausgewertet, %3 uebersprungen, Abfrage %4 ms, "
         "verspaetete Bilder (Ruckeln) %5 = %6 %",
         "Diagnostics (10 s): %1 frames/s, %2 evaluated, %3 skipped, query %4 ms, "
         "late frames (stutter) %5 = %6 %"},
        {"Werte-Kopie: %1 Zeigerziel(e) dazu, Routine wird neu geschrieben.",
         "Value copy: %1 pointer target(s) added, rewriting the routine."},
        {"Werte-Kopie: %1 Bilder je Sekunde ausgewertet, %2 verpasst.",
         "Value copy: %1 frames per second evaluated, %2 missed."},
        {"Leaderboards: %1 geladen (%2 nicht auswertbar).", "Leaderboards: %1 loaded (%2 not evaluable)."},
        {"%1 Adressen werden ueberwacht (%2 Abfrage(n) je Durchgang).",
         "%1 addresses monitored (%2 request(s) per pass)."},
        {"%1 Achievements geladen (%2 bereits freigeschaltet).", "%1 achievements loaded (%2 already unlocked)."},
        {"Savestates", "Savestates"},
        {"Cheats", "Cheats"},
        {"Einstellungen nicht lesbar", "settings not readable"},
        {"ACHTUNG: ", "WARNING: "},
        {" am SD2SNES/FXPak aktiviert -- Hardcore wird fuer den Rest der Sitzung beendet, ab jetzt Softcore.",
         " enabled on the SD2SNES/FXPak -- Hardcore ends for the rest of this session, Softcore from now on."},
        {" am SD2SNES/FXPak aktiviert -- nur Softcore verfuegbar. Fuer Hardcore im SD2SNES/FXPak-Menue deaktivieren, dann Monitor neu starten.",
         " enabled on the SD2SNES/FXPak -- only Softcore available. For Hardcore, disable in the SD2SNES/FXPak menu, then restart the monitor."},
        {"Hardcore-Pruefung nicht moeglich (Einstellungen des SD2SNES/FXPak nicht lesbar) -- nur Softcore.",
         "Hardcore check not possible (SD2SNES/FXPak settings not readable) -- Softcore only."},
        {"Hardcore-Voraussetzung erfuellt: Savestates und Cheats sind am SD2SNES/FXPak aus.",
         "Hardcore requirement met: savestates and cheats are off on the SD2SNES/FXPak."},
        {"Spiel beendet (Menue) -- Ueberwachung endet.", "Game ended (menu) -- monitoring stops."},
        {"Spiel gewechselt -- Ueberwachung endet: ", "Game changed -- monitoring stops: "},
        {"Spiel laeuft (%1 Adressen aktiv) -- alle Achievements scharf.",
         "Game running (%1 addresses active) -- all achievements armed."},
        {"WARNUNG: nach 3 s keine einzige Wertaenderung.\n  Moegliche Gruende:\n  1. Laeuft das Spiel wirklich (nicht pausiert, nicht im Menue)?\n  2. Beschreibt dieses Spiel die Adressen schon (Titelbild)?\n  3. Ist die Firmware des SD2SNES/FXPak aktuell?",
         "WARNING: not a single value change after 3 s.\n  Possible reasons:\n  1. Is the game really running (not paused, not in the menu)?\n  2. Does the game write these addresses yet (title screen)?\n  3. Is the SD2SNES/FXPak firmware up to date?"},
        {"Daten kommen an: %1/%2 Adressen aendern sich.", "Data is arriving: %1/%2 addresses are changing."},
        {"Erste Wertaenderung nach %1 s (%2) -- die Werte kommen an.",
         "First value change after %1 s (%2) -- values are arriving."},
        {"Achievement freigeschaltet: %1 (%2)", "Achievement unlocked: %1 (%2)"},
        {"gebucht", "recorded"},
        {"Werte: ", "Values: "},
        {"Abfragetakt: %1 Durchgaenge je Sekunde (Ziel 60).", "Polling rate: %1 passes per second (target 60)."},
        {"Fehler: ", "Error: "},
        {"  Leaderboard %1: Wert %2 -- NICHT eingereicht (nur im Hardcore-Modus gueltig).",
         "  Leaderboard %1: value %2 -- NOT submitted (only valid in Hardcore mode)."},
        {"  *** Leaderboard %1: %2 (%3) ***", "  *** Leaderboard %1: %2 (%3) ***"},
        {"eingereicht", "submitted"},
        {"Leaderboard laeuft: ", "Leaderboard running: "},
        {"Leaderboard abgebrochen: ", "Leaderboard cancelled: "},
        {"Leaderboard eingereicht: ", "Leaderboard submitted: "},
        {"RA-Aktivitaet: Fehler: ", "RA activity: error: "},
        {"Monitor gestoppt.", "Monitor stopped."},
        {"\n--- %1 (%2P) ---", "\n--- %1 (%2P) ---"},
        {"MemAddr: ", "MemAddr: "},
        {"am SD2SNES/FXPak nicht lesbare Adressen: ", "addresses not readable on the SD2SNES/FXPak: "},
        {"Bedingung wird nicht unterstuetzt -- feuert nie.", "Condition is not supported -- will never trigger."},
        {"noch keine Messwerte -- Monitor starten.", "no measurements yet -- start the monitor."},
        {"aktuelle Werte: ", "current values: "},
        {", ", ", "},
        {"Adressen bestaetigt -- wird ausgewertet.", "Addresses confirmed -- now being evaluated."},
        {"wartet noch auf bestaetigte Adressen (Schutz gegen Fehlbuchungen).",
         "still waiting for confirmed addresses (protection against false submissions)."},
    };
    return t;
}

}  // namespace

void SpracheSetzen(const std::string& kennung) {
    g_sprache = (kennung == "en") ? "en" : "de";
}

const std::string& Sprache() { return g_sprache; }

bool Englisch() { return g_sprache == "en"; }

const char* T(const char* deutsch) {
    if (!deutsch || g_sprache != "en") return deutsch;
    const auto& t = TabelleEn();
    auto it = t.find(deutsch);
    return it == t.end() ? deutsch : it->second.c_str();
}

std::string Fmt(const std::string& muster, std::initializer_list<std::string> werte) {
    // %1 bekommt den ersten Wert,
    // %2 den zweiten usw. Ersetzt wird in einem Durchgang, damit ein
    // eingesetzter Wert, der selbst "%2" enthaelt, nicht nachtraeglich
    // ersetzt wird.
    std::string aus;
    aus.reserve(muster.size() + 32);
    const std::string* w = werte.begin();
    const size_t n = werte.size();
    for (size_t i = 0; i < muster.size(); ++i) {
        const char c = muster[i];
        if (c == '%' && i + 1 < muster.size() && muster[i + 1] >= '1' && muster[i + 1] <= '9') {
            const size_t idx = (size_t)(muster[i + 1] - '1');
            if (idx < n) {
                aus += w[idx];
                ++i;
                continue;
            }
        }
        aus.push_back(c);
    }
    return aus;
}

std::string Zahl(long long n) { return std::to_string(n); }

std::string Hex(unsigned long long n, int breite) {
    char t[32];
    std::snprintf(t, sizeof(t), "%0*llx", breite, n);
    return t;
}

}  // namespace i18n
