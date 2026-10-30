#include "worker.h"

#include "Md5.h"
#include "i18n.h"
#include "snes_kopf.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cstdio>

namespace {

constexpr auto kSuchAbstand = std::chrono::seconds(2);
constexpr long long kTaktMs = 16;            // ca. 60 Abfragen je Sekunde
constexpr long long kInfoAbstandMs = 2000;   // Spielwechsel/Savestates pruefen
constexpr long long kPingIntervalMs = 120000;

std::string Upper(std::string s) {
    for (char& c : s) c = (char)::toupper((unsigned char)c);
    return s;
}

long long MsSeit(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t).count();
}

std::string RaAdr(uint32_t a) { return "$" + Upper(i18n::Hex(a, 6)); }

// Dateiname ohne Ordner, fuer Meldungen.
std::string NurName(const std::string& pfad) {
    const size_t p = pfad.find_last_of("/\\");
    return p == std::string::npos ? pfad : pfad.substr(p + 1);
}

bool WahrText(std::string s) {
    for (char& c : s) c = (char)::tolower((unsigned char)c);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s == "true" || s == "yes" || s == "on" || s == "1";
}

}  // namespace

MonitorWorker::MonitorWorker(std::shared_ptr<RaClient> client, Sucher sucher)
    : client_(std::move(client)), sucher_(std::move(sucher)) {
    thread_ = std::thread([this]() { run(); });
}

