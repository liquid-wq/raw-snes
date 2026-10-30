// Simuliertes FXPak fuer den Selbsttest.
//
// Beantwortet das USB-Protokoll wie das Modul: Geraeteinfo, Lesen (ein und mehrere Bereiche), Dateien von der
// SD-Karte und Konfigurationswerte. Der Speicher des Moduls ist ein
// 16-MiB-Feld mit derselben Aufteilung wie am Geraet (ROM ab 0, SRAM ab
// E00000, WRAM-Spiegel ab F50000).
#pragma once

#include "fxpak_port.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class FxPakSim {
public:
    FxPakSim();

    // Zustand des Moduls; aus jedem Thread aenderbar.
    void SetzeRom(const std::string& pfad);
    void SetzeEinstellungen(uint16_t e);
    void SetzeAngesteckt(bool an);
    void Datei(const std::string& pfad, std::vector<uint8_t> inhalt);
    void Konfig(const std::string& schluessel, const std::string& wert);
    // Schreibt wie das Spiel in den WRAM (RA-Adresse 0..1FFFF). Die Kopie ab
    // F50000 laeuft nur mit, solange der FPGA-Kern sie fuehrt (SetzeSpiegel).
    void WramSchreiben(uint32_t ra, uint8_t wert);
    void IramSchreiben(uint32_t versatz, uint8_t wert);   // SA-1-I-RAM, 0..7FF
    void Speicher(uint32_t adresse, const std::vector<uint8_t>& daten);

    // Kern mit Zusatzchip: keine WRAM-Kopie.
    void SetzeSpiegel(bool an);
    // NMI-Hook der Firmware ("EnableIngameHook").
    void SetzeHook(bool an);
    // Wie bei Super FX am Geraet gemessen: das NMI laeuft am Hook vorbei,
    // nur die IRQs gehen hindurch.
    void SetzeNurIrq(bool an);
    // Ein Bild: ist der Hook an, fuehrt die simulierte Konsole ihn aus --
    // samt eingehaengter Routine. false, wenn dabei etwas schiefging
    // (unbekannter Befehl, Register oder Stapel nicht wiederhergestellt).
    bool Bild(std::string* fehler = nullptr);
    // Ein IRQ durch denselben Hook (bei manchen Spielen mehrmals je Bild),
    // in Bildschirmzeile `zeile`. Bild() laeuft in Zeile 225.
    bool Irq(std::string* fehler = nullptr, int zeile = 100);
    int Bilder();
    // Spiel-Reset: die Firmware schreibt den Hook neu.
    void HookNeuSchreiben();
    uint8_t Puffer(uint32_t adresse);   // Befehlspuffer, $2A00-$2BFF

    bool Angesteckt();
    int Befehle();  // Anzahl bearbeiteter Befehle

    // Wie die echte Firmware: Die naechste Antwort kommt erst nach `ms`; wer
    // in dieser Zeit Daten schickt, verstopft den Empfang dauerhaft.
    void NaechsteAntwortVerzoegern(int ms);
    int NimmVerzoegerung();
    void Verstopfen();
    bool Verstopft();

    // Bearbeitet einen vollstaendigen Befehl und liefert alle Antwortbytes.
    std::vector<uint8_t> Bearbeite(const std::vector<uint8_t>& befehl);
    // Spielstart (Befehl 9) samt Schaltern; holt die Nachbildung der Konsole ab.
    struct Startauftrag {
        std::string pfad;
        uint8_t schalter = 0;
    };
    static constexpr uint8_t kNurReset = 0x80;   // Zuruecksetzen statt Starten
    bool NimmStart(Startauftrag& s);
    // Sofort beim Befehl (unter Sperre, Speicher nur ueber Roh()): wie die Firmware,
    // die ROM und Kern vor der Antwort laedt.
    std::function<void(const Startauftrag&)> beimStart;
    // Befehl an den Mikrocontroller ueber den Befehlspuffer ($2A00, vom PC
    // geschrieben); wie beimStart unter Sperre.
    std::function<void(uint8_t befehl)> beimBefehl;
    // Fuer die Nachbildung der Konsole: Speicher und Befehlspuffer direkt
    // (Schreiben unter Sperre()).
    uint8_t* Roh() { return speicher_.data(); }
    uint8_t* RohPuffer() { return puffer_.data(); }
    std::mutex& Sperre() { return m_; }
    // Nutzdaten eines Schreibbefehls (Befehl 1), nach dem Befehl empfangen.
    void Schreibdaten(uint8_t raum, uint32_t adresse, const std::vector<uint8_t>& daten);

    // Hat ein Hook-Lauf seit dem letzten Abruf Direktseite oder Datenbank
    // verstellt? Setzt die Markierung zurueck.
    bool NimmSeitenGeaendert() { const bool r = seitenGeaendert_; seitenGeaendert_ = false; return r; }
    // Wie oft lief ein Nachlauf (umgelenkter Ruecksprung) bis zur Rueckkehr?
    int Nachlaeufe() const { return nachlaeufe_; }
    // Befehle des letzten Hook-Laufs (ohne Nachlauf).
    int HookBefehle() const { return hookBefehle_; }

private:
    bool seitenGeaendert_ = false;
    int nachlaeufe_ = 0;
    int hookBefehle_ = 0;
    void HookAnlegen();
    bool HookLauf(uint8_t vektor, int zeile, std::string* fehler);
    std::mutex m_;
    std::vector<uint8_t> speicher_;
    std::vector<uint8_t> wram_ = std::vector<uint8_t>(0x20000, 0);
    std::vector<uint8_t> iram_ = std::vector<uint8_t>(0x800, 0);
    std::vector<uint8_t> puffer_ = std::vector<uint8_t>(0x200, 0);
    bool spiegel_ = true;
    bool hook_ = false;
    bool nurIrq_ = false;
    int bilder_ = 0;
    std::map<std::string, std::vector<uint8_t>> dateien_;
    std::map<std::string, std::string> konfig_;
    std::string rom_;
    uint16_t einstellungen_ = 0;
    bool angesteckt_ = true;
    int befehle_ = 0;
    int verzoegerung_ = 0;
    bool verstopft_ = false;
    std::vector<Startauftrag> starts_;
};

// Leitung zum simulierten Modul.
std::unique_ptr<Leitung> SimLeitung(std::shared_ptr<FxPakSim> sim);
