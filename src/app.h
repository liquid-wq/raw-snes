// Hauptfenster von RAW-SNES.
//
// Querformat: Kopfleiste, links die Seitenleiste (Modul, Start, Fortschritt,
// Konto, Live-Werte), rechts Reiter fuer Achievements, Leaderboards und
// Protokoll. Weil Dear ImGui jedes Bild neu zeichnet, gibt es keine
// Widgets mit eigenem Zustand: der Zustand steht hier in Membern,
// Zeichnen() stellt ihn dar.
#pragma once

#include "bilder.h"
#include "einstellungen.h"
#include "ra_cache.h"
#include "ra_client.h"
#include "worker.h"

#include "imgui.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

struct Schriften {
    ImFont* ui = nullptr;         // Segoe UI
    ImFont* fett = nullptr;       // Segoe UI fett
    ImFont* kursiv = nullptr;     // Segoe UI kursiv
    ImFont* mono = nullptr;       // Consolas (Log, Live-Werte)
    ImFont* monoFett = nullptr;   // Consolas fett (Live-Werte)
    ImFont* courierFett = nullptr;    // Intro
    ImFont* courierKursiv = nullptr;  // Intro
};

class App {
public:
    App(void* fenster, float dpi, const Schriften& schriften);
    ~App();

    // Nach dem Anlegen des OpenGL-Kontexts.
    void Starten();
    // Ein Bild.
    void Zeichnen();
    // Querformat: links die Konsole, rechts die Reiter.
    static constexpr float kBreite = 1040.0f;
    static constexpr float kHoehe = 740.0f;

    // Aus beliebigem Thread: Auftrag fuer den Oberflaechen-Thread.
    void Post(std::function<void()> f);

    // Fuer die Absturzspur: das Log, wie es im Fenster steht.
    const std::deque<std::string>& LogZeilen() const { return log_; }

    // ---------------- Eingebettete Nutzung ----------------
    // Ohne Kopf (Menue, Portrait) und ohne eigene Anmeldung; der Rahmen
    // stellt beides. Die Logik laeuft ueber Takt() auch, wenn RAW-SNES gerade
    // nicht angezeigt wird.
    void SetzeEingebettet(bool e) { eingebettet_ = e; }
    // Nur die Einzel-Exe: fragt einmal im Hintergrund nach einer neueren
    // Veroeffentlichung. Das Ergebnis zeigt Takt() als Dialog.
    static void UpdatePruefen();
    void Takt();
    void ZeichneIn(ImVec2 pos, ImVec2 groesse, bool nurListeUndLog);
    void Dialoge() { ZeichneDialog(); }
    void LoginSetzen(const std::string& user, const std::string& token);
    void MonitorUmschalten() { onToggleMonitor(); }
    // Werkzeugeigene Schalter fuer Aktionen/Optionen des Rahmens.
    void ZeichneAutostart();
    void ZeichneProbeKnopf(float breite);
    struct StatusDaten {
        bool angemeldet = false, verbunden = false, monitorLaeuft = false, monitorMoeglich = false;
        std::string spiel, hinweis;
        int frei = 0, gesamt = 0;
    };
    StatusDaten StatusHolen() const;
    struct ZuletztDaten { std::string titel, spiel; };
    const std::vector<ZuletztDaten>& Zuletzt() const { return zuletzt_; }

private:
    bool eingebettet_ = false;
    bool nurListe_ = false;
    std::vector<ZuletztDaten> zuletzt_;

    // ---------------- Zustand der Oberflaeche ----------------
    enum class AchState { Locked, Unlocked, JustUnlocked, Unsupported, Waiting };
    struct AchZeile {
        long long id = 0;
        std::string title, desc, badge;
        int points = 0;
        AchState state = AchState::Locked;
        int progCur = 0, progTarget = 0;
        bool showProgress = false;
        bool ohnePlatz = false;   // gerade nicht ueberwacht (kein Platz im Hook)
    };
    struct LiveZeile {
        uint32_t addr = 0;
        int value = -1;
        int changeCount = 0;
        unsigned long long lastChangeMs = 0;
    };
    struct LbZeile {
        long long id = 0;
        std::string title, status;
    };

    // Eigene modale Dialoge.
    struct Dialog {
        enum class Art { Info, Warnung, Fehler, Frage, Katze, Optionen, Kofi };
        Art art = Art::Info;
        std::string titel, text;
        std::function<void()> ja, nein;
        bool geoeffnet = false;
        int nummer = 0;
    };

    // ---------------- Ablaeufe ----------------
    void onLogin();
    void onLogout();
    void onToggleMonitor();
    void setLanguage(const std::string& lang);
    void appendLog(const std::string& msg);
    void loadConfig();
    void saveConfig();
    std::string configPath() const;
    void monitorZuruecksetzen();
    void probeEinblendung();

    void verdrahteWorker();