MonitorWorker::~MonitorWorker() {
    stop();
    {
        std::lock_guard<std::mutex> l(queueMutex_);
        quit_ = true;
        queue_.clear();
    }
    queueCv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void MonitorWorker::post(std::function<void()> auftrag) {
    {
        std::lock_guard<std::mutex> l(queueMutex_);
        queue_.push_back(std::move(auftrag));
    }
    queueCv_.notify_one();
}

void MonitorWorker::run() {
    // Feinere Zeitscheiben fuer Sleep(): sonst schlaeft jede Pause mindestens
    // 15,6 ms und der Takt sinkt auf rund 30 Abfragen je Sekunde.
    ::timeBeginPeriod(1);
    for (;;) {
        std::function<void()> auftrag;
        {
            std::unique_lock<std::mutex> l(queueMutex_);
            queueCv_.wait_for(l, std::chrono::milliseconds(1000),
                              [this]() { return quit_ || !queue_.empty() || !diagnoseQueue_.empty(); });
            if (quit_) break;
            if (!queue_.empty()) {
                auftrag = std::move(queue_.front());
                queue_.pop_front();
            }
        }
        runPendingDiagnose();
        try {
            if (auftrag) auftrag();
            else leerlauf();
        } catch (const std::exception& e) {
            emitLog(std::string(T("Fehler: ")) + e.what());
        }
    }
    if (fx_) fx_->Schliessen();
    ::timeEndPeriod(1);
}

void MonitorWorker::runPendingDiagnose() {
    std::deque<long long> ids;
    {
        std::lock_guard<std::mutex> l(queueMutex_);
        ids.swap(diagnoseQueue_);
    }
    for (long long id : ids) doDiagnose(id);
}

// ================= Einstellungen und Auftraege =================

void MonitorWorker::setAnmeldung(const std::string& user, const std::string& token) {
    std::lock_guard<std::mutex> l(einstMutex_);
    user_ = user;
    token_ = token;
}

std::string MonitorWorker::anmeldungUser() {
    std::lock_guard<std::mutex> l(einstMutex_);
    return user_;
}

std::string MonitorWorker::anmeldungToken() {
    std::lock_guard<std::mutex> l(einstMutex_);
    return token_;
}

void MonitorWorker::setAutostart(bool an) { autostart_ = an; }

void MonitorWorker::setDiagnoseDatei(const std::string& pfad) {
    std::lock_guard<std::mutex> l(einstMutex_);
    diagPfadWunsch_ = pfad;
}
void MonitorWorker::setHardcore(bool an) { hardcoreWunsch_ = an; }

void MonitorWorker::seedHashes(const std::map<std::string, std::string>& hashes) {
    post([this, hashes]() {
        for (const auto& kv : hashes) hashes_[kv.first] = kv.second;
    });
}

void MonitorWorker::startMonitoring() {
    post([this]() { doStartMonitoring(false); });
}

void MonitorWorker::continueWithPath(const std::string& path) {
    post([this, path]() { doContinueWithPath(path); });
}

void MonitorWorker::diagnoseAchievement(long long achievementId) {
    {
        std::lock_guard<std::mutex> l(queueMutex_);
        diagnoseQueue_.push_back(achievementId);
    }
    queueCv_.notify_one();
}

void MonitorWorker::stop() { running_ = false; }

// ================= Verbindung und Leerlauf =================

bool MonitorWorker::verbinden() {
    std::string bericht;
    auto port = sucher_ ? sucher_() : FindeFxPak(std::string(), &bericht);
    if (!port) {
        // Nur melden, wenn sich das Bild aendert -- die Suche laeuft alle 2 s.
        if (!bericht.empty() && bericht != letzterSuchbericht_) emitLog(std::string(T("SD2SNES/FXPak-Suche: ")) + bericht);
        letzterSuchbericht_ = bericht;
        return false;
    }
    letzterSuchbericht_.clear();
    fehlversuche_ = 0;
    fx_ = std::move(port);
    try {
        info_ = fx_->Info();
    } catch (const std::exception& e) {
        fx_.reset();
        emitLog(std::string(T("SD2SNES/FXPak antwortet nicht: ")) + e.what());
        return false;
    }
    romBekannt_ = false;
    emitLog(Fmt(T("Verbunden mit %1 auf %2 (Firmware %3)."),
                {info_.geraet.empty() ? std::string("SD2SNES/FXPak") : info_.geraet, fx_->Portname(), info_.version}));
    if (connected) connected(fx_->Portname(), info_);
    return true;
}

void MonitorWorker::trennen(const std::string& grund) {
    fx_.reset();
    romBekannt_ = false;
    laufendeRom_.clear();
    naechsteSuche_ = Clock::now() + kSuchAbstand;
    if (disconnected) disconnected(grund);
}

void MonitorWorker::leerlauf() {
    if (!fx_) {
        if (Clock::now() < naechsteSuche_) return;
        naechsteSuche_ = Clock::now() + kSuchAbstand;
        if (!verbinden()) return;
    }
    // Im Leerlauf genuegt ein Blick alle 2 s -- jede Anfrage stoert das Menue.
    if (Clock::now() < naechsteInfo_) return;
    naechsteInfo_ = Clock::now() + std::chrono::seconds(2);
    try {
        info_ = fx_->Info();
        fehlversuche_ = 0;
        beschaeftigtGemeldet_ = false;
    } catch (const std::exception& e) {
        // Antwort steht noch aus: abwarten, nichts senden, kein Fehler.
        if (FxPakPort::Beschaeftigt(e)) {
            if (!beschaeftigtGemeldet_) emitLog(T("SD2SNES/FXPak ist beschaeftigt -- warte auf seine Antwort."));
            beschaeftigtGemeldet_ = true;
            return;
        }
        // Einzelne Aussetzer sind kein Verbindungsverlust; erst nach dem
        // dritten in Folge aufgeben.
        // Beim Spielwechsel nimmt das Modul kurz nichts an; die Versuche werden
        // deshalb nicht einzeln gemeldet.
        ++fehlversuche_;
        if (fehlversuche_ >= 3) {
            emitLog(std::string(T("Verbindung zum SD2SNES/FXPak unterbrochen (z. B. beim Spielwechsel) -- verbinde neu: ")) + e.what());
            trennen(e.what());
        }
        return;
    }

    const std::string rom = fxpak::IstMenue(info_.rom) ? std::string() : info_.rom;
    if (!romBekannt_ || rom != laufendeRom_) {
        laufendeRom_ = rom;
        romBekannt_ = true;
        if (rom != autoGesperrtFuer_) autoGesperrtFuer_.clear();
        if (romChanged) romChanged(rom);
        if (rom.empty()) emitLog(T("SD2SNES/FXPak: Menue."));
        else emitLog(std::string(T("SD2SNES/FXPak: laufende ROM ")) + rom);
    }

    if (autostart_ && !rom.empty() && fxpak::IstSnesRom(rom) && rom != autoGesperrtFuer_ &&
        !anmeldungToken().empty()) {
        emitLog(T("Spiel erkannt -- Ueberwachung startet automatisch."));
        doStartMonitoring(true);
    }
}

// ================= Spielerkennung =================

std::optional<std::string> MonitorWorker::ermittleHash(const std::string& romPfad) {
    // Schluessel fuer den Zwischenspeicher: Pfad plus Pruefsumme aus dem
    // ROM-Header -- eine ausgetauschte Datei unter gleichem Namen faellt so auf.
    snes::Kopf kopf;
    {
        std::vector<std::vector<uint8_t>> kandidaten;
        for (uint32_t lage : snes::kLagen) {
            try { kandidaten.push_back(fx_->Lesen(fxpak::kRom + lage, snes::kKopfLaenge)); }
            catch (const std::exception&) { kandidaten.emplace_back(); }
        }
        kopf = snes::Waehle(kandidaten);
    }
    kopf_ = kopf;
    chipUntertyp_ = 0;
    // Eigene Chips (Cx4, SPC7110, ST01x) nennen ihren Untertyp im Byte vor dem Header.
    if (kopf.gefunden && (kopf.chip >> 4) == 0xF && kopf.lage > 0) {
        try { chipUntertyp_ = fx_->Lesen(fxpak::kRom + kopf.lage - 1, 1).at(0); }
        catch (const std::exception&) {}
    }
    std::string schluessel = romPfad;
    if (kopf.gefunden) {
        schluessel += "|" + i18n::Hex(kopf.pruefsumme, 4) + i18n::Hex(kopf.komplement, 4);
        emitLog(Fmt(T("ROM-Header: \"%1\", %2, %3 KiB"),
                    {kopf.titel, snes::Name(kopf.abbildung), i18n::Zahl(kopf.romBytes / 1024)}));
    }
    auto it = hashes_.find(schluessel);
    if (it != hashes_.end()) {
        emitLog(std::string(T("Hash bekannt: ")) + it->second);
        return it->second;
    }

    // 1. Die Datei selbst von der SD-Karte -- ergibt genau den Hash, den RA kennt.
    try {
        emitLog(Fmt(T("Lese %1 von der SD-Karte..."), {NurName(romPfad)}));
        const Bytes datei = fx_->DateiLesen(romPfad, [this](uint32_t g, uint32_t n) {
            if (identifyProgress) identifyProgress(g, n);
        });
        const std::string md5 = RaClient::snes_hash(datei);
        emitLog(Fmt(T("Hash: %1 (%2 KiB)"), {md5, i18n::Zahl((long long)datei.size() / 1024)}));
        hashes_[schluessel] = md5;
        if (hashLearned) hashLearned(schluessel, md5);
        return md5;
    } catch (const std::exception& e) {
        emitLog(std::string(T("Datei nicht lesbar: ")) + e.what());
    }

    // 2. Ersatzweise das ROM-Abbild im Modul, in der Groesse laut Header.
    // Stimmt nur, wenn die Datei genau diese Groesse hat -- deshalb nicht
    // im Zwischenspeicher abgelegt.
    if (kopf.gefunden && kopf.romBytes > 0 && kopf.romBytes <= 8u * 1024 * 1024) {
        try {
            emitLog(T("Nehme ersatzweise das ROM-Abbild im Modul."));
            Bytes rom;
            rom.reserve(kopf.romBytes);
            constexpr uint32_t kStueck = 0x10000;
            for (uint32_t o = 0; o < kopf.romBytes; o += kStueck) {
                const Bytes s = fx_->Lesen(fxpak::kRom + o, std::min(kStueck, kopf.romBytes - o));
                rom.insert(rom.end(), s.begin(), s.end());
                if (identifyProgress) identifyProgress((uint32_t)rom.size(), kopf.romBytes);
            }
            const std::string md5 = rawsnes::md5Hex(rom);
            emitLog(Fmt(T("Hash (ROM-Abbild): %1"), {md5}));
            return md5;
        } catch (const std::exception& e) {
            emitLog(std::string(T("ROM-Abbild nicht lesbar: ")) + e.what());
        }
    }
    return std::nullopt;
}

void MonitorWorker::doStartMonitoring(bool automatisch) {
    if (!fx_ && !verbinden()) {
        emitLog(T("Kein SD2SNES/FXPak gefunden -- eingesteckt und Konsole an?"));
        if (finished) finished();
        return;
    }
    user_lauf_ = anmeldungUser();
    token_lauf_ = anmeldungToken();
    if (token_lauf_.empty()) {
        emitLog(T("Bitte zuerst anmelden."));
        if (finished) finished();
        return;
    }
    try {
        info_ = fx_->Info();
    } catch (const std::exception& e) {
        trennen(e.what());
        emitLog(std::string(T("Verbindung zum SD2SNES/FXPak verloren: ")) + e.what());
        if (finished) finished();
        return;
    }
    const std::string rom = fxpak::IstMenue(info_.rom) ? std::string() : info_.rom;
    if (rom.empty() || !fxpak::IstSnesRom(rom)) {
        emitLog(T("Am SD2SNES/FXPak laeuft gerade kein SNES-Spiel -- erst ein Spiel starten."));
        if (finished) finished();
        return;
    }
    // Endet die Ueberwachung (Stopp, kein Set, Fehler), startet der Autostart
    // fuer genau diese ROM erst nach einem Wechsel wieder.
    autoGesperrtFuer_ = rom;
    laufendeRom_ = rom;
    romBekannt_ = true;
    hardcore_ = hardcoreWunsch_.load();
    running_ = true;
    if (monitoringStarted) monitoringStarted();
    (void)automatisch;

    const auto md5 = ermittleHash(rom);
    if (!md5) {
        running_ = false;
        if (romNotDetected) romNotDetected();
        return;
    }
    if (!running_) {  // waehrend des Lesens gestoppt
        if (finished) finished();
        return;
    }
    auto [gameOpt, err] = client_->load_by_md5(*md5, user_lauf_, token_lauf_);
    if (!gameOpt) {
        emitLog(std::string(T("Fehler: ")) + err);
        running_ = false;
        if (finished) finished();
        return;
    }
    ladeUndUeberwache(*gameOpt);
}

void MonitorWorker::doContinueWithPath(const std::string& path) {
    if (!fx_) {
        emitLog(T("Kein SD2SNES/FXPak verbunden."));
        if (finished) finished();
        return;
    }
    user_lauf_ = anmeldungUser();
    token_lauf_ = anmeldungToken();
    hardcore_ = hardcoreWunsch_.load();
    running_ = true;
    auto [gameOpt, err] = client_->identify_and_load(path, user_lauf_, token_lauf_);
    if (!gameOpt) {
        emitLog(std::string(T("Fehler: ")) + err);
        running_ = false;
        if (finished) finished();
        return;
    }
    ladeUndUeberwache(*gameOpt);
}

// ================= Laden + Abfrageschleife =================

void MonitorWorker::ladeUndUeberwache(GameData game) {
    game_ = std::move(game);
    if (gameLoaded) gameLoaded(game_.name);

    if (game_.no_set || game_.achievements.empty()) {
        // RetroAchievements fuehrt nicht gepruefte Fassungen eines Spiels unter diesem Namen.
        const bool fassung = game_.name.rfind("Unsupported Game Version", 0) == 0;
        if (keinSet) keinSet(fassung);
        if (fassung)
            emitLog(game_.name + T(": Diese Fassung des Spiels unterstuetzt RetroAchievements nicht. Welche Fassungen "
                                   "passen, steht auf der Spielseite bei RetroAchievements (Supported Game Hashes)."));
        else
            emitLog(game_.name + T(": kein Achievement-Set."));
        running_ = false;
        if (finished) finished();
        return;
    }

    tracked_.clear();
    for (const auto& ac : game_.achievements) {
        TrackedAchievement t;
        t.info = ac;
        t.rt = std::make_unique<rawsnes::AchievementRuntime>(ac.mem);
        std::vector<std::pair<std::string, std::string>> single{{std::to_string(ac.id), ac.mem}};
        for (auto a : rawsnes::collectAddresses(single)) t.needed.insert(a);
        tracked_.push_back(std::move(t));
    }
    leaderboards_.clear();
    for (const auto& lb : game_.leaderboards) {
        leaderboards_.emplace_back(lb.id, lb.title, lb.format, lb.startMem, lb.cancelMem, lb.submitMem,
                                   lb.valueMem);
    }
    if (!leaderboards_.empty()) {
        int unsupportedCount = 0;
        for (auto& lb : leaderboards_) if (lb.unsupported()) ++unsupportedCount;
        emitLog(Fmt(T("Leaderboards: %1 geladen (%2 nicht auswertbar)."),
                    {i18n::Zahl((long long)leaderboards_.size()), i18n::Zahl(unsupportedCount)}));
    }
    {
        LeaderboardList lbList;
        for (const auto& lb : game_.leaderboards) lbList.emplace_back(lb.id, lb.title);
        if (leaderboardsReady) leaderboardsReady(lbList);
    }

    // Beobachtet werden die Adressen der Achievements; gelesen zusaetzlich
    // die der Leaderboards.
    std::vector<std::pair<std::string, std::string>> idAndMem;
    for (const auto& ac : game_.achievements) idAndMem.emplace_back(std::to_string(ac.id), ac.mem);
    watchedAddrs_.clear();
    for (auto a : rawsnes::collectAddresses(idAndMem)) watchedAddrs_.push_back(a);
    changeCounts_.assign(watchedAddrs_.size(), 0);

    std::vector<std::pair<std::string, std::string>> alles = idAndMem;
    for (const auto& lb : game_.leaderboards) {
        for (const std::string* m : {&lb.startMem, &lb.cancelMem, &lb.submitMem, &lb.valueMem})
            alles.emplace_back("lb" + std::to_string(lb.id), *m);
    }
    const std::vector<uint32_t> alleAdressen = rawsnes::collectAddresses(alles);
    plan_ = snes::Leseplan(alleAdressen);
    if (liveValuesInit) liveValuesInit(watchedAddrs_);

    std::set<long long> validIds;
    for (const auto& a : game_.achievements) validIds.insert(a.id);

    checkHardcoreAtStart();
    auto [ids, info] = client_->ra_unlocks(game_.gameid, user_lauf_, token_lauf_, validIds, hardcore_);
    (void)info;
    alreadyUnlocked_ = ids;
    for (auto& t : tracked_) t.unlocked = alreadyUnlocked_.count(t.info.id) > 0;

    // Lesemodus: Standardkern mit WRAM-Kopie, oder Zusatzchip mit Werte-Kopie.
    kopieModus_ = zusatzchip() != nullptr;
    kopieAktiv_ = false;
    abdeckungGemeldet_ = false;
    // Adressen je Leaderboard (fuer die Werte-Kopie und die Abdeckung).
    kopieLb_.clear();
    for (const auto& lb : game_.leaderboards) {
        std::vector<std::pair<std::string, std::string>> m;
        for (const std::string* s : {&lb.startMem, &lb.cancelMem, &lb.submitMem, &lb.valueMem})
            m.emplace_back("lb", *s);
        kopieLb_.push_back(rawsnes::collectAddresses(m));
    }
    if (kopieModus_) {
        emitLog(Fmt(T("Spiel mit Zusatzchip (%1): Das SD2SNES/FXPak fuehrt dafuer keine WRAM-Kopie. "
                      "Die Werte kopiert die Konsole selbst, einmal je Bild ueber den NMI-Hook."),
                    {zusatzchip()}));
        // Modul-RAM (SRAM/BW-RAM) weiter direkt ueber USB; in die Routine
        // kommen nur WRAM und I-RAM, Achievement fuer Achievement.
        std::vector<uint32_t> sram;
        for (uint32_t a : alleAdressen)
            if (!nmikopie::KonsoleLiest(a)) sram.push_back(a);
        sramPlan_ = snes::Leseplan(sram);
        kopieZiele_.clear();
        neueZiele_.clear();
        kopieNeuBauen_ = false;
        // Bei Super FX laeuft nur das IRQ durch den Hook (am Geraet gemessen):
        // Bildtakt dort ueber die Austastluecke.
        // Welcher Interrupt durch den Hook laeuft, haengt vom Spiel ab, nicht
        // vom Chip (am Geraet gemessen): SA-1, Cx4 und Street Fighter Alpha 2
        // den NMI, Star Fox nur IRQs (mehrere je Bild), Star Ocean nur einen
        // IRQ je Bild, und davor vertraegt es kaum Verzoegerung. Eine kurze
        // Sonde misst das bei jedem Spiel und waehlt den Takt.
        kopieTakt_ = kopieSonde();
        if (kopieTakt_ == nmikopie::Takt::Austastung)
            emitLog(T("Bildtakt ueber die Austastluecke: Bei diesem Spiel laufen nur IRQs durch den Hook."));
        if (kopieTakt_ == nmikopie::Takt::Nachlauf)
            emitLog(T("S-DD1: Die Werte werden erst nach dem IRQ des Spiels kopiert."));
        bilderGesamt_ = 0;
        verpasst_ = 0;
        stapelGesamt_.clear();
        spRuhe_ = 0;
        bilderFenster_ = verpasstFenster_ = lesungenFenster_ = verspaetetFenster_ = 0;
        lesezeitFenster_ = 0;
        diagnoseSeit_ = Clock::time_point{};
        if (!kopieEinrichten(true)) {
            emitLog(T("Ohne Werte-Kopie kann dieses Spiel nicht ueberwacht werden."));
            running_ = false;
            if (finished) finished();
            return;
        }
    } else {
        emitLog(Fmt(T("%1 Adressen werden ueberwacht (%2 Abfrage(n) je Durchgang)."),
                    {i18n::Zahl((long long)plan_.AnzahlAdressen()), i18n::Zahl((long long)plan_.Lesungen().size())}));
        abdeckungPruefen();
    }

    // Ohne Platz in der Werte-Kopie zaehlt nicht als "nicht auswertbar" --
    // das meldet kopieEinrichten, die ruecken spaeter nach.
    int nichtAuswertbar = 0;
    for (auto& t : tracked_) {
        t.lesbar = std::all_of(t.needed.begin(), t.needed.end(), [this](uint32_t a) { return lesbar(a); });
        const bool nurPlatz = kopieModus_ && std::all_of(t.needed.begin(), t.needed.end(), [](uint32_t a) {
            return nmikopie::KonsoleLiest(a) || snes::RaLesbar(a);
        });
        if (!t.unlocked && (t.rt->unsupported() || (!t.lesbar && !nurPlatz))) ++nichtAuswertbar;
    }
    if (nichtAuswertbar)
        emitLog(Fmt(T("%1 Achievement(s) nicht auswertbar (Adressen hier nicht lesbar oder unbekannte Syntax)."),
                    {i18n::Zahl(nichtAuswertbar)}));
    emitLog(Fmt(T("%1 Achievements geladen (%2 bereits freigeschaltet)."),
                {i18n::Zahl((long long)tracked_.size()), i18n::Zahl((long long)alreadyUnlocked_.size())}));
    if (achievementsReady) achievementsReady(game_, alreadyUnlocked_);

    havePrevRam_ = false;
    confirmedAddrs_.clear();
    // Die Werte-Kopie liest den echten WRAM -- keine Altwerte, kein Warmlauf.
    warmedUp_ = kopieModus_;
    haveWarmSince_ = false;
    warned3s_ = false;
    ersteAenderungGemeldet_ = false;
    durchgaenge_ = 0;
    taktGemeldet_ = false;
    waitingIds_.clear();
    sessionStart_ = Clock::now();
    runPollLoop();
}

// ================= Hardcore =================

std::optional<std::vector<std::string>> MonitorWorker::hardcoreViolations() {
    if (!fx_) return std::nullopt;
    std::vector<std::string> v;
    try {
        info_ = fx_->Info();
        if (info_.einstellungen & fxpak::kEinstSavestates) v.push_back(T("Savestates"));
        const auto cheats = fx_->KonfigLesen("EnableCheats");
        if (!cheats) return std::nullopt;  // nicht pruefbar -> nicht Hardcore
        if (WahrText(*cheats)) v.push_back(T("Cheats"));
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return v;
}

void MonitorWorker::reportViolations(const std::vector<std::string>& v, bool midSession) {
    std::string liste;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) liste += " + ";
        liste += v[i];
    }
    hardcore_ = false;
    if (midSession) {
        emitLog(std::string(T("ACHTUNG: ")) + liste +
                T(" am SD2SNES/FXPak aktiviert -- Hardcore wird fuer den Rest der Sitzung beendet, ab jetzt Softcore."));
    } else {
        emitLog(std::string(T("ACHTUNG: ")) + liste +
                T(" am SD2SNES/FXPak aktiviert -- nur Softcore verfuegbar. Fuer Hardcore im SD2SNES/FXPak-Menue "
                  "deaktivieren, dann Monitor neu starten."));
    }
    if (hardcoreDowngraded) hardcoreDowngraded(liste);
}

