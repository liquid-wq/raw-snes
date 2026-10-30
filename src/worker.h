// Monitor-Worker: Verbindung zum FXPak, Spielerkennung, Abfrageschleife.
//
// Aufbau wie in RAW-NES: ein eigener Thread mit Auftragswarteschlange, die
// Verbindung zum Modul lebt ausschliesslich dort; die Oberflaeche erfaehrt
// alles ueber Rueckrufe (im Worker-Thread aufgerufen, von der Oberflaeche in
// ihre eigene Warteschlange weitergereicht).
//
// Anders als bei RAW-NES:
//  - Autoerkennung: im Leerlauf sucht der Worker selbst nach dem Modul und
//    meldet, welche ROM gerade laeuft. Auf Wunsch startet die Ueberwachung
//    von selbst, sobald ein Spiel laeuft, und wechselt mit dem Spiel.
//  - Spielerkennung ohne ROM-Sammlung: die laufende ROM wird ueber USB von
//    der SD-Karte gelesen und gehasht.
//  - Gelesen wird gebuendelt (bis zu acht Bereiche je Abfrage), ca. 60 Mal
//    je Sekunde.
#pragma once

#include "ConditionEngine.h"
#include "RichPresence.h"
#include "fxpak_port.h"
#include "nmi_kopie.h"
#include "ra_client.h"
#include "snes_kopf.h"
#include "snes_speicher.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using LeaderboardList = std::vector<std::pair<long long, std::string>>;

// Wie vollstaendig ein Spiel ueberwacht wird. "Ohne Platz" heisst: die Adressen
// waeren lesbar, passen aber gerade nicht in die Werte-Kopie ueber den Hook
// (nur bei Spielen mit Zusatzchip). "Nicht auswertbar" heisst: Adressen hier
// nicht lesbar oder Schreibweise unbekannt.
struct Abdeckung {
    int offen = 0, aktiv = 0, ohnePlatz = 0, nichtAuswertbar = 0;
    int lbGesamt = 0, lbAktiv = 0, lbOhnePlatz = 0, lbNichtAuswertbar = 0;
    std::set<long long> ohnePlatzIds;   // gerade nicht ueberwacht, weil ohne Platz
    // Massgeblich sind die Achievements. Leaderboard-Eintraege nimmt RetroAchievements
    // nur im Hardcore-Modus an, den es fuer dieses Geraet noch nicht gibt; sie werden
    // mitgezaehlt, zaehlen aber nicht fuer den Hinweis.
    bool voll() const { return ohnePlatz == 0 && nichtAuswertbar == 0; }
    bool operator==(const Abdeckung& o) const {
        return offen == o.offen && aktiv == o.aktiv && ohnePlatz == o.ohnePlatz &&
               nichtAuswertbar == o.nichtAuswertbar && lbGesamt == o.lbGesamt && lbAktiv == o.lbAktiv &&
               lbOhnePlatz == o.lbOhnePlatz && lbNichtAuswertbar == o.lbNichtAuswertbar && ohnePlatzIds == o.ohnePlatzIds;
    }
};

class MonitorWorker {
public:
    // sucher: liefert ein geoeffnetes Modul oder nullptr; leer = FindeFxPak().
    // Im Selbsttest steht hier ein simuliertes Modul.
    using Sucher = std::function<std::unique_ptr<FxPakPort>()>;
    explicit MonitorWorker(std::shared_ptr<RaClient> client, Sucher sucher = nullptr);
    ~MonitorWorker();

    // --- Einstellungen (aus der Oberflaeche, jederzeit) --------------------
    void setAnmeldung(const std::string& user, const std::string& token);
    void setAutostart(bool an);
    void setHardcore(bool an);
    // Diagnosedatei (eine Zeile je Sekunde); leer = keine.
    void setDiagnoseDatei(const std::string& pfad);
    void seedHashes(const std::map<std::string, std::string>& hashes);

    // --- Auftraege (laufen im Worker-Thread) -------------------------------
    void startMonitoring();
    void continueWithPath(const std::string& path);
    void diagnoseAchievement(long long achievementId);

    // Direkt, nicht ueber die Warteschlange: beendet die Ueberwachung.
    // Der Autostart greift fuer dieselbe ROM erst wieder nach einem Wechsel.
    void stop();