    // ---------------- Dialoge ----------------
    void Info(const std::string& titel, const std::string& text, std::function<void()> ok = nullptr);
    void Warnung(const std::string& titel, const std::string& text);
    void Fehler(const std::string& titel, const std::string& text);
    void Frage(const std::string& titel, const std::string& text,
               std::function<void()> ja, std::function<void()> nein = nullptr);
    void DialogAuf(Dialog::Art art);
    void ZeichneDialog();
    void ZeichneOptionen(bool& schliessen);
    void ZeichneKatze(bool& schliessen);
    void ZeichneKatzenText();

    // ---------------- Badges ----------------
    struct Badge {
        enum class Zustand { Laedt, Fertig, Fehlt } zustand = Zustand::Laedt;
        bilder::Textur tex;
        std::string png;
    };
    Badge* BadgeHolen(const std::string& name);
    void BadgeThread();

    // ---------------- Zeichnen ----------------
    // Kopfleiste: Katze, Name, Tastenfarben, Konto, Optionen, Ko-fi.
    void ZeichneKopfleiste();
    // Seitenleiste: Modul als Cartridge, Start, Fortschritt, Konto, Live-Werte.
    void ZeichneSeite();
    void ZeichneCartridge(float breite);
    void ZeichneFortschritt(float breite);
    void ZeichneKonto(float breite);
    void ZeichneLiveWerte(float breite, float hoehe);
    void Abschnitt(const char* titel);
    // Hauptbereich: Reiter Achievements / Leaderboards / Log.
    void ZeichneHauptbereich(ImVec2 groesse);
    void ZeichneAchievements(float breite);
    void ZeichneKachel(AchZeile& z, ImVec2 groesse);
    void ZeichneLeaderboards(float breite);
    void ZeichneLog();
    enum class Reiter { Achievements, Leaderboards, Log };
    enum class Filter { Alle, Offen, Frei };
    Reiter reiter_ = Reiter::Achievements;
    Filter filter_ = Filter::Alle;

    // ---------------- Felder ----------------
    void* fenster_ = nullptr;
    float dpi_ = 1.0f;
    Schriften f_;

    RaCache cache_;
    std::shared_ptr<RaClient> client_;
    std::unique_ptr<MonitorWorker> worker_;

    std::mutex postMutex_;
    std::vector<std::function<void()>> postQueue_;

    std::string token_;
    Einstellungen cfg_;

    // Eingabefelder
    char userBuf_[128] = {};
    char passBuf_[128] = {};
    bool rememberLogin_ = true;
    bool hardcore_ = false;
    bool autostart_ = true;

    // Anmeldung. *Key = deutscher Quelltext, wird je Bild uebersetzt.
    std::string loginStatusKey_ = "nicht angemeldet";
    std::string loginStatusText_;
    bool loginOk_ = false;
    bool loginLaeuft_ = false;

    // Modul
    bool connected_ = false;
    std::string connPort_;
    std::string geraetText_;          // "sd2snes Mk.III · Firmware 1.11.0"
    std::string laufendeRom_;         // Pfad auf der SD-Karte, leer = Menue
    bool romBekannt_ = false;
    float hashFortschritt_ = -1.0f;   // 0..1 waehrend die ROM gelesen wird

    // Ueberwachung
    bool monitoring_ = false;
    std::string gameStatus_;          // Name laut RA
    bool hardcoreEnabled_ = true;

    std::vector<AchZeile> ach_;
    long long selectedId_ = -1;
    std::map<int, LiveZeile> live_;
    std::vector<LbZeile> lbs_;

    // Abdeckung des laufenden Spiels (Worker: abdeckungGeaendert). Ist sie nicht
    // voll, steht ein roter Hinweis unter der Cartridge; einmal je Spiel kommt
    // zusaetzlich ein Fenster mit OK.
    Abdeckung abdeckung_;
    bool abdeckungDa_ = false;
    // Spiel erkannt, Server liefert keine Achievements: 1 = kein Set, 2 = Fassung nicht unterstuetzt.
    int keinSet_ = 0;
    std::string abdeckungHinweisFuer_;
    std::string AbdeckungText(bool lang) const;
    void ZeichneAbdeckung(float breite);

    std::deque<std::string> log_;
    bool logAmEnde_ = true;

    std::deque<Dialog> dialoge_;
    int dialogNummer_ = 0;
    bool katzeAusOptionen_ = false;

    bilder::Textur logo_;

    std::mutex badgeMutex_;
    std::map<std::string, Badge> badges_;
    std::deque<std::string> badgeWarteschlange_;
    std::condition_variable badgeCv_;
    std::thread badgeThread_;
    bool badgeEnde_ = false;
    struct BadgeErgebnis { std::string name; bool ok; std::string png; int w, h; std::vector<uint8_t> rgba; };
    std::vector<BadgeErgebnis> badgeFertig_;

    std::vector<std::thread> hilfsThreads_;
};