void MonitorWorker::checkHardcoreAtStart() {
    if (!hardcore_) return;
    auto v = hardcoreViolations();
    if (!v) {
        hardcore_ = false;
        emitLog(T("Hardcore-Pruefung nicht moeglich (Einstellungen des SD2SNES/FXPak nicht lesbar) -- nur Softcore."));
        if (hardcoreDowngraded) hardcoreDowngraded(T("Einstellungen nicht lesbar"));
        return;
    }
    if (v->empty()) {
        emitLog(T("Hardcore-Voraussetzung erfuellt: Savestates und Cheats sind am SD2SNES/FXPak aus."));
        return;
    }
    reportViolations(*v, false);
}

// ================= Diagnose =================

void MonitorWorker::doDiagnose(long long achievementId) {
    TrackedAchievement* found = nullptr;
    for (auto& t : tracked_) if (t.info.id == achievementId) { found = &t; break; }
    if (!found) return;

    emitLog(Fmt(T("\n--- %1 (%2P) ---"), {found->info.title, i18n::Zahl(found->info.points)}));
    emitLog(std::string(T("MemAddr: ")) + found->info.mem);

    std::string nichtLesbar;
    for (auto a : found->needed) {
        if (lesbar(a)) continue;
        if (!nichtLesbar.empty()) nichtLesbar += T(", ");
        nichtLesbar += RaAdr(a);
    }
    if (!nichtLesbar.empty()) emitLog(std::string(T("am SD2SNES/FXPak nicht lesbare Adressen: ")) + nichtLesbar);
    if (found->rt->unsupported()) {
        emitLog(T("Bedingung wird nicht unterstuetzt -- feuert nie."));
        return;
    }
    if (!havePrevRam_) {
        emitLog(T("noch keine Messwerte -- Monitor starten."));
        return;
    }
    std::string werte;
    for (auto a : found->needed) {
        auto it = prevRam_.find(a);
        if (!werte.empty()) werte += T(", ");
        werte += RaAdr(a) + "=" + (it != prevRam_.end() ? std::to_string(it->second) : std::string("?"));
    }
    emitLog(std::string(T("aktuelle Werte: ")) + werte);
    const bool frei = warmedUp_ || std::includes(confirmedAddrs_.begin(), confirmedAddrs_.end(),
                                                 found->needed.begin(), found->needed.end());
    emitLog(frei ? T("Adressen bestaetigt -- wird ausgewertet.")
                 : T("wartet noch auf bestaetigte Adressen (Schutz gegen Fehlbuchungen)."));
}