    // --- Rueckrufe (werden im Worker-Thread aufgerufen) -------------------
    std::function<void(const std::string&)> log;
    std::function<void(const std::string&, const fxpak::GeraeteInfo&)> connected;
    std::function<void(const std::string&)> disconnected;
    // Pfad der laufenden ROM auf der SD-Karte; leer = Menue.
    std::function<void(const std::string&)> romChanged;
    std::function<void(uint32_t, uint32_t)> identifyProgress;
    std::function<void()> monitoringStarted;
    std::function<void(const std::string&, int, long long, bool)> unlocked;
    std::function<void(long long, int, int)> progressChanged;
    std::function<void(long long, bool)> achievementWaitingChanged;
    std::function<void(const std::string&)> connectionLost;
    std::function<void(const std::string&)> hardcoreDowngraded;
    std::function<void()> finished;
    std::function<void(long long, const std::string&)> leaderboardStarted;
    std::function<void(long long, const std::string&)> leaderboardCanceled;
    std::function<void(long long, const std::string&, const std::string&)> leaderboardSubmitted;
    std::function<void(const LeaderboardList&)> leaderboardsReady;
    // Bei jeder Aenderung der Abdeckung, auch wenn Achievements nachruecken.
    std::function<void(const Abdeckung&)> abdeckungGeaendert;
    // Spiel erkannt, aber der Server liefert keine Achievements (true: Fassung
    // von RetroAchievements nicht unterstuetzt).
    std::function<void(bool)> keinSet;
    std::function<void()> romNotDetected;
    std::function<void(const std::string&)> gameLoaded;
    std::function<void(const GameData&, const std::set<long long>&)> achievementsReady;
    std::function<void(const std::string&, const std::string&)> hashLearned;
    std::function<void(const std::vector<uint32_t>&)> liveValuesInit;
    std::function<void(int, uint8_t, int)> liveValueUpdate;

private:
    using Clock = std::chrono::steady_clock;

    void post(std::function<void()> auftrag);
    void run();
    void runPendingDiagnose();
    void leerlauf();

    bool verbinden();
    void trennen(const std::string& grund);

    void doStartMonitoring(bool automatisch);
    void doContinueWithPath(const std::string& path);
    void doDiagnose(long long achievementId);

    // Hash der laufenden ROM: Cache, sonst Datei von der SD, sonst ROM-Abbild.
    std::optional<std::string> ermittleHash(const std::string& romPfad);
    void ladeUndUeberwache(GameData game);

    struct TrackedAchievement {
        Achievement info;
        std::unique_ptr<rawsnes::AchievementRuntime> rt;
        bool unlocked = false;
        bool lesbar = true;            // alle Adressen im aktuellen Lesemodus erreichbar
        std::set<uint32_t> needed;
    };

    // Werte-Kopie ueber den NMI-Hook (Spiele mit Zusatzchip, nmi_kopie.h).
    const char* zusatzchip() const;     // nullptr = Standardkern mit WRAM-Kopie
    void werteAus(const rawsnes::RamMap& ram);
    bool kopieEinrichten(bool erstesMal);
    nmikopie::Takt kopieSonde();
    void kopieAushaengen();
    bool leseKopie(rawsnes::RamMap& ram);   // false: noch kein neues Bild
    bool lesbar(uint32_t ra) const;
    void abdeckungPruefen();
    Abdeckung abdeckung_;
    bool abdeckungGemeldet_ = false;

    std::optional<std::vector<std::string>> hardcoreViolations();
    void checkHardcoreAtStart();
    void reportViolations(const std::vector<std::string>& v, bool midSession);

    void runPollLoop();
    rawsnes::RamMap leseRam();

    void emitLog(const std::string& s) { if (log) log(s); }
    std::string anmeldungUser();
    std::string anmeldungToken();

    // --- Thread und Warteschlange ---
    std::thread thread_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<std::function<void()>> queue_;
    std::deque<long long> diagnoseQueue_;
    bool quit_ = false;

    // --- Einstellungen, aus jedem Thread gesetzt ---
    std::mutex einstMutex_;
    std::string user_, token_, diagPfadWunsch_;
    std::atomic<bool> autostart_{true};
    std::atomic<bool> hardcoreWunsch_{false};

    // --- Zustand (nur im Worker-Thread benutzt) ---
    std::unique_ptr<FxPakPort> fx_;
    fxpak::GeraeteInfo info_;
    std::string laufendeRom_;
    bool romBekannt_ = false;
    std::string autoGesperrtFuer_;     // nach Stopp: diese ROM nicht erneut automatisch
    Clock::time_point naechsteSuche_{};
    std::string letzterSuchbericht_;
    int fehlversuche_ = 0;            // Info-Aussetzer in Folge (Leerlauf und Abfrageschleife)
    Clock::time_point naechsteInfo_{};
    bool beschaeftigtGemeldet_ = false;
    std::map<std::string, std::string> hashes_;

