// Verbindung zum FXPak: Protokollablauf (Befehl, Antwort, Datenbloecke) ueber
// eine austauschbare Leitung.
//
// Die Leitung ist im Betrieb der serielle USB-Anschluss unter Windows
// (Port-Suche ueber die USB-Kennung, Lesen mit Zeitgrenze im Treiber -- aus
// dem eigenen ed_serial.cpp von RAW-NES). Im Selbsttest steht dort ein
// simuliertes Modul. Das Protokoll selbst kommt aus core/fxpak_protokoll.
// Alle Aufrufe blockieren den aufrufenden Thread.
#pragma once

#include "fxpak_protokoll.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

using Bytes = std::vector<uint8_t>;

class Leitung {
public:
    virtual ~Leitung() = default;
    virtual void Oeffnen() = 0;                       // wirft bei Fehlschlag
    virtual void Schliessen() = 0;
    virtual bool Offen() const = 0;
    virtual void Schreiben(const uint8_t* d, size_t n) = 0;  // wirft bei Fehlschlag
    // Kehrt zurueck, sobald `anzahl` Bytes da sind oder die Zeit um ist.
    virtual Bytes LesenBis(uint32_t anzahl, int zeitMs) = 0;
    virtual void EingangLeeren() = 0;
};

// COM-Anschluss unter Windows ("COM5").
std::unique_ptr<Leitung> SerielleLeitung(const std::string& port);

class FxPakPort {
public:
    // Ohne Leitung: serieller Anschluss mit diesem Namen.
    explicit FxPakPort(std::string port, std::unique_ptr<Leitung> leitung = nullptr);
    ~FxPakPort();

    // Oeffnet die Leitung und prueft mit einer Info-Anfrage, ob ein FXPak
    // antwortet. Wirft std::runtime_error bei Fehlschlag.
    void Oeffnen();
    void Schliessen();
    bool Offen() const { return leitung_ && leitung_->Offen(); }

    fxpak::GeraeteInfo Info();
    Bytes Lesen(uint32_t adresse, uint32_t laenge);
    Bytes LesenRaum(fxpak::Raum raum, uint32_t adresse, uint32_t laenge);
    // Schreibt ohne Antwort des Moduls (Befehl und Daten am Stueck).
    void Schreiben(fxpak::Raum raum, uint32_t adresse, const Bytes& daten);
    // Liest bis zu acht Bereiche in einem Durchgang, Ergebnis lueckenlos.
    Bytes LesenMehrfach(const std::vector<fxpak::Bereich>& bereiche);
    // Ganze Datei von der SD-Karte des Moduls. fortschritt(gelesen, gesamt)
    // darf leer sein. Wirft bei Fehler.
    Bytes DateiLesen(const std::string& pfad,
                     const std::function<void(uint32_t, uint32_t)>& fortschritt = nullptr);
    // Datei auf die SD-Karte schreiben (ersetzt sie), umbenennen, loeschen.
    // Nur im Menue des Moduls benutzen. Werfen bei Fehler.
    void DateiSchreiben(const std::string& pfad, const Bytes& daten);
    void Umbenennen(const std::string& pfad, const std::string& neuerName);
    void DateiLoeschen(const std::string& pfad);
    // ROM von der Karte starten. Ob das Modul vor dem Laden antwortet, ist
    // nicht belegt: fehlt die Antwort, wird die Leitung neu ausgerichtet.
    void Starten(const std::string& pfad, uint8_t schalter = 0);
    // Konsole zuruecksetzen; die Antwort kommt vor dem Zuruecksetzen.
    void Zuruecksetzen();
    // Wert aus der Modul-Konfiguration; leer, wenn unbekannt oder nicht lesbar.
    std::optional<std::string> KonfigLesen(const std::string& schluessel);

    const std::string& Portname() const { return port_; }

    // Bringt den Empfang des Moduls wieder auf Blockanfang (siehe .cpp).
    bool Synchronisieren();

    // Wurde der Befehl gar nicht erst gesendet, weil die Antwort auf den
    // vorigen noch aussteht? Das ist kein Verbindungsfehler.
    static bool Beschaeftigt(const std::exception& e);

private:
    bool InfoBeantwortet(int zeitMs);
    // Wartet hoechstens zeitMs, bis eine ausstehende Antwort ganz da ist, und
    // verwirft sie. true: es steht nichts mehr aus.
    bool Nachholen(int zeitMs);
    // Ein Befehl mit Antwort und Nutzdaten (in Bloecken zu datenBlock).
    // Wirft bei Fehler; eine unvollstaendige Antwort wird vorgemerkt.
    Bytes Austausch(const fxpak::Paket& p, int datenBlock, uint8_t* kopf, int kopfMs, int datenMs,
                    fxpak::Antwort* antwort = nullptr);

    std::string port_;
    std::unique_ptr<Leitung> leitung_;
    bool nimmtNichtsAn_ = false;   // letzter Sendeversuch: kein Byte angenommen
    // Ausstehende Antwort: Kopf (teilweise in teil_) und/oder Nutzdaten.
    bool ausstehendKopf_ = false;
    uint32_t ausstehendDaten_ = 0;
    int ausstehendBlock_ = fxpak::kBlock;
    long long ausstehendSeit_ = 0;
    // Laenger bleibt keine Antwort aus, die noch kommt; danach gilt sie als
    // verloren (z. B. weil der Befehl selbst nie vollstaendig ankam).
    int ausstehendMaxMs_ = 10000;
    Bytes teil_;
    std::mutex lock_;
};

struct PortInfo {
    std::string name;
    uint16_t vid = 0, pid = 0;
    bool hatKennung = false;
};
std::vector<PortInfo> AlleComPorts();

// Sucht ein FXPak: erst `bevorzugt`, dann Ports mit der FXPak-Kennung.
// Gibt es keinen solchen Port, werden alle USB-Seriell-Ports angefragt
// (Nachbauten melden sich teils mit anderer Kennung); Ports ohne USB-Kennung
// (Bluetooth u. ae.) bleiben aussen vor. nullptr, wenn keines antwortet.
// `bericht` (darf leer sein): was gefunden und versucht wurde, eine Zeile.
std::unique_ptr<FxPakPort> FindeFxPak(const std::string& bevorzugt = std::string(),
                                      std::string* bericht = nullptr);