// ================= Werte-Kopie ueber den NMI-Hook =================

const char* MonitorWorker::zusatzchip() const {
    if (!kopf_.gefunden || (kopf_.chip & 0x0F) < 3) return nullptr;   // kein Koprozessor
    switch (kopf_.chip >> 4) {
        case 0x1: return "Super FX";
        case 0x2: return "OBC1";
        case 0x3: return "SA-1";
        case 0x4: return "S-DD1";
        case 0xF: return chipUntertyp_ == 0x10 ? "Cx4" : nullptr;
        default: return nullptr;   // DSP u. a.: Kern mit WRAM-Kopie
    }
}

bool MonitorWorker::lesbar(uint32_t ra) const {
    if (!kopieModus_) return snes::RaLesbar(ra);
    if (nmikopie::KonsoleLiest(ra)) return kopie_.lage.count(ra) > 0 || !kopieAktiv_;
    return snes::RaLesbar(ra);
}

void MonitorWorker::abdeckungPruefen() {
    // Waeren die Adressen lesbar, wenn genug Platz in der Werte-Kopie waere?
    const auto nurPlatz = [this](const auto& adressen) {
        return kopieModus_ && std::all_of(adressen.begin(), adressen.end(), [](uint32_t a) {
                   return nmikopie::KonsoleLiest(a) || snes::RaLesbar(a);
               });
    };
    const auto alleLesbar = [this](const auto& adressen) {
        return std::all_of(adressen.begin(), adressen.end(), [this](uint32_t a) { return lesbar(a); });
    };
    Abdeckung a;
    for (const auto& t : tracked_) {
        if (t.unlocked) continue;
        ++a.offen;
        if (t.rt->unsupported()) ++a.nichtAuswertbar;
        else if (alleLesbar(t.needed)) ++a.aktiv;
        else if (nurPlatz(t.needed)) { ++a.ohnePlatz; a.ohnePlatzIds.insert(t.info.id); }
        else ++a.nichtAuswertbar;
    }
    for (size_t i = 0; i < leaderboards_.size(); ++i) {
        ++a.lbGesamt;
        const std::vector<uint32_t> leer;
        const auto& adressen = i < kopieLb_.size() ? kopieLb_[i] : leer;
        if (leaderboards_[i].unsupported()) ++a.lbNichtAuswertbar;
        else if (alleLesbar(adressen)) ++a.lbAktiv;
        else if (nurPlatz(adressen)) ++a.lbOhnePlatz;
        else ++a.lbNichtAuswertbar;
    }
    if (abdeckungGemeldet_ && a == abdeckung_) return;
    abdeckungGemeldet_ = true;
    abdeckung_ = a;
    if (!a.voll())
        emitLog(Fmt(T("Nur teilweise unterstuetzt: %1 von %2 offenen Achievements werden ueberwacht."),
                    {i18n::Zahl(a.aktiv), i18n::Zahl(a.offen)}));
    if (abdeckungGeaendert) abdeckungGeaendert(a);
}

// Je offenes Achievement eine Gruppe (nur ganz nuetzt sie etwas), dazu die
// Zeigerziele vorneweg und die Leaderboards zuletzt.
std::vector<nmikopie::Gruppe> MonitorWorker::kopieGruppen() const {
    std::vector<nmikopie::Gruppe> g;
    if (!kopieZiele_.empty()) g.push_back({0, std::vector<uint32_t>(kopieZiele_.begin(), kopieZiele_.end())});
    for (const auto& t : tracked_)
        if (!t.unlocked && !t.rt->unsupported())
            g.push_back({1, std::vector<uint32_t>(t.needed.begin(), t.needed.end())});
    for (const auto& lb : kopieLb_) g.push_back({2, lb});
    return g;
}

void MonitorWorker::kopieAktualisiereLesbar() {
    for (auto& t : tracked_)
        t.lesbar = std::all_of(t.needed.begin(), t.needed.end(), [this](uint32_t a) { return lesbar(a); });
}

// Haengt fuer kurze Zeit die Sonde ein (zaehlt Durchlaeufe und NMIs) und
// waehlt daraus den Takt: NMI dabei -> Nmi; nur IRQs, deutlich mehr als einer
// je Bild -> Austastung; nur IRQs, etwa einer je Bild -> Nachlauf.
nmikopie::Takt MonitorWorker::kopieSonde() {
    using fxpak::Raum;
    try {
        const nmikopie::Zustand z =
            nmikopie::Pruefe(fx_->LesenRaum(Raum::Befehlspuffer, nmikopie::kPuffer, nmikopie::kPufferGroesse));
        if (z == nmikopie::Zustand::Eingehaengt) {
            for (const auto& s : nmikopie::Aushaengen()) fx_->Schreiben(Raum::Befehlspuffer, s.adresse, {s.wert});
            ::Sleep(50);
        } else if (z != nmikopie::Zustand::Original) {
            return nmikopie::Takt::Nachlauf;   // kopieEinrichten meldet den Grund
        }
        fx_->Schreiben(Raum::Befehlspuffer, nmikopie::kSprung, {0x4C, (uint8_t)(nmikopie::kBlock & 0xFF),
                                                               (uint8_t)(nmikopie::kBlock >> 8)});
        fx_->Schreiben(Raum::Befehlspuffer, nmikopie::kBlock, nmikopie::Sonde());
        for (const auto& s : nmikopie::Einhaengen()) fx_->Schreiben(Raum::Befehlspuffer, s.adresse, {s.wert});
        // Erst entscheiden, wenn in einem Fenster von 0,4 s genug Durchlaeufe
        // kamen: Beim Spielstart oder Laden laeuft der Hook oft eine Weile gar
        // nicht (am Geraet: Street Fighter Alpha 2).
        int alle = 0, nmi = 0;
        uint8_t altAlle = 0, altNmi = 0;
        bool gemeldet = false;
        const Clock::time_point beginn = Clock::now();
        while (running_ && MsSeit(beginn) < 60000) {
            ::Sleep(400);
            const Bytes r = fx_->LesenRaum(Raum::Befehlspuffer, nmikopie::kSondeAlle, 2);
            if (r.size() < 2) continue;
            alle = (uint8_t)(r[0] - altAlle);
            nmi = (uint8_t)(r[1] - altNmi);
            altAlle = r[0];
            altNmi = r[1];
            if (alle >= 8) break;
            if (!gemeldet && MsSeit(beginn) > 3000) {
                gemeldet = true;
                emitLog(T("Sonde: Der Hook laeuft noch nicht (Spiel laedt, oder In-Game-Hook aus?) -- warte."));
            }
        }
        for (const auto& s : nmikopie::Aushaengen()) fx_->Schreiben(Raum::Befehlspuffer, s.adresse, {s.wert});
        ::Sleep(50);
        if (alle < 8) {
            // Kein Ergebnis: die Kopie im NMI; laeuft der Hook spaeter nur ueber
            // IRQs, misst leseKopie neu.
            emitLog(T("Sonde: kein Ergebnis -- Kopie im NMI."));
            return nmikopie::Takt::Nmi;
        }
        // 0,4 s sind 20 (PAL) bzw. 24 (NTSC) Bilder; ab 36 Durchlaeufen sind
        // es sicher mehrere je Bild.
        const nmikopie::Takt t = nmi > 0      ? nmikopie::Takt::Nmi
                                 : alle >= 36 ? nmikopie::Takt::Austastung
                                              : nmikopie::Takt::Nachlauf;
        emitLog(Fmt(T("Sonde: %1 Durchlaeufe in 0,4 s, davon %2 ueber den NMI -- %3."),
                    {i18n::Zahl(alle), i18n::Zahl(nmi),
                     t == nmikopie::Takt::Nmi          ? T("Kopie im NMI")
                     : t == nmikopie::Takt::Austastung ? T("Kopie in der Austastluecke")
                                                       : T("Kopie nach dem IRQ des Spiels")}));
        return t;
    } catch (const std::exception& e) {
        emitLog(std::string(T("Werte-Kopie: ")) + e.what());
        return nmikopie::Takt::Nachlauf;
    }
}