    std::shared_ptr<RaClient> client_;
    Sucher sucher_;
    std::string user_lauf_, token_lauf_;
    GameData game_;
    std::set<long long> alreadyUnlocked_;

    std::atomic<bool> running_{false};
    std::atomic<bool> hardcore_{false};

    std::vector<TrackedAchievement> tracked_;
    std::vector<rawsnes::LeaderboardRuntime> leaderboards_;
    snes::Leseplan plan_;
    snes::Kopf kopf_;
    uint8_t chipUntertyp_ = 0;          // $FFBF bei eigenen Chips (Cx4 = $10)

    bool kopieModus_ = false;
    bool kopieAktiv_ = false;           // Hook-Ende zeigt auf unsere Routine
    nmikopie::Programm kopie_;
    std::vector<std::vector<uint32_t>> kopieLb_;   // Adressen je Leaderboard
    std::set<uint32_t> kopieZiele_;     // Zeigerziele (AddAddress), immer dabei
    std::set<uint32_t> neueZiele_;      // Zeigerziele, die noch fehlen
    bool kopieNeuBauen_ = false;        // nach einer Freischaltung: Platz neu verteilen
    nmikopie::Takt kopieTakt_ = nmikopie::Takt::Nmi;
    long long laeufeSeitEinrichten_ = 0;
    std::vector<nmikopie::Gruppe> kopieGruppen() const;
    void kopieAktualisiereLesbar();
    snes::Leseplan sramPlan_;           // SRAM/BW-RAM weiter ueber USB
    bool habeZaehler_ = false;
    uint8_t zaehler_ = 0;
    long long bilderGesamt_ = 0, verpasst_ = 0;
    Clock::time_point letztesBild_;
    Clock::time_point naechsterNeuaufbau_;
    bool stilleGemeldet_ = false;
    // Diagnose alle 10 s: Bilder, Abfragezeit, verspaetete Bilder (Ruckeln).
    void kopieDiagnose();
    std::map<uint16_t, long long> stapelGesamt_;
    int bilderFenster_ = 0, verpasstFenster_ = 0, lesungenFenster_ = 0, verspaetetFenster_ = 0;
    double lesezeitFenster_ = 0;
    Clock::time_point diagnoseSeit_;

    // Diagnosedatei wie bei MEGA-RAW: Sitzungskopf, dann jede Sekunde eine
    // Zeile mit Bildern, Lagbildern, Abfragezeit und Werten.
    void diagOeffnen();
    void diagSekunde(const rawsnes::RamMap& ram);
    void diagSchliessen();
    std::FILE* diag_ = nullptr;
    Clock::time_point diagStart_, diagTakt_;
    int dBilder_ = 0, dVerpasst_ = 0, dVerspaetet_ = 0, dAbfragen_ = 0, dAenderungen_ = 0;
    double dAbfrageMs_ = 0, dAbfrageMax_ = 0;
    uint16_t spRuhe_ = 0;
    // Durchlaeufe der Routine (NMI + IRQ) und gelieferte Vektor-Bytes.
    uint8_t letzteLaeufe_ = 0;
    bool habeLaeufe_ = false;
    int dLaeufe_ = 0;
    std::map<int, int> dVektoren_;
    rawsnes::RamMap prevRam_;
    bool havePrevRam_ = false;

    std::vector<uint32_t> watchedAddrs_;
    std::vector<int> changeCounts_;

    // SICHERUNG GEGEN FEHLBUCHUNGEN: Der WRAM-Spiegel im Modul enthaelt bis zum
    // ersten Schreibzugriff des Spiels Altwerte. Eine Adresse gilt erst als
    // bestaetigt, wenn sie sich geaendert hat; laufen genug Adressen eine
    // Weile, gelten alle als bestaetigt (Warmlauf).
    std::set<uint32_t> confirmedAddrs_;
    bool warmedUp_ = false;
    bool haveWarmSince_ = false;
    Clock::time_point warmSince_;
    Clock::time_point sessionStart_;
    bool warned3s_ = false;
    bool ersteAenderungGemeldet_ = false;
    long long durchgaenge_ = 0;
    bool taktGemeldet_ = false;
    static constexpr int kWarmupMinActive = 3;
    static constexpr double kWarmupSeconds = 5.0;
    std::set<long long> waitingIds_;
};