bool MonitorWorker::kopieEinrichten(bool erstesMal) {
    using fxpak::Raum;
    try {
        // Ob der In-Game-Hook eingeschaltet ist, laesst sich nicht verlaesslich
        // abfragen (Firmware 1.11.0 meldet es nicht in der Geraeteinfo).
        // Nachweis ist allein, dass Bilder ankommen -- siehe leseKopie.
        (void)erstesMal;
        const Bytes puffer = fx_->LesenRaum(Raum::Befehlspuffer, nmikopie::kPuffer, nmikopie::kPufferGroesse);
        switch (nmikopie::Pruefe(puffer)) {
            case nmikopie::Zustand::Unbekannt:
                emitLog(T("NMI-Hook der Firmware nicht erkannt (andere Firmware-Version?) -- Werte-Kopie nicht moeglich."));
                return false;
            case nmikopie::Zustand::ZuVieleCheats:
                emitLog(T("WRAM-Cheats aktiv -- die Werte-Kopie braucht deren Platz. Cheats fuer dieses Spiel abschalten."));
                return false;
            case nmikopie::Zustand::Eingehaengt:
                // Rest einer frueheren Sitzung: erst aushaengen, dann einen
                // Moment warten, bis kein NMI mehr in der alten Routine steckt.
                for (const auto& s : nmikopie::Aushaengen()) fx_->Schreiben(Raum::Befehlspuffer, s.adresse, {s.wert});
                ::Sleep(50);
                break;
            case nmikopie::Zustand::Original:
                break;
        }
        kopieAktiv_ = false;
        kopie_ = nmikopie::ErzeugeGruppen(kopieGruppen(), nullptr, kopieTakt_);
        fx_->Schreiben(Raum::Befehlspuffer, nmikopie::kSprung, kopie_.sprung);
        fx_->Schreiben(Raum::Befehlspuffer, nmikopie::kBlock, kopie_.block);
        const Bytes zurueck = fx_->LesenRaum(Raum::Befehlspuffer, nmikopie::kPuffer, nmikopie::kPufferGroesse);
        const auto gleich = [&](uint32_t adresse, const std::vector<uint8_t>& soll) {
            const size_t o = adresse - nmikopie::kPuffer;
            return zurueck.size() >= o + soll.size() && std::equal(soll.begin(), soll.end(), zurueck.begin() + (long)o);
        };
        if (!gleich(nmikopie::kSprung, kopie_.sprung) || !gleich(nmikopie::kBlock, kopie_.block)) {
            emitLog(T("Werte-Kopie: Schreiben in den Befehlspuffer fehlgeschlagen (Rueckleseprobe)."));
            return false;
        }
        for (const auto& s : nmikopie::Einhaengen()) fx_->Schreiben(Raum::Befehlspuffer, s.adresse, {s.wert});
        const Bytes ende = fx_->LesenRaum(Raum::Befehlspuffer, nmikopie::kAusgang, 2);
        if (ende.size() < 2 || ende[0] != nmikopie::kBefehlNeu || ende[1] != nmikopie::kOperandNeu) {
            emitLog(T("Werte-Kopie: Einhaengen in den NMI-Hook fehlgeschlagen (Rueckleseprobe)."));
            for (const auto& s : nmikopie::Aushaengen()) fx_->Schreiben(Raum::Befehlspuffer, s.adresse, {s.wert});
            return false;
        }
        kopieAktiv_ = true;
        habeZaehler_ = false;
        habeLaeufe_ = false;
        laeufeSeitEinrichten_ = 0;
        stilleGemeldet_ = false;
        letztesBild_ = Clock::now();
        kopieAktualisiereLesbar();
        int offen = 0, drin = 0;
        for (const auto& t : tracked_) {
            if (t.unlocked || t.rt->unsupported()) continue;
            ++offen;
            if (t.lesbar) ++drin;
        }
        emitLog(Fmt(T("Werte-Kopie eingehaengt: %1 von %2 offenen Achievements, %3 Adressen, ca. %4 Takte je Bild."),
                    {i18n::Zahl(drin), i18n::Zahl(offen), i18n::Zahl((long long)kopie_.lage.size()),
                     i18n::Zahl(kopie_.takte)}));
        if (drin < offen)
            emitLog(Fmt(T("%1 Achievement(s) passen gerade nicht in den Befehlspuffer und werden nicht ueberwacht. "
                          "Sie kommen erst dazu, wenn durch Freischaltungen Platz frei wird; was vorher bei ihnen "
                          "erreicht wird, zaehlt nicht."),
                        {i18n::Zahl(offen - drin)}));
        abdeckungPruefen();
        return true;
    } catch (const std::exception& e) {
        emitLog(std::string(T("Werte-Kopie: ")) + e.what());
        return false;
    }
}

// ================= Diagnosedatei =================

void MonitorWorker::diagOeffnen() {
    std::string pfad;
    {
        std::lock_guard<std::mutex> l(einstMutex_);
        pfad = diagPfadWunsch_;
    }
    if (pfad.empty() || diag_) return;
    const auto breit = [](const std::string& s) {
        std::wstring w(s.size() + 1, L'\0');
        const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], (int)w.size());
        w.resize(n > 0 ? (size_t)n - 1 : 0);
        return w;
    };
    // Ab 5 MB wird die alte Datei beiseitegelegt.
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (::GetFileAttributesExW(breit(pfad).c_str(), GetFileExInfoStandard, &a) && (a.nFileSizeLow > 5000000 || a.nFileSizeHigh))
        ::MoveFileExW(breit(pfad).c_str(), breit(pfad + ".old").c_str(), MOVEFILE_REPLACE_EXISTING);
    diag_ = _wfopen(breit(pfad).c_str(), L"a");
    if (!diag_) return;
    diagStart_ = diagTakt_ = Clock::now();
    dBilder_ = dVerpasst_ = dVerspaetet_ = dAbfragen_ = dAenderungen_ = 0;
    dAbfrageMs_ = dAbfrageMax_ = 0;
    SYSTEMTIME t;
    ::GetLocalTime(&t);
    std::fprintf(diag_, "=== session %04d-%02d-%02d %02d:%02d:%02d | RAW-SNES %s (build %d) | %s %s | port %s\n",
                 t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, RAWSNES_VERSION, RAWSNES_BUILD,
                 info_.geraet.c_str(), info_.version.c_str(), fx_ ? fx_->Portname().c_str() : "-");
    std::fprintf(diag_, "game %s | gameid %lld | rom %s | chip %02X%s | mode %s | %zu addresses\n",
                 game_.name.c_str(), (long long)game_.gameid, laufendeRom_.c_str(), kopf_.chip,
                 zusatzchip() ? (std::string(" (") + zusatzchip() + ")").c_str() : "",
                 kopieModus_ ? "copy" : "mirror", watchedAddrs_.size());
    if (kopieModus_)
        std::fprintf(diag_, "copy: %d entries, ~%d cycles/frame, %zu bytes, %zu without room, timing %s\n",
                     kopie_.eintraege, kopie_.takte, kopie_.block.size(), kopie_.ohnePlatz.size(),
                     kopieTakt_ == nmikopie::Takt::Nmi          ? "nmi"
                     : kopieTakt_ == nmikopie::Takt::Nachlauf ? "after game irq (busy not meaningful)"
                                                                : "vblank-irq (busy not meaningful)");
    std::fflush(diag_);
}

void MonitorWorker::diagSekunde(const rawsnes::RamMap& ram) {
    if (!diag_) return;
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - diagTakt_).count();
    if (ms < 1000.0) return;
    diagTakt_ = Clock::now();
    std::string werte;
    for (uint32_t a : watchedAddrs_) {
        auto it = ram.find(a);
        char h[4];
        std::snprintf(h, sizeof(h), "%02X", it != ram.end() ? it->second : 0);
        if (!werte.empty()) werte += ' ';
        werte += it != ram.end() ? h : "--";
    }
    std::string vek;
    for (const auto& kv : dVektoren_) {
        char h[16];
        std::snprintf(h, sizeof(h), "%s%02X:%d", vek.empty() ? "" : ",", kv.first, kv.second);
        vek += h;
    }
    const double s = std::chrono::duration<double>(Clock::now() - diagStart_).count();
    std::fprintf(diag_, "%.3f frames/s=%.1f evaluated=%d lost=%d busy=%d sp_idle=%04X runs/s=%.1f vec=%s polls=%d "
                        "poll_ms=%.1f/%.1f changes=%d vals=%s\n",
                 s, (dBilder_ + dVerpasst_) * 1000.0 / ms, dBilder_, dVerpasst_, dVerspaetet_, spRuhe_,
                 dLaeufe_ * 1000.0 / ms, vek.empty() ? "-" : vek.c_str(), dAbfragen_,
                 dAbfragen_ ? dAbfrageMs_ / dAbfragen_ : 0.0, dAbfrageMax_, dAenderungen_, werte.c_str());
    std::fflush(diag_);
    dBilder_ = dVerpasst_ = dVerspaetet_ = dAbfragen_ = dAenderungen_ = dLaeufe_ = 0;
    dVektoren_.clear();
    dAbfrageMs_ = dAbfrageMax_ = 0;
}

void MonitorWorker::diagSchliessen() {
    if (!diag_) return;
    std::fprintf(diag_, "=== end\n");
    std::fclose(diag_);
    diag_ = nullptr;
}

void MonitorWorker::kopieDiagnose() {
    const Clock::time_point jetzt = Clock::now();
    if (diagnoseSeit_ == Clock::time_point{}) {
        diagnoseSeit_ = jetzt;
        return;
    }
    const double sekunden = std::chrono::duration<double>(jetzt - diagnoseSeit_).count();
    if (sekunden < 10.0) return;
    // Ins Protokoll nur, wenn Bilder verloren gingen oder die Diagnose an ist;
    // sonst wuerde alle 10 s dieselbe Zeile die wichtigen Meldungen verdecken.
    const bool melden = verpasstFenster_ > 0 || diag_ != nullptr;
    if (!melden) {
    } else if (bilderFenster_ > 0 && kopieTakt_ != nmikopie::Takt::Nmi) {
        // Im IRQ bzw. danach steht der Stapelzeiger nie in Ruhestellung:
        // keine Aussage ueber Ruckeln moeglich.
        char bps[32];
        std::snprintf(bps, sizeof(bps), "%.1f", (bilderFenster_ + verpasstFenster_) / sekunden);
        emitLog(Fmt(T("Diagnose (10 s): %1 Bilder/s, %2 ausgewertet, %3 uebersprungen (Ruckeln bei diesem Chip nicht messbar)"),
                    {bps, i18n::Zahl(bilderFenster_), i18n::Zahl(verpasstFenster_)}));
    } else if (bilderFenster_ > 0) {
        const int verspaetet = verspaetetFenster_;
        const auto komma = [](double v, const char* format) {
            char t[32];
            std::snprintf(t, sizeof(t), format, v);
            return std::string(t);
        };
        emitLog(Fmt(T("Diagnose (10 s): %1 Bilder/s, %2 ausgewertet, %3 uebersprungen, Abfrage %4 ms, "
                      "verspaetete Bilder (Ruckeln) %5 = %6 %"),
                    {komma((bilderFenster_ + verpasstFenster_) / sekunden, "%.1f"), i18n::Zahl(bilderFenster_),
                     i18n::Zahl(verpasstFenster_),
                     komma(lesungenFenster_ ? lesezeitFenster_ / lesungenFenster_ : 0.0, "%.1f"),
                     i18n::Zahl(verspaetet), komma(100.0 * verspaetet / bilderFenster_, "%.0f")}));
    }
    diagnoseSeit_ = jetzt;
    bilderFenster_ = verpasstFenster_ = lesungenFenster_ = verspaetetFenster_ = 0;
    lesezeitFenster_ = 0;
}

void MonitorWorker::kopieAushaengen() {
    if (!kopieAktiv_ || !fx_) return;
    kopieAktiv_ = false;
    try {
        for (const auto& s : nmikopie::Aushaengen()) fx_->Schreiben(fxpak::Raum::Befehlspuffer, s.adresse, {s.wert});
        emitLog(T("Werte-Kopie ausgehaengt."));
    } catch (const std::exception&) {
        // Geht auch so wieder weg: die Firmware schreibt den Hook bei jedem
        // Spielstart neu.
    }
}

bool MonitorWorker::leseKopie(rawsnes::RamMap& ram) {
    using fxpak::Raum;
    kopieDiagnose();
    nmikopie::Bild b;
    for (int versuch = 0; versuch < 2; ++versuch) {
        const Clock::time_point t0 = Clock::now();
        b = nmikopie::Deute(kopie_, fx_->LesenRaum(Raum::Befehlspuffer, nmikopie::kLeseStart, nmikopie::kLeseLaenge));
        const double dauer = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        lesezeitFenster_ += dauer;
        ++lesungenFenster_;
        ++dAbfragen_;
        dAbfrageMs_ += dauer;
        dAbfrageMax_ = std::max(dAbfrageMax_, dauer);
        if (!b.eingehaengt || b.vollstaendig) break;   // sonst schrieb die Konsole gerade
    }
    int neu = 0;
    if (b.eingehaengt) {
        if (habeLaeufe_) {
            neu = (uint8_t)(b.laeufe - letzteLaeufe_);
            dLaeufe_ += neu;
            laeufeSeitEinrichten_ += neu;
        }
        letzteLaeufe_ = b.laeufe;
        habeLaeufe_ = true;
        ++dVektoren_[b.vektor];
    }

    if (!b.eingehaengt) {
        // Die Firmware hat den Hook neu geschrieben (Reset des Spiels).
        if (Clock::now() >= naechsterNeuaufbau_) {
            naechsterNeuaufbau_ = Clock::now() + std::chrono::seconds(1);
            emitLog(T("NMI-Hook neu geschrieben (Reset?) -- Werte-Kopie wird wieder eingehaengt."));
            kopieEinrichten(false);
        }
        return false;
    }
    // Nach dem IRQ zu kopieren taugt nur, wenn allein IRQs durch den Hook
    // laufen. Kommt doch ein NMI durch (am Geraet: Street Fighter Alpha 2
    // haengt sonst), sofort auf die Kopie im NMI umstellen.
    if (kopieTakt_ == nmikopie::Takt::Nachlauf && b.vektor == 0xEA) {
        emitLog(T("NMI im Hook gesehen -- Werte-Kopie jetzt im NMI."));
        for (const auto& s : nmikopie::Aushaengen()) fx_->Schreiben(Raum::Befehlspuffer, s.adresse, {s.wert});
        ::Sleep(50);
        kopieTakt_ = nmikopie::Takt::Nmi;
        kopieEinrichten(false);
        return false;
    }
    if (!b.vollstaendig || (habeZaehler_ && b.zaehler == zaehler_)) {
        // Der Hook laeuft, aber nur ueber IRQs: Bildtakt ueber die Austastluecke.
        // Nicht mehr ueber den NMI: neu messen. Nur wenn die Routine seit der
        // letzten Lesung gelaufen ist -- bei abgeschaltetem Hook steht im
        // Vektor-Byte noch der letzte Lauf.
        if (kopieTakt_ == nmikopie::Takt::Nmi && laeufeSeitEinrichten_ > 30 && neu > 0 && b.vektor == 0xEE &&
            MsSeit(letztesBild_) > 2000 && Clock::now() >= naechsterNeuaufbau_) {
            naechsterNeuaufbau_ = Clock::now() + std::chrono::seconds(2);
            emitLog(T("Der Hook laeuft bei diesem Spiel jetzt nur ueber IRQs -- neu messen."));
            kopieTakt_ = kopieSonde();
            kopieEinrichten(false);
            return false;
        }
        if (!stilleGemeldet_ && MsSeit(letztesBild_) > 3000) {
            stilleGemeldet_ = true;
            emitLog(T("Seit 3 s kein Bild vom NMI-Hook. Ist der In-Game-Hook am SD2SNES/FXPak eingeschaltet "
                      "(Menue: In-game hook)? Oder pausiert bzw. laedt das Spiel gerade?"));
            emitLog(Fmt(T("  Routine durchlaufen: %1 Mal, zuletzt gelesenes Vektor-Byte $%2."),
                        {i18n::Zahl((long long)(uint8_t)b.laeufe), Upper(i18n::Hex(b.vektor, 2))}));
        }
        return false;
    }
    if (stilleGemeldet_) {
        stilleGemeldet_ = false;
        emitLog(T("NMI-Hook liefert wieder Bilder."));
    }
    if (habeZaehler_) {
        const int abstand = (uint8_t)(b.zaehler - zaehler_);
        if (abstand > 1) {
            verpasst_ += abstand - 1;
            verpasstFenster_ += abstand - 1;
            dVerpasst_ += abstand - 1;
        }
    }
    ++dBilder_;
    // Verspaetet erst zaehlen, wenn die Ruhestellung feststeht (nach 1 s).
    if (spRuhe_ && b.stapel != spRuhe_) {
        ++dVerspaetet_;
        ++verspaetetFenster_;
    }
    zaehler_ = b.zaehler;
    habeZaehler_ = true;
    letztesBild_ = Clock::now();
    ++bilderGesamt_;
    ++bilderFenster_;
    // Ruhestellung = haeufigster Stapelzeiger bisher, alle 50 Bilder neu bestimmt.
    ++stapelGesamt_[b.stapel];
    if (bilderGesamt_ % 50 == 0) {
        long long beste = -1;
        for (const auto& kv : stapelGesamt_)
            if (kv.second > beste) { beste = kv.second; spRuhe_ = kv.first; }
    }

    for (const auto& kv : b.werte) ram[kv.first] = kv.second;
    if (!sramPlan_.Lesungen().empty()) {
        std::vector<std::vector<uint8_t>> antworten;
        for (const auto& l : sramPlan_.Lesungen()) antworten.push_back(fx_->LesenMehrfach(l.bereiche));
        for (const auto& kv : sramPlan_.Verteile(antworten)) ram[kv.first] = kv.second;
    }

    // AddAddress-Zeiger: Ziele im WRAM kommen in die Routine, Ziele im SRAM
    // werden direkt nachgelesen.
    std::set<uint32_t> ziele;
    for (const auto& t : tracked_)
        if (!t.unlocked) t.rt->zielAdressen(ram, ziele);
    std::vector<uint32_t> sramZiele;
    for (uint32_t a : ziele) {
        if (ram.count(a)) continue;
        if (nmikopie::KonsoleLiest(a)) neueZiele_.insert(a);
        else if (snes::RaLesbar(a)) sramZiele.push_back(a);
    }
    if (!sramZiele.empty()) {
        const snes::Leseplan zusatz(sramZiele);
        std::vector<std::vector<uint8_t>> a2;
        for (const auto& l : zusatz.Lesungen()) a2.push_back(fx_->LesenMehrfach(l.bereiche));
        for (const auto& kv : zusatz.Verteile(a2)) ram[kv.first] = kv.second;
    }
    // Neu packen: neue Zeigerziele, oder nach einer Freischaltung ist Platz
    // fuer Achievements frei geworden, die bisher nicht hineinpassten.
    size_t dazu = 0;
    for (uint32_t a : neueZiele_) dazu += kopieZiele_.insert(a).second ? 1 : 0;
    neueZiele_.clear();
    bool wartend = false;
    if (kopieNeuBauen_)
        for (const auto& t : tracked_)
            if (!t.unlocked && !t.rt->unsupported() && !t.lesbar) wartend = true;
    if ((dazu || wartend) && Clock::now() >= naechsterNeuaufbau_) {
        naechsterNeuaufbau_ = Clock::now() + std::chrono::seconds(2);
        kopieNeuBauen_ = false;
        if (dazu)
            emitLog(Fmt(T("Werte-Kopie: %1 Zeigerziel(e) dazu, Routine wird neu geschrieben."),
                        {i18n::Zahl((long long)dazu)}));
        for (const auto& s : nmikopie::Aushaengen()) fx_->Schreiben(Raum::Befehlspuffer, s.adresse, {s.wert});
        ::Sleep(50);
        kopieEinrichten(false);
    } else if (kopieNeuBauen_ && !wartend) {
        kopieNeuBauen_ = false;   // alle offenen sind schon dabei
    }
    return true;
}

// ================= Abfrageschleife =================

rawsnes::RamMap MonitorWorker::leseRam() {
    const Clock::time_point t0 = Clock::now();
    struct Messen {
        MonitorWorker* w;
        Clock::time_point t0;
        ~Messen() {
            const double dauer = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            ++w->dAbfragen_;
            ++w->dBilder_;
            w->dAbfrageMs_ += dauer;
            w->dAbfrageMax_ = std::max(w->dAbfrageMax_, dauer);
        }
    } messen{this, t0};
    std::vector<std::vector<uint8_t>> antworten;
    antworten.reserve(plan_.Lesungen().size());
    for (const auto& l : plan_.Lesungen()) antworten.push_back(fx_->LesenMehrfach(l.bereiche));
    rawsnes::RamMap ram = plan_.Verteile(antworten);

    // Zweiter Schritt fuer AddAddress-Zeiger: wohin sie gerade zeigen, steht
    // erst mit den eben gelesenen Werten fest. Nur das Fehlende nachlesen.
    std::set<uint32_t> ziele;
    for (const auto& t : tracked_)
        if (!t.unlocked) t.rt->zielAdressen(ram, ziele);
    std::vector<uint32_t> fehlend;
    for (uint32_t a : ziele)
        if (!ram.count(a) && snes::RaLesbar(a)) fehlend.push_back(a);
    if (!fehlend.empty()) {
        const snes::Leseplan zusatz(fehlend);
        std::vector<std::vector<uint8_t>> a2;
        for (const auto& l : zusatz.Lesungen()) a2.push_back(fx_->LesenMehrfach(l.bereiche));
        for (const auto& kv : zusatz.Verteile(a2)) ram[kv.first] = kv.second;
    }
    return ram;
}

void MonitorWorker::runPollLoop() {
    Clock::time_point pingTimer = Clock::now();
    Clock::time_point infoTimer = Clock::now();
    bool spielGewechselt = false;
    diagOeffnen();

    while (running_) {
        const Clock::time_point taktBeginn = Clock::now();
        runPendingDiagnose();
        try {
            if (!fx_) {
                // Nach Verbindungsverlust: jede Sekunde neu suchen.
                if (!verbinden()) {
                    ::Sleep(1000);
                    continue;
                }
                emitLog(T("Verbindung wiederhergestellt."));
                havePrevRam_ = false;
            }

            // Spielwechsel und Savestates im Blick behalten.
            if (MsSeit(infoTimer) >= kInfoAbstandMs) {
                infoTimer = Clock::now();
                info_ = fx_->Info();
                const std::string rom = fxpak::IstMenue(info_.rom) ? std::string() : info_.rom;
                if (rom != laufendeRom_) {
                    emitLog(rom.empty() ? std::string(T("Spiel beendet (Menue) -- Ueberwachung endet."))
                                        : std::string(T("Spiel gewechselt -- Ueberwachung endet: ")) + rom);
                    spielGewechselt = true;
                    break;
                }
                if (hardcore_ && (info_.einstellungen & fxpak::kEinstSavestates)) reportViolations({T("Savestates")}, true);
            }

            rawsnes::RamMap ram;
            if (kopieModus_) {
                // Ausgewertet wird jedes Bild genau einmal, sobald es da ist.
                if (!leseKopie(ram)) {
                    fehlversuche_ = 0;
                    diagSekunde(prevRam_);   // auch ohne Bilder: gerade dann aufschlussreich
                    ::Sleep(stilleGemeldet_ ? 50 : 3);
                    continue;
                }
                fehlversuche_ = 0;
                werteAus(ram);
            } else {
                ram = leseRam();
                fehlversuche_ = 0;
                werteAus(ram);
            }

            if (MsSeit(pingTimer) > kPingIntervalMs) {
                std::string richPresence;
                if (!game_.richPresenceScript.empty()) {
                    auto compiled = rawsnes::parseRichPresenceScript(game_.richPresenceScript);
                    if (auto text = rawsnes::evaluateRichPresence(compiled)) richPresence = *text;
                }
                auto [ok, info] = client_->ra_ping(game_.gameid, user_lauf_, token_lauf_, richPresence);
                if (!ok) emitLog(std::string(T("RA-Aktivitaet: Fehler: ")) + info);
                pingTimer = Clock::now();
            }

            // Werte-Kopie: bis zum naechsten Bild bleiben knapp 17 ms; danach wird
            // in kurzen Abstaenden nachgesehen.
            const long long rest = (kopieModus_ ? 10 : kTaktMs) - MsSeit(taktBeginn);
            if (rest > 0) ::Sleep((DWORD)rest);
        } catch (const std::exception& e) {
            // Wie im Leerlauf: auf eine ausstehende Antwort warten, einzelne
            // Aussetzer hinnehmen, erst beim dritten in Folge als Verlust
            // behandeln.
            if (fx_ && FxPakPort::Beschaeftigt(e)) {
                ::Sleep(50);
                continue;
            }
            ++fehlversuche_;
            if (fx_ && fehlversuche_ < 3) {
                ::Sleep(100);
                continue;
            }
            if (connectionLost) connectionLost(e.what());
            emitLog(std::string(T("Verbindung zum SD2SNES/FXPak unterbrochen (z. B. beim Spielwechsel) -- verbinde neu: ")) + e.what());
            if (fx_) fx_->Schliessen();
            fx_.reset();
            ::Sleep(500);
        }
    }
    kopieAushaengen();
    diagSchliessen();
    running_ = false;
    if (spielGewechselt) {
        // Der Leerlauf meldet die neue ROM und startet sie bei Autostart.
        romBekannt_ = false;
        autoGesperrtFuer_.clear();
    }
    emitLog(T("Monitor gestoppt."));
    if (finished) finished();
}

// Ein Durchgang der Auswertung: Live-Werte, Achievements, Leaderboards.
void MonitorWorker::werteAus(const rawsnes::RamMap& ram) {
    {
            // Live-Werte und Aenderungen.
            for (size_t slot = 0; slot < watchedAddrs_.size(); ++slot) {
                const uint32_t addr = watchedAddrs_[slot];
                auto it = ram.find(addr);
                if (it == ram.end()) continue;  // am Geraet nicht lesbar
                const uint8_t val = it->second;
                if (havePrevRam_) {
                    auto pit = prevRam_.find(addr);
                    if (pit != prevRam_.end() && pit->second != val) {
                        ++changeCounts_[slot];
                        ++dAenderungen_;
                        confirmedAddrs_.insert(addr);
                        if (!ersteAenderungGemeldet_) {
                            ersteAenderungGemeldet_ = true;
                            emitLog(Fmt(T("Erste Wertaenderung nach %1 s (%2) -- die Werte kommen an."),
                                        {i18n::Zahl(MsSeit(sessionStart_) / 1000), RaAdr(addr)}));
                        }
                    }
                }
                if (liveValueUpdate) liveValueUpdate((int)slot, val, changeCounts_[slot]);
            }

            // Warmlauf: laufen genug Adressen eine Weile, ist das Spiel wirklich da.
            if (!warmedUp_) {
                if ((int)confirmedAddrs_.size() >= kWarmupMinActive) {
                    if (!haveWarmSince_) {
                        warmSince_ = Clock::now();
                        haveWarmSince_ = true;
                    } else if (MsSeit(warmSince_) >= (long long)(kWarmupSeconds * 1000)) {
                        warmedUp_ = true;
                        emitLog(Fmt(T("Spiel laeuft (%1 Adressen aktiv) -- alle Achievements scharf."),
                                    {i18n::Zahl((long long)confirmedAddrs_.size())}));
                    }
                } else {
                    haveWarmSince_ = false;
                }
            }

            // Takt messen: Trefferzaehler (".400.") zaehlen Bilder, die SNES
            // zeigt 60 je Sekunde. Liegt der Takt deutlich darunter, brauchen
            // solche Bedingungen laenger als im Spiel vorgesehen.
            ++durchgaenge_;
            if (!taktGemeldet_ && MsSeit(sessionStart_) > 10000) {
                taktGemeldet_ = true;
                const double hz = durchgaenge_ * 1000.0 / (double)MsSeit(sessionStart_);
                char t[32];
                std::snprintf(t, sizeof(t), "%.1f", hz);
                if (kopieModus_)
                    emitLog(Fmt(T("Werte-Kopie: %1 Bilder je Sekunde ausgewertet, %2 verpasst."),
                                {t, i18n::Zahl(verpasst_)}));
                else
                    emitLog(Fmt(T("Abfragetakt: %1 Durchgaenge je Sekunde (Ziel 60)."), {t}));
            }

            if (!warned3s_ && MsSeit(sessionStart_) > 3000) {
                warned3s_ = true;
                int aktiv = 0;
                for (int c : changeCounts_) if (c > 0) ++aktiv;
                if (aktiv == 0) {
                    emitLog(T("WARNUNG: nach 3 s keine einzige Wertaenderung.\n"
                              "  Moegliche Gruende:\n"
                              "  1. Laeuft das Spiel wirklich (nicht pausiert, nicht im Menue)?\n"
                              "  2. Beschreibt dieses Spiel die Adressen schon (Titelbild)?\n"
                              "  3. Ist die Firmware des SD2SNES/FXPak aktuell?"));
                } else {
                    emitLog(Fmt(T("Daten kommen an: %1/%2 Adressen aendern sich."),
                                {i18n::Zahl(aktiv), i18n::Zahl((long long)watchedAddrs_.size())}));
                }
            }

            if (havePrevRam_) {
                for (auto& t : tracked_) {
                    if (t.unlocked || t.rt->unsupported() || !t.lesbar) continue;
                    const bool frei = warmedUp_ || std::includes(confirmedAddrs_.begin(), confirmedAddrs_.end(),
                                                                 t.needed.begin(), t.needed.end());
                    const bool wasWaiting = waitingIds_.count(t.info.id) > 0;
                    if (!frei && !wasWaiting) {
                        waitingIds_.insert(t.info.id);
                        if (achievementWaitingChanged) achievementWaitingChanged(t.info.id, true);
                    } else if (frei && wasWaiting) {
                        waitingIds_.erase(t.info.id);
                        if (achievementWaitingChanged) achievementWaitingChanged(t.info.id, false);
                    }
                    if (!frei) continue;

                    const bool fired = t.rt->update(ram, &prevRam_);
                    auto prog = t.rt->progress();
                    if (prog.hasProgress && progressChanged) progressChanged(t.info.id, prog.current, prog.target);
                    if (fired) {
                        t.unlocked = true;
                        if (kopieModus_) kopieNeuBauen_ = true;   // Platz wird frei
                        auto [ok, info] = client_->ra_award(t.info.id, user_lauf_, token_lauf_, hardcore_);
                        if (unlocked) unlocked(t.info.title, t.info.points, t.info.id, hardcore_);
                        abdeckungPruefen();   // die Zaehlung der offenen aendert sich
                        emitLog(Fmt(T("Achievement freigeschaltet: %1 (%2)"),
                                    {t.info.title, ok ? std::string(T("gebucht"))
                                                      : std::string(T("Fehler: ")) + info}));
                        // Fuer die Fehlersuche: Bedingung und Werte im Moment des Ausloesens.
                        std::string werte;
                        for (auto a : t.needed) {
                            auto it = ram.find(a);
                            if (!werte.empty()) werte += ", ";
                            werte += RaAdr(a) + "=" + (it != ram.end() ? std::to_string(it->second) : std::string("?"));
                        }
                        emitLog("  MemAddr: " + t.info.mem);
                        emitLog("  " + std::string(T("Werte: ")) + werte);
                    }
                }

                // Leaderboards: eingereicht wird nur im Hardcore-Modus (RA-Regel).
                for (auto& lb : leaderboards_) {
                    double val = 0;
                    switch (lb.update(ram, &prevRam_, &val)) {
                        case rawsnes::LeaderboardRuntime::Event::Start:
                            if (leaderboardStarted) leaderboardStarted(lb.id(), lb.title());
                            break;
                        case rawsnes::LeaderboardRuntime::Event::Cancel:
                            if (leaderboardCanceled) leaderboardCanceled(lb.id(), lb.title());
                            break;
                        case rawsnes::LeaderboardRuntime::Event::Submit: {
                            char buf[64];
                            std::snprintf(buf, sizeof(buf), "%.0f", val);
                            const std::string formatted = buf;
                            if (!hardcore_) {
                                emitLog(Fmt(T("  Leaderboard %1: Wert %2 -- NICHT eingereicht "
                                              "(nur im Hardcore-Modus gueltig)."),
                                            {lb.title(), formatted}));
                                break;
                            }
                            auto [ok, info] = client_->ra_submit_lb(lb.id(), user_lauf_, token_lauf_,
                                                                    static_cast<uint32_t>(val));
                            if (leaderboardSubmitted) leaderboardSubmitted(lb.id(), lb.title(), formatted);
                            emitLog(Fmt(T("  *** Leaderboard %1: %2 (%3) ***"),
                                        {lb.title(), formatted,
                                         ok ? std::string(T("eingereicht")) : std::string(T("Fehler: ")) + info}));
                            break;
                        }
                        case rawsnes::LeaderboardRuntime::Event::None:
                            break;
                    }
                }
            }

            diagSekunde(ram);
            prevRam_ = ram;
            havePrevRam_ = true;
    }
}
