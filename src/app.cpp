#include "app.h"

#include "erzeugt_katze.h"
#include "erzeugt_logo.h"
#include "i18n.h"
#include "plattform.h"
#include "popup.h"
#include "ra_http.h"

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

// Farben vom Super Nintendo: hellgraues Gehaeuse, dunkleres Grau, die lila
// Schiebeschalter der US-Konsole und die vier Tasten des Super Famicom.
constexpr ImU32 Rgb(unsigned rgb, unsigned a = 255) {
    return IM_COL32((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, a);
}
constexpr ImU32 kLight = Rgb(0xd6d5dc);        // Gehaeuse oben (Kopf)
constexpr ImU32 kLightRille = Rgb(0xbdbcc6);   // Lueftungsrillen
constexpr ImU32 kLightText = Rgb(0x3a3946);    // Schrift auf hellem Grund
constexpr ImU32 kLightLeise = Rgb(0x6d6b7c);
constexpr ImU32 kDark = Rgb(0x3a3943);         // Hauptflaeche
constexpr ImU32 kPanel = Rgb(0x2d2c35);        // abgesetzte Flaechen
constexpr ImU32 kLila = Rgb(0x5b4fc2);         // Schiebeschalter
constexpr ImU32 kLilaHell = Rgb(0xa49ce8);
constexpr ImU32 kTextMain = Rgb(0xe6e4ee);
constexpr ImU32 kTextMuted = Rgb(0xa7a5b4);
constexpr ImU32 kTasteRot = Rgb(0xd93a3f);
constexpr ImU32 kTasteGelb = Rgb(0xf0bf2c);
constexpr ImU32 kTasteBlau = Rgb(0x3e6fd8);
constexpr ImU32 kTasteGruen = Rgb(0x2fa558);
constexpr ImU32 kUnlocked = Rgb(0x55c27a);     // freigeschaltet
constexpr ImU32 kInProgress = kTasteGelb;      // Hit-Count laeuft
constexpr ImU32 kLockedIconBg = Rgb(0x24232b); // Platzhalter gesperrtes Badge
constexpr ImU32 kJustUnlocked = Rgb(0xffd75e); // goldener Blitz beim Freischalten
constexpr ImU32 kWaiting = Rgb(0x7f9be6);      // wartet auf bestaetigte Adressen
constexpr ImU32 kFehlerRot = Rgb(0xe06060);

// Hardcore bleibt im Code, ist aber gesperrt: RetroAchievements nimmt
// Hardcore-Freischaltungen von diesem Geraet (noch) nicht an.
constexpr bool kHardcoreErlaubt = false;

// Diagnosedatei diagnose_live.txt neben der Exe (eine Zeile je Sekunde).
// Im Testbau an; fuer eine Veroeffentlichung auf false.
constexpr bool kDiagnose = false;

constexpr ImU32 kSeite = Rgb(0x2f2e37);        // Seitenleiste
constexpr ImU32 kKarte = Rgb(0x8e8d99);        // Cartridge-Gehaeuse
constexpr ImU32 kEtikett = Rgb(0xebeaf0);      // Cartridge-Etikett
constexpr int kLogMaxZeilen = 2000;

constexpr const char* kKofiUrl = "https://ko-fi.com/liqui69747";
constexpr const char* kSupportUrl = "https://liquid-wq.github.io/data/";
constexpr const char* kIssuesUrl = "https://github.com/liquid-wq/raw-snes/issues";
constexpr const char* kReleaseUrl = "https://github.com/liquid-wq/raw-snes/releases/latest";

// Update-Hinweis wie bei RAW-TURN und MEGA-RAW: Die Seite nennt in einer
// Textdatei die Build-Nummer der aktuellen Veroeffentlichung. Ist sie hoeher
// als die eigene, erscheint einmal ein Hinweis. Ohne Netz bleibt es still.
constexpr const char* kVersionHost = "liquid-wq.github.io";
constexpr const char* kVersionPfad = "/data/version_rawsnes.txt";
std::atomic<int> g_neuerBuild{0};
bool g_updateGezeigt = false;
constexpr const char* kKatzeHinweis = "aber falls du darueber nachdenkst,\nlies bitte zuerst 'ueber die Katze'";

ImVec4 V(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

ImU32 MitDeckkraft(ImU32 c, float a) {
    const unsigned alpha = (unsigned)std::lround(((c >> IM_COL32_A_SHIFT) & 0xFF) * a);
    return (c & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

unsigned long long JetztMs() { return ::GetTickCount64(); }

void Txt(ImFont* f, float px, ImU32 farbe, const std::string& s) {
    ImGui::PushFont(f, px);
    ImGui::PushStyleColor(ImGuiCol_Text, V(farbe));
    ImGui::TextUnformatted(s.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void Zentriert(ImFont* f, float px, ImU32 farbe, const std::string& s) {
    ImGui::PushFont(f, px);
    ImGui::PushStyleColor(ImGuiCol_Text, V(farbe));
    std::istringstream zeilen(s);
    std::string z;
    while (std::getline(zeilen, z)) {
        const float b = ImGui::CalcTextSize(z.c_str()).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - b) * 0.5f));
        ImGui::TextUnformatted(z.c_str());
    }
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

std::string LeseDatei(const std::string& pfad) {
    std::ifstream f(fs::u8path(pfad), std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Farbe eines frisch geaenderten Live-Werts: gold, glimmt ueber 1 s zum
// Ruhe-Gruen zurueck.
ImU32 FarbeNachAlter(unsigned long long lastMs, unsigned long long now) {
    constexpr int kGoldR = 0xff, kGoldG = 0xc8, kGoldB = 0x3d;
    constexpr int kRestR = 0x8f, kRestG = 0xe0, kRestB = 0xa8;
    constexpr unsigned long long kFadeMs = 1000;
    if (lastMs == 0) return IM_COL32(kRestR, kRestG, kRestB, 255);
    const unsigned long long age = now - lastMs;
    if (age >= kFadeMs) return IM_COL32(kRestR, kRestG, kRestB, 255);
    const double t = (double)age / (double)kFadeMs;
    return IM_COL32((int)(kGoldR + t * (kRestR - kGoldR)), (int)(kGoldG + t * (kRestG - kGoldG)),
                    (int)(kGoldB + t * (kRestB - kGoldB)), 255);
}

// Checkbox: Rahmen ohne Fuellung, Haken in Lila-Hell.
bool Haken(const char* text, bool* wert) {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, V(0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, V(Rgb(0xe6e4ee, 24)));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, V(Rgb(0xe6e4ee, 40)));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, V(kLilaHell));
    ImGui::PushStyleColor(ImGuiCol_Border, V(kTextMuted));
    const bool geaendert = ImGui::Checkbox(text, wert);
    ImGui::PopStyleColor(5);
    return geaendert;
}

// Knopf in einer Grundfarbe (Lila fuer Start, Rot fuer Stopp).
bool FarbKnopf(const char* text, ImU32 farbe, const ImVec2& groesse = ImVec2(0, 0)) {
    const ImVec4 c = V(farbe);
    ImGui::PushStyleColor(ImGuiCol_Button, c);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(std::min(1.0f, c.x * 1.15f), std::min(1.0f, c.y * 1.15f),
                                                         std::min(1.0f, c.z * 1.15f), 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(c.x * 0.85f, c.y * 0.85f, c.z * 0.85f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, V(Rgb(0xffffff, 60)));
    ImGui::PushStyleColor(ImGuiCol_Text, V(Rgb(0xffffff)));
    const bool k = ImGui::Button(text, groesse);
    ImGui::PopStyleColor(5);
    return k;
}

std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string NurName(const std::string& pfad) {
    const size_t p = pfad.find_last_of("/\\");
    return p == std::string::npos ? pfad : pfad.substr(p + 1);
}

// RA-Adresse so, wie man sie am SNES kennt: WRAM als $7E:xxxx/$7F:xxxx,
// Cartridge-RAM als SRAM-Versatz.
std::string SnesAdresse(uint32_t ra) {
    char t[16];
    if (ra < 0x20000) std::snprintf(t, sizeof(t), "$%06X", 0x7E0000u + ra);
    else std::snprintf(t, sizeof(t), "S:%05X", ra - 0x20000u);
    return t;
}

}  // namespace

// ============================================================================
// Aufbau und Abbau
// ============================================================================

App::App(void* fenster, float dpi, const Schriften& schriften)
    : fenster_(fenster), dpi_(dpi), f_(schriften),
      cache_(plattform::ExeOrdner() + "/ra_cache_snes.json"),
      client_(std::make_shared<RaClient>(cache_, make_winhttp_request_fn())) {
    // Worker EINMAL erzeugen -- lebt fuer die gesamte Laufzeit in seinem
    // eigenen Thread und sucht dort selbst nach dem Modul.
    worker_ = std::make_unique<MonitorWorker>(client_);
    if (kDiagnose) worker_->setDiagnoseDatei(plattform::ExeOrdner() + "/diagnose_live.txt");
    verdrahteWorker();
    badgeThread_ = std::thread([this]() { BadgeThread(); });
}

void App::UpdatePruefen() {
    // Losgeloest und ohne Bezug auf die App: die Abfrage soll das Beenden nie
    // aufhalten.
    std::thread([]() {
        ::Sleep(2000);
        std::string body;
        if (!http_get(kVersionHost, kVersionPfad, 8000, RA_USER_AGENT, &body)) return;
        // Eine Byte-Order-Mark oder Leerraum vor der Zahl ueberspringen.
        size_t i = 0;
        while (i < body.size() && !std::isdigit((unsigned char)body[i])) ++i;
        const int n = std::atoi(body.c_str() + i);
        if (n > RAWSNES_BUILD) g_neuerBuild = n;
    }).detach();
}

App::~App() {
    if (worker_) worker_->stop();
    worker_.reset();
    {
        std::lock_guard<std::mutex> l(badgeMutex_);
        badgeEnde_ = true;
    }
    badgeCv_.notify_all();
    if (badgeThread_.joinable()) badgeThread_.join();
    for (auto& t : hilfsThreads_) if (t.joinable()) t.join();
    bilder::Freigeben(logo_);
    for (auto& kv : badges_) bilder::Freigeben(kv.second.tex);
}

void App::Post(std::function<void()> f) {
    std::lock_guard<std::mutex> l(postMutex_);
    postQueue_.push_back(std::move(f));
}

std::string App::configPath() const { return plattform::ExeOrdner() + "/rawsnes_config.json"; }

void App::Starten() {
    // Katzen-Portrait, RGBA 128x128, belegt 69x106.
    logo_ = bilder::Hochladen(kLogoPixel, kLogoTexturKante, kLogoTexturKante, false);
    logo_.w = kLogoBreite;
    logo_.h = kLogoHoehe;
    logo_.u = (float)kLogoBreite / kLogoTexturKante;
    logo_.v = (float)kLogoHoehe / kLogoTexturKante;

    loadConfig();
    appendLog(Fmt(T("RAW-SNES %1 (Build %2) bereit -- suche SD2SNES/FXPak..."),
                  {RAWSNES_VERSION, i18n::Zahl(RAWSNES_BUILD)}));

    worker_->setAutostart(autostart_);
    worker_->setHardcore(hardcore_);
    if (!cfg_.hashes.empty()) worker_->seedHashes(cfg_.hashes);

    // Auto-Login, wenn Zugangsdaten gespeichert sind. Eingebettet meldet der
    // Rahmen an und reicht den Schluessel ueber LoginSetzen() weiter.
    if (!eingebettet_ && userBuf_[0] && !cfg_.raToken.empty()) onLogin();
}

// ============================================================================
// Worker-Verdrahtung
// ============================================================================

void App::verdrahteWorker() {
    MonitorWorker& w = *worker_;
    w.log = [this](const std::string& m) { Post([this, m]() { appendLog(m); }); };
    w.connected = [this](const std::string& port, const fxpak::GeraeteInfo& info) {
        Post([this, port, info]() {
            connected_ = true;
            connPort_ = port;
            std::string g = info.geraet.empty() ? std::string("SD2SNES/FXPak") : info.geraet;
            if (!info.version.empty()) g += "  \xC2\xB7  Firmware " + info.version;
            geraetText_ = g;
        });
    };
    w.disconnected = [this](const std::string&) {
        Post([this]() {
            connected_ = false;
            connPort_.clear();
            geraetText_.clear();
            laufendeRom_.clear();
            romBekannt_ = false;
        });
    };
    w.romChanged = [this](const std::string& rom) {
        Post([this, rom]() {
            laufendeRom_ = rom;
            romBekannt_ = true;
            keinSet_ = 0;
        });
    };
    w.identifyProgress = [this](uint32_t gelesen, uint32_t gesamt) {
        Post([this, gelesen, gesamt]() {
            hashFortschritt_ = gesamt ? std::min(1.0f, (float)gelesen / (float)gesamt) : 0.0f;
        });
    };
    w.monitoringStarted = [this]() {
        Post([this]() {
            monitoring_ = true;
            hardcoreEnabled_ = false;
            abdeckungDa_ = false;
            keinSet_ = 0;
            hashFortschritt_ = 0.0f;
            gameStatus_.clear();
            ach_.clear();
            live_.clear();
            lbs_.clear();
        });
    };
    w.romNotDetected = [this]() {
        Post([this]() {
            hashFortschritt_ = -1.0f;
            Frage(T("Spiel nicht erkannt"),
                  T("Die laufende ROM konnte nicht vom SD2SNES/FXPak gelesen werden.\n\n"
                    "Dieselbe ROM jetzt am PC auswaehlen? Daraus wird der Hash fuer "
                    "RetroAchievements gebildet."),
                  [this]() {
                      const std::string path = plattform::DateiWaehlen(
                          fenster_, T("SNES-ROM waehlen"),
                          {{i18n::Englisch() ? "SNES ROMs" : "SNES-ROMs", "*.sfc;*.smc;*.swc;*.fig;*.bs"},
                           {i18n::Englisch() ? "All files" : "Alle Dateien", "*.*"}});
                      if (path.empty()) { monitorZuruecksetzen(); return; }
                      worker_->continueWithPath(path);
                  },
                  [this]() { monitorZuruecksetzen(); });
        });
    };
    w.gameLoaded = [this](const std::string& name) {
        Post([this, name]() {
            gameStatus_ = name;
            hashFortschritt_ = -1.0f;
        });
    };
    w.achievementsReady = [this](const GameData& game, const std::set<long long>& already) {
        Post([this, game, already]() {
            ach_.clear();
            selectedId_ = -1;
            for (const auto& ac : game.achievements) {
                AchZeile z;
                z.id = ac.id;
                z.title = ac.title;
                z.desc = ac.desc;
                z.points = ac.points;
                z.badge = ac.badge;
                z.state = already.count(ac.id) ? AchState::Unlocked : AchState::Locked;
                if (z.state == AchState::Locked && rawsnes::AchievementRuntime(ac.mem).unsupported())
                    z.state = AchState::Unsupported;
                // Die Abdeckung kommt vor der Liste an (Werte-Kopie wird zuerst eingerichtet).
                z.ohnePlatz = abdeckungDa_ && abdeckung_.ohnePlatzIds.count(ac.id) > 0;
                ach_.push_back(std::move(z));
            }
        });
    };
    w.unlocked = [this](const std::string& title, int points, long long achId, bool) {
        Post([this, title, points, achId]() {
            std::string desc, badge;
            for (auto& z : ach_) {
                if (z.id != achId) continue;
                z.state = AchState::JustUnlocked;
                desc = z.desc;
                badge = z.badge;
            }
            std::string png;
            if (!badge.empty()) {
                if (Badge* b = BadgeHolen(badge)) png = b->png;
                if (png.empty()) png = LeseDatei(plattform::ExeOrdner() + "/badge_cache/" + badge + ".png");
            }
            popup::Zeigen(title, desc, points, png, i18n::Englisch(), dpi_);
            zuletzt_.push_back({title, gameStatus_});
        });
    };
    w.progressChanged = [this](long long achId, int current, int target) {
        Post([this, achId, current, target]() {
            for (auto& z : ach_) {
                if (z.id != achId) continue;
                if (target <= 1) { z.showProgress = false; continue; }
                z.showProgress = true;
                z.progCur = current;
                z.progTarget = target;
            }
        });
    };
    w.achievementWaitingChanged = [this](long long id, bool waiting) {
        Post([this, id, waiting]() {
            for (auto& z : ach_) {
                if (z.id != id) continue;
                if (z.state == AchState::Unlocked || z.state == AchState::JustUnlocked) continue;
                z.state = waiting ? AchState::Waiting : AchState::Locked;
            }
        });
    };
    w.connectionLost = [this](const std::string&) {
        Post([this]() {
            connected_ = false;
            connPort_.clear();
        });
    };
    w.hardcoreDowngraded = [this](const std::string& liste) {
        Post([this, liste]() {
            hardcore_ = false;
            appendLog(std::string(T("Hardcore beendet -- Grund: ")) + liste);
            Warnung(T("Hardcore beendet"),
                    Fmt(T("Hardcore ist fuer diese Sitzung nicht moeglich: %1\n\n"
                          "Es zaehlt jetzt Softcore. Fuer Hardcore im Menue des SD2SNES/FXPak "
                          "Savestates (In-game Savestates) und Cheats ausschalten, dann "
                          "die Ueberwachung neu starten."),
                        {liste}));
        });
    };
    w.hashLearned = [this](const std::string& schluessel, const std::string& md5) {
        Post([this, schluessel, md5]() {
            cfg_.hashes[schluessel] = md5;
            saveConfig();
        });
    };
    w.abdeckungGeaendert = [this](const Abdeckung& a) {
        Post([this, a]() {
            abdeckung_ = a;
            abdeckungDa_ = true;
            for (auto& z : ach_) z.ohnePlatz = a.ohnePlatzIds.count(z.id) > 0;
            if (!a.voll() && abdeckungHinweisFuer_ != gameStatus_) {
                abdeckungHinweisFuer_ = gameStatus_;
                Info(T("Nur teilweise unterstuetzt"), AbdeckungText(true));
            }
        });
    };
    w.leaderboardsReady = [this](const LeaderboardList& lbs) {
        Post([this, lbs]() {
            lbs_.clear();
            for (const auto& [id, title] : lbs) {
                LbZeile z;
                z.id = id;
                z.title = Trim(title).empty() ? Fmt(T("Leaderboard #%1"), {i18n::Zahl(id)}) : title;
                z.status = "wartet auf Start";
                lbs_.push_back(std::move(z));
            }
        });
    };
    auto lbStatus = [this](long long id, const std::string& status) {
        for (auto& z : lbs_) if (z.id == id) z.status = status;
    };
    w.leaderboardStarted = [this, lbStatus](long long id, const std::string& title) {
        Post([this, lbStatus, id, title]() {
            lbStatus(id, "laeuft");
            appendLog(std::string(T("Leaderboard laeuft: ")) + title);
        });
    };
    w.leaderboardCanceled = [this, lbStatus](long long id, const std::string& title) {
        Post([this, lbStatus, id, title]() {
            lbStatus(id, "abgebrochen");
            appendLog(std::string(T("Leaderboard abgebrochen: ")) + title);
        });
    };
    w.leaderboardSubmitted = [this, lbStatus](long long id, const std::string& title, const std::string& value) {
        Post([this, lbStatus, id, title, value]() {
            lbStatus(id, std::string("eingereicht: \x01") + value);
            appendLog(std::string(T("Leaderboard eingereicht: ")) + title + " = " + value);
        });
    };
    w.keinSet = [this](bool fassung) { Post([this, fassung]() { keinSet_ = fassung ? 2 : 1; }); };
    w.finished = [this]() { Post([this]() { monitorZuruecksetzen(); }); };
    w.liveValuesInit = [this](const std::vector<uint32_t>& addrs) {
        Post([this, addrs]() {
            live_.clear();
            for (size_t i = 0; i < addrs.size(); ++i) {
                LiveZeile z;
                z.addr = addrs[i];
                live_[(int)i] = z;
            }
        });
    };
    w.liveValueUpdate = [this](int slot, uint8_t value, int changeCount) {
        Post([this, slot, value, changeCount]() {
            auto it = live_.find(slot);
            if (it == live_.end()) return;
            LiveZeile& z = it->second;
            if (z.value != value && z.value >= 0) z.lastChangeMs = JetztMs();
            z.value = value;
            z.changeCount = changeCount;
        });
    };
}

void App::monitorZuruecksetzen() {
    monitoring_ = false;
    hardcoreEnabled_ = true;
    hashFortschritt_ = -1.0f;
    hardcore_ = kHardcoreErlaubt && cfg_.hardcore;  // eine Herabstufung gilt nur fuer die Sitzung
}

// ============================================================================
// Konfiguration
// ============================================================================

void App::loadConfig() {
    cfg_.Laden(configPath());
    std::snprintf(userBuf_, sizeof(userBuf_), "%s", cfg_.raUser.c_str());
    rememberLogin_ = cfg_.rememberLogin;
    autostart_ = cfg_.autostart;
    // Ein frueher gespeichertes "Hardcore an" gilt nicht, solange es gesperrt ist.
    hardcore_ = kHardcoreErlaubt && cfg_.hardcore;
    // Eingebettet bestimmt der Rahmen die Sprache.
    if (!eingebettet_) i18n::SpracheSetzen(cfg_.language);
}

void App::saveConfig() {
    cfg_.raUser = userBuf_;
    // Nur der Schluessel, nie das Passwort. Vor der Anmeldung (token_ leer)
    // bleibt ein gespeicherter Schluessel erhalten; Abmelden loescht ihn.
    if (!token_.empty()) cfg_.raToken = token_;
    cfg_.rememberLogin = rememberLogin_;
    cfg_.language = i18n::Sprache();
    cfg_.autostart = autostart_;
    cfg_.Speichern(configPath());
}

void App::setLanguage(const std::string& lang) {
    if (i18n::Sprache() == lang) return;
    i18n::SpracheSetzen(lang);
    saveConfig();
}

void App::appendLog(const std::string& msg) {
    size_t start = 0;
    for (;;) {
        const size_t nl = msg.find('\n', start);
        log_.push_back(msg.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    while ((int)log_.size() > kLogMaxZeilen) log_.pop_front();
}

// ============================================================================
// Ablaeufe
// ============================================================================

void App::onLogin() {
    const std::string user = userBuf_;
    const std::string pw = passBuf_;
    // Ohne eingegebenes Passwort: mit dem gespeicherten Schluessel anmelden.
    const std::string schluessel = pw.empty() ? cfg_.raToken : std::string();
    if (user.empty() || (pw.empty() && schluessel.empty())) {
        loginStatusText_.clear();
        loginStatusKey_ = "Bitte Benutzername und Passwort eingeben.";
        return;
    }
    loginLaeuft_ = true;
    loginStatusText_.clear();
    loginStatusKey_ = "Login laeuft...";

    hilfsThreads_.emplace_back([this, user, pw, schluessel]() {
        std::string statusText, statusKey, newToken;
        bool ok = false;
        try {
            auto tok = pw.empty() ? client_->ra_login_token(user, schluessel) : client_->ra_login(user, pw);
            if (tok) {
                newToken = *tok;
                statusText = "\xE2\x9C\x93 " + user;  // ✓
                ok = true;
            } else {
                statusKey = pw.empty() ? "Anmeldung abgelaufen -- bitte das Passwort eingeben."
                                       : "Login fehlgeschlagen (falsche Zugangsdaten?).";
            }
        } catch (const RateLimited& e) {
            statusText = "RA drosselt, bitte " + std::to_string(e.retry_after) + "s warten.";
        } catch (const std::exception& e) {
            statusText = std::string("Netzwerkfehler: ") + e.what();
        }
        Post([this, statusText, statusKey, newToken, ok, user]() {
            loginLaeuft_ = false;
            loginStatusText_ = statusText;
            if (!statusKey.empty()) loginStatusKey_ = statusKey;
            if (ok) {
                loginOk_ = true;
                token_ = newToken;
                std::memset(passBuf_, 0, sizeof(passBuf_));   // das Passwort wird nicht weiter gebraucht
                worker_->setAnmeldung(user, token_);
                appendLog(T("Login erfolgreich."));
                saveConfig();
            }
        });
    });
}

void App::onLogout() {
    if (monitoring_) worker_->stop();
    token_.clear();
    cfg_.raToken.clear();
    saveConfig();
    loginOk_ = false;
    loginStatusText_.clear();
    loginStatusKey_ = "nicht angemeldet";
    worker_->setAnmeldung("", "");
    appendLog(T("Abgemeldet."));
}

void App::onToggleMonitor() {
    if (monitoring_) {
        worker_->stop();
        return;  // finished() raeumt auf
    }
    if (token_.empty()) {
        appendLog(T("Bitte zuerst anmelden."));
        return;
    }
    monitoring_ = true;
    hardcoreEnabled_ = false;
    worker_->setHardcore(hardcore_);
    worker_->startMonitoring();
}

void App::probeEinblendung() {
    popup::Zeigen(T("Probe-Anzeige"), T("So sieht eine Freischaltung aus."), 10, std::string(),
                  i18n::Englisch(), dpi_);
}

// ============================================================================
// Dialoge
// ============================================================================

void App::Info(const std::string& titel, const std::string& text, std::function<void()> ok) {
    Dialog d;
    d.art = Dialog::Art::Info;
    d.titel = titel;
    d.text = text;
    d.ja = std::move(ok);
    d.nummer = ++dialogNummer_;
    dialoge_.push_back(std::move(d));
}

void App::Warnung(const std::string& titel, const std::string& text) {
    Dialog d;
    d.art = Dialog::Art::Warnung;
    d.titel = titel;
    d.text = text;
    d.nummer = ++dialogNummer_;
    dialoge_.push_back(std::move(d));
}

void App::Fehler(const std::string& titel, const std::string& text) {
    Dialog d;
    d.art = Dialog::Art::Fehler;
    d.titel = titel;
    d.text = text;
    d.nummer = ++dialogNummer_;
    dialoge_.push_back(std::move(d));
}

void App::Frage(const std::string& titel, const std::string& text,
                std::function<void()> ja, std::function<void()> nein) {
    Dialog d;
    d.art = Dialog::Art::Frage;
    d.titel = titel;
    d.text = text;
    d.ja = std::move(ja);
    d.nein = std::move(nein);
    d.nummer = ++dialogNummer_;
    dialoge_.push_back(std::move(d));
}

void App::DialogAuf(Dialog::Art art) {
    // Optionen und Ko-fi nur einmal in der Warteschlange.
    for (const auto& d : dialoge_) if (d.art == art) return;
    Dialog d;
    d.art = art;
    d.nummer = ++dialogNummer_;
    switch (art) {
        case Dialog::Art::Optionen: d.titel = T("Optionen"); break;
        case Dialog::Art::Kofi: d.titel = "Ko-fi"; break;
        case Dialog::Art::Katze: d.titel = T("ueber die Katze"); break;
        default: break;
    }
    dialoge_.push_back(std::move(d));
}

void App::ZeichneKatzenText() {
    ImGui::PushStyleColor(ImGuiCol_Text, V(kTextMain));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(i18n::Englisch() ? kKatzeTextEn : kKatzeText);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void App::ZeichneKatze(bool& schliessen) {
    const float s = dpi_;
    if (logo_.ok()) {
        const ImVec2 g(logo_.w * 2.0f * s, logo_.h * 2.0f * s);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - g.x) * 0.5f);
        ImGui::Image((ImTextureID)(intptr_t)logo_.id, g, ImVec2(0, 0), ImVec2(logo_.u, logo_.v));
    }
    Zentriert(f_.fett, 14, kLilaHell, "In memory of Jason");
    Zentriert(f_.ui, 11, kTextMuted, "2010 \xE2\x80\x93 2025");
    ImGui::Dummy(ImVec2(0, 4 * s));
    const float unten = ImGui::GetFrameHeightWithSpacing() + 8 * s;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(kPanel));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * s, 12 * s));
    ImGui::BeginChild("katzentext", ImVec2(0, ImGui::GetContentRegionAvail().y - unten),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    ZeichneKatzenText();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 4 * s));
    if (ImGui::Button("OK", ImVec2(-FLT_MIN, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) schliessen = true;
}

void App::ZeichneOptionen(bool& schliessen) {
    const float s = dpi_;
    const float breit = 340 * s;
    auto Marke = [&](const char* t) {
        Txt(f_.fett, 12, kLilaHell, T(t));
        ImGui::Dummy(ImVec2(0, 1 * s));
    };

    Marke("Sprache / Language");
    if (ImGui::RadioButton("Deutsch", !i18n::Englisch())) setLanguage("de");
    ImGui::SameLine(0, 18 * s);
    if (ImGui::RadioButton("English", i18n::Englisch())) setLanguage("en");
    ImGui::Dummy(ImVec2(0, 6 * s));

    Marke("Automatik");
    if (Haken(T("Ueberwachung automatisch starten, sobald ein Spiel laeuft"), &autostart_)) {
        worker_->setAutostart(autostart_);
        saveConfig();
    }
    ImGui::Dummy(ImVec2(0, 6 * s));

    Marke("Freischalt-Anzeige");
    if (ImGui::Button(T("Freischalt-Anzeige testen"), ImVec2(breit, 0))) probeEinblendung();
    ImGui::Dummy(ImVec2(0, 6 * s));

    Marke("Hilfe");
    if (ImGui::Button(T("Fehler melden"), ImVec2(breit, 0))) plattform::UrlOeffnen(kIssuesUrl);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Oeffnet die Fehlerliste auf GitHub im Browser."));
    if (ImGui::Button(T("Log kopieren"), ImVec2(breit, 0))) {
        std::string alles;
        for (const auto& z : log_) { alles += z; alles += "\r\n"; }
        ImGui::SetClipboardText(alles.c_str());
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Fuer Fehlermeldungen: das komplette Log in die Zwischenablage."));
    ImGui::Dummy(ImVec2(0, 8 * s));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4 * s));

    // Support-Verweis und Hinweis, wie in mega-raw und raw-turn.
    {
        const float b = ImGui::CalcTextSize("Support").x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - b) * 0.5f);
        if (ImGui::TextLink("Support")) plattform::UrlOeffnen(kSupportUrl);
        Zentriert(f_.kursiv ? f_.kursiv : f_.ui, 12, kTextMuted, T(kKatzeHinweis));
    }
    ImGui::Dummy(ImVec2(0, 6 * s));
    if (ImGui::Button(T("ueber die Katze"), ImVec2(breit, 0))) {
        katzeAusOptionen_ = true;
        ImGui::OpenPopup("###katze_innen");
    }
    ImGui::Dummy(ImVec2(0, 2 * s));
    if (ImGui::Button(T("Schliessen"), ImVec2(breit, 0)) ||
        (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen("###katze_innen")))
        schliessen = true;

    // Verschachtelt, damit die Optionen darunter offen bleiben.
    ImGui::SetNextWindowSize(ImVec2(500 * s, 640 * s), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    const std::string kt = std::string(T("ueber die Katze")) + "###katze_innen";
    bool katzeOffen = true;  // X oben rechts
    if (ImGui::BeginPopupModal(kt.c_str(), &katzeOffen, ImGuiWindowFlags_NoSavedSettings)) {
        bool zu = false;
        ZeichneKatze(zu);
        if (zu) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void App::ZeichneDialog() {
    if (dialoge_.empty()) return;
    Dialog& d = dialoge_.front();
    const std::string titel = d.titel.empty() ? std::string("RAW-SNES") : d.titel;
    const std::string id = titel + "###dialog" + std::to_string(d.nummer);
    if (!d.geoeffnet) {
        ImGui::OpenPopup(id.c_str());
        d.geoeffnet = true;
    }
    const float s = dpi_;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings;
    if (d.art == Dialog::Art::Katze) ImGui::SetNextWindowSize(ImVec2(500 * s, 640 * s), ImGuiCond_Appearing);
    else flags |= ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * s, 14 * s));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, V(kDark));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, V(kLila));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, V(kLila));
    std::function<void()> danach;
    bool schliessen = false;
    // Optionen, Katze und Ko-fi bekommen ein X oben rechts. Das X schliesst
    // wie "Abbrechen" -- ohne Folgeaktion.
    bool offen = true;
    const bool mitX = d.art == Dialog::Art::Optionen || d.art == Dialog::Art::Katze || d.art == Dialog::Art::Kofi;

    if (ImGui::BeginPopupModal(id.c_str(), mitX ? &offen : nullptr, flags)) {
        const float knopf = 96 * s;
        auto Knoepfe = [&](const char* a, const char* b) -> int {
            const float gesamt = b ? knopf * 2 + 8 * s : knopf;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - gesamt));
            int r = 0;
            if (FarbKnopf(a, kLila, ImVec2(knopf, 0))) r = 1;
            if (b) {
                ImGui::SameLine(0, 8 * s);
                if (ImGui::Button(b, ImVec2(knopf, 0))) r = 2;
            }
            return r;
        };

        switch (d.art) {
            case Dialog::Art::Info:
            case Dialog::Art::Warnung:
            case Dialog::Art::Fehler:
            case Dialog::Art::Frage: {
                const ImU32 marke = d.art == Dialog::Art::Warnung ? kJustUnlocked
                                  : d.art == Dialog::Art::Fehler ? kFehlerRot : 0;
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460 * s);
                if (marke) ImGui::PushStyleColor(ImGuiCol_Text, V(marke));
                ImGui::TextUnformatted(d.text.c_str());
                if (marke) ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
                ImGui::Dummy(ImVec2(0, 8 * s));
                if (d.art == Dialog::Art::Frage) {
                    const int r = Knoepfe(T("Ja"), T("Nein"));
                    if (r == 1) { danach = d.ja; schliessen = true; }
                    if (r == 2 || ImGui::IsKeyPressed(ImGuiKey_Escape)) { danach = d.nein; schliessen = true; }
                } else if (Knoepfe("OK", nullptr) == 1 || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                           ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    danach = d.ja;
                    schliessen = true;
                }
                break;
            }
            case Dialog::Art::Katze:
                ZeichneKatze(schliessen);
                break;
            case Dialog::Art::Optionen:
                ZeichneOptionen(schliessen);
                break;
            case Dialog::Art::Kofi: {
                // Kein Link im Text: erst OK oeffnet die Seite.
                ImGui::Dummy(ImVec2(300 * s, 0));
                Zentriert(f_.kursiv ? f_.kursiv : f_.ui, 13, kTextMain, T(kKatzeHinweis));
                ImGui::Dummy(ImVec2(0, 8 * s));
                if (ImGui::Button(T("ueber die Katze"), ImVec2(-FLT_MIN, 0))) ImGui::OpenPopup("###katze_kofi");
                ImGui::Dummy(ImVec2(0, 4 * s));
                const int r = Knoepfe("OK", T("Abbrechen"));
                if (r == 1) { plattform::UrlOeffnen(kKofiUrl); schliessen = true; }
                if (r == 2 || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen("###katze_kofi")))
                    schliessen = true;
                ImGui::SetNextWindowSize(ImVec2(500 * s, 640 * s), ImGuiCond_Appearing);
                ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
                const std::string kt = std::string(T("ueber die Katze")) + "###katze_kofi";
                bool katzeOffen = true;  // X oben rechts
                if (ImGui::BeginPopupModal(kt.c_str(), &katzeOffen, ImGuiWindowFlags_NoSavedSettings)) {
                    bool zu = false;
                    ZeichneKatze(zu);
                    if (zu) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                break;
            }
        }
        if (schliessen) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    // Per X geschlossen: ImGui hat das Fenster schon zu, nur noch austragen.
    if (mitX && !offen) {
        schliessen = true;
        danach = nullptr;
    }

    if (schliessen) {
        dialoge_.pop_front();
        if (danach) danach();
    }
}

// ============================================================================
// Badges -- Download in eigenem Thread, Ablage in badge_cache neben der Exe.
// ============================================================================

App::Badge* App::BadgeHolen(const std::string& name) {
    if (name.empty()) return nullptr;
    auto it = badges_.find(name);
    if (it != badges_.end()) return &it->second;
    Badge& b = badges_[name];
    {
        std::lock_guard<std::mutex> l(badgeMutex_);
        badgeWarteschlange_.push_back(name);
    }
    badgeCv_.notify_one();
    return &b;
}

void App::BadgeThread() {
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const std::string ordner = plattform::ExeOrdner() + "/badge_cache";
    std::error_code ec;
    fs::create_directories(fs::u8path(ordner), ec);
    for (;;) {
        std::string name;
        {
            std::unique_lock<std::mutex> l(badgeMutex_);
            badgeCv_.wait(l, [this]() { return badgeEnde_ || !badgeWarteschlange_.empty(); });
            if (badgeEnde_) break;
            name = badgeWarteschlange_.front();
            badgeWarteschlange_.pop_front();
        }
        const std::string lokal = ordner + "/" + name + ".png";
        std::string png = LeseDatei(lokal);
        if (png.empty()) {
            std::string body;
            if (http_get("media.retroachievements.org", "/Badge/" + name + ".png", 8000, RA_USER_AGENT, &body)) {
                std::ofstream f(fs::u8path(lokal), std::ios::binary);
                f.write(body.data(), (std::streamsize)body.size());
                png = std::move(body);
            }
        }
        BadgeErgebnis e;
        e.name = name;
        e.ok = !png.empty() && bilder::PngDekodieren(png.data(), png.size(), &e.w, &e.h, &e.rgba);
        e.png = std::move(png);
        std::lock_guard<std::mutex> l(badgeMutex_);
        badgeFertig_.push_back(std::move(e));
    }
    ::CoUninitialize();
}

// ============================================================================
// Zeichnen
// ============================================================================

void App::Zeichnen() {
    Takt();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ZeichneIn(vp->Pos, vp->Size, false);
    ZeichneDialog();
}

void App::ZeichneIn(ImVec2 pos, ImVec2 groesse, bool nurListeUndLog) {
    nurListe_ = nurListeUndLog;
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(groesse);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::Begin(eingebettet_ ? "##rawsnes" : "##haupt", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);

    const float s = dpi_;
    if (!eingebettet_) ZeichneKopfleiste();
    const ImVec2 rest = ImGui::GetContentRegionAvail();
    if (nurListe_) {
        ZeichneHauptbereich(rest);
    } else {
        // Links die Konsole (Modul, Start, Fortschritt, Konto, Live-Werte),
        // rechts die Reiter. Die Seitenleiste waechst mit sehr breiten
        // Fenstern nicht mit.
        const float seite = std::min(300.0f * s, std::max(240.0f * s, rest.x * 0.30f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, V(kSeite));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * s, 14 * s));
        ImGui::BeginChild("seite", ImVec2(seite, rest.y), ImGuiChildFlags_AlwaysUseWindowPadding);
        ZeichneSeite();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 0);
        ZeichneHauptbereich(ImVec2(rest.x - seite, rest.y));
    }

    ImGui::End();
}

void App::LoginSetzen(const std::string& user, const std::string& token) {
    std::snprintf(userBuf_, sizeof(userBuf_), "%s", user.c_str());
    token_ = token;
    loginOk_ = !token.empty();
    loginLaeuft_ = false;
    loginStatusText_ = loginOk_ ? "\xE2\x9C\x93 " + user : std::string();
    loginStatusKey_ = "nicht angemeldet";
    worker_->setAnmeldung(loginOk_ ? user : std::string(), token_);
}

App::StatusDaten App::StatusHolen() const {
    StatusDaten s;
    s.angemeldet = !token_.empty();
    s.verbunden = connected_;
    s.monitorLaeuft = monitoring_;
    s.monitorMoeglich = monitoring_ || (loginOk_ && connected_ && !laufendeRom_.empty());
    s.spiel = gameStatus_;
    if (!connected_) s.hinweis = T("SD2SNES/FXPak nicht verbunden");
    else if (laufendeRom_.empty()) s.hinweis = T("Menue des SD2SNES/FXPak -- ein Spiel starten.");
    s.gesamt = (int)ach_.size();
    for (const auto& z : ach_)
        if (z.state == AchState::Unlocked || z.state == AchState::JustUnlocked) ++s.frei;
    return s;
}

void App::ZeichneAutostart() {
    if (Haken((std::string(T("Automatisch starten")) + "##snes").c_str(), &autostart_)) {
        worker_->setAutostart(autostart_);
        saveConfig();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", T("Startet die Ueberwachung von selbst, sobald am SNES ein Spiel laeuft, "
                                  "und wechselt mit dem Spiel."));
}

void App::ZeichneProbeKnopf(float breite) {
    if (ImGui::Button((std::string(T("Freischalt-Anzeige testen")) + "##snes").c_str(), ImVec2(breite, 0)))
        probeEinblendung();
}

void App::Takt() {
    // Auftraege aus Worker und Hilfsthreads.
    {
        std::vector<std::function<void()>> q;
        {
            std::lock_guard<std::mutex> l(postMutex_);
            q.swap(postQueue_);
        }
        for (auto& f : q) f();
    }
    if (const int neu = g_neuerBuild.load(); neu > 0 && !g_updateGezeigt) {
        g_updateGezeigt = true;
        Frage(T("Update verfuegbar"),
              Fmt(T("Eine neue Version ist verfuegbar (Build %1, du hast Build %2).\n\nDownload-Seite oeffnen?"),
                  {i18n::Zahl(neu), i18n::Zahl(RAWSNES_BUILD)}),
              []() { plattform::UrlOeffnen(kReleaseUrl); }, {});
    }
    // Fertig geladene Badges als Textur hochladen (nur hier: OpenGL-Thread).
    {
        std::vector<BadgeErgebnis> fertig;
        {
            std::lock_guard<std::mutex> l(badgeMutex_);
            fertig.swap(badgeFertig_);
        }
        for (auto& e : fertig) {
            Badge& b = badges_[e.name];
            b.png = std::move(e.png);
            if (e.ok) {
                b.tex = bilder::Hochladen(e.rgba.data(), e.w, e.h, false);
                b.zustand = Badge::Zustand::Fertig;
            } else {
                b.zustand = Badge::Zustand::Fehlt;
            }
        }
    }
}

// ============================================================================
// Kopfleiste
// ============================================================================

void App::ZeichneKopfleiste() {
    const float s = dpi_;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float breite = ImGui::GetContentRegionAvail().x;
    const float hoehe = (logo_.ok() ? 104 : 58) * s;
    const ImVec2 p1(p0.x + breite, p0.y + hoehe);

    // Gehaeuse: hell, darunter die lila Kante der Schiebeschalter.
    dl->AddRectFilled(p0, p1, kLight);
    dl->AddRectFilled(ImVec2(p0.x, p1.y - 3 * s), p1, kLila);

    // Mitte, untereinander: Katze, Name, die vier Tasten mit Build-Zeile
    {
        const float mitte = p0.x + breite * 0.5f;
        float y = p0.y + 6 * s;
        if (logo_.ok()) {
            const float logoH = 46 * s, logoW = logoH * logo_.w / logo_.h;
            dl->AddImage((ImTextureID)(intptr_t)logo_.id, ImVec2(mitte - logoW * 0.5f, y),
                         ImVec2(mitte + logoW * 0.5f, y + logoH), ImVec2(0, 0), ImVec2(logo_.u, logo_.v));
            y += logoH + 2 * s;
        }
        const float nameW = f_.fett->CalcTextSizeA(22 * s, FLT_MAX, 0.0f, "RAW-SNES").x;
        dl->AddText(f_.fett, 22 * s, ImVec2(mitte - nameW * 0.5f, y), kLightText, "RAW-SNES");
        y += 28 * s;
        const std::string unter = "SD2SNES / FXPak Pro  \xC2\xB7  " RAWSNES_VERSION " (Build " +
                                  std::to_string(RAWSNES_BUILD) + ")";
        const float unterW = 58 * s + f_.ui->CalcTextSizeA(10 * s, FLT_MAX, 0.0f, unter.c_str()).x;
        const float x = mitte - unterW * 0.5f;
        const ImU32 tasten[4] = {kTasteRot, kTasteGelb, kTasteBlau, kTasteGruen};
        for (int i = 0; i < 4; ++i)
            dl->AddCircleFilled(ImVec2(x + 5 * s + i * 13 * s, y + 6 * s), 4.5f * s, tasten[i], 16);
        dl->AddText(f_.ui, 10 * s, ImVec2(x + 58 * s, y), kLightLeise, unter.c_str());
    }

    // Links: Optionen, Ko-fi. Rechts: Konto-Anzeige.
    ImGui::PushFont(f_.ui, 12);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * s, 6 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * s);
    ImGui::PushStyleColor(ImGuiCol_Button, V(kLightRille));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, V(Rgb(0xadacb8)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, V(Rgb(0x9d9ca8)));
    ImGui::PushStyleColor(ImGuiCol_Text, V(kLightText));
    const std::string opt = std::string(T("Optionen")) + "###optionen";
    const float bOpt = ImGui::CalcTextSize(T("Optionen")).x + 24 * s;
    const float bKofi = ImGui::CalcTextSize("Ko-fi").x + 24 * s;
    const float by = p0.y + (hoehe - 3 * s - ImGui::GetFrameHeight()) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(p0.x + 16 * s, by));
    if (ImGui::Button(opt.c_str(), ImVec2(bOpt, 0))) DialogAuf(Dialog::Art::Optionen);
    ImGui::SameLine(0, 8 * s);
    if (ImGui::Button("Ko-fi###kofi", ImVec2(bKofi, 0))) DialogAuf(Dialog::Art::Kofi);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(3);

    // Konto als kleine Plakette am rechten Rand
    {
        const std::string name = loginOk_ ? std::string(userBuf_) : std::string(T("nicht angemeldet"));
        const ImVec2 ts = ImGui::CalcTextSize(name.c_str());
        const float pw = ts.x + 30 * s, ph = ImGui::GetFrameHeight();
        const ImVec2 a(p1.x - 16 * s - pw, by), b(p1.x - 16 * s, by + ph);
        dl->AddRectFilled(a, b, Rgb(0xc6c5ce), ph * 0.5f);
        dl->AddCircleFilled(ImVec2(a.x + 13 * s, a.y + ph * 0.5f), 4.5f * s, loginOk_ ? kTasteGruen : kLightLeise, 16);
        dl->AddText(ImVec2(a.x + 23 * s, a.y + (ph - ts.y) * 0.5f), kLightText, name.c_str());
    }
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y));
}

// ============================================================================
// Seitenleiste
// ============================================================================

void App::Abschnitt(const char* titel) {
    const float s = dpi_;
    ImGui::Dummy(ImVec2(0, 4 * s));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(f_.fett, 11);
    const ImVec2 ts = ImGui::CalcTextSize(titel);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(p, kLilaHell, titel);
    dl->AddLine(ImVec2(p.x + ts.x + 8 * s, p.y + ts.y * 0.55f), ImVec2(p.x + w, p.y + ts.y * 0.55f),
                Rgb(0x4a4858), 1.0f);
    ImGui::Dummy(ImVec2(w, ts.y + 2 * s));
    ImGui::PopFont();
}

void App::ZeichneSeite() {
    const float s = dpi_;
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(f_.ui, 12);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6 * s, 6 * s));

    ZeichneCartridge(w);
    if (abdeckungDa_ && !abdeckung_.voll()) ZeichneAbdeckung(w);

    // Fortschritt beim Lesen der ROM
    if (hashFortschritt_ >= 0.0f && gameStatus_.empty()) {
        char t[64];
        std::snprintf(t, sizeof(t), "%s %d%%", T("erkenne Spiel..."), (int)std::lround(hashFortschritt_ * 100));
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, V(kLila));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, V(kPanel));
        ImGui::ProgressBar(hashFortschritt_, ImVec2(w, 16 * s), t);
        ImGui::PopStyleColor(2);
    }

    // Start/Stopp ueber die ganze Breite
    {
        const bool spielDa = connected_ && !laufendeRom_.empty();
        const bool aktiv = monitoring_ || (loginOk_ && spielDa);
        ImGui::BeginDisabled(!aktiv);
        ImGui::PushFont(f_.fett, 13);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f * s);
        bool klick;
        if (monitoring_)
            klick = FarbKnopf((std::string("\xE2\x96\xA0  ") + T("Monitor stoppen") + "###monitor").c_str(), kTasteRot,
                              ImVec2(w, 36 * s));
        else
            klick = FarbKnopf((std::string("\xE2\x96\xB6  ") + T("Monitor starten") + "###monitor").c_str(), kLila,
                              ImVec2(w, 36 * s));
        ImGui::PopStyleVar();
        ImGui::PopFont();
        if (klick) onToggleMonitor();
        ImGui::EndDisabled();
        if (!aktiv && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", !loginOk_ ? T("Erst bei RetroAchievements anmelden.")
                                              : T("Erst ein Spiel am SD2SNES/FXPak starten."));
    }
    ZeichneAutostart();
    {
        ImGui::BeginDisabled(!kHardcoreErlaubt || !hardcoreEnabled_);
        bool hc = kHardcoreErlaubt && hardcore_;
        if (Haken(T("Hardcore-Modus"), &hc)) {
            hardcore_ = hc;
            cfg_.hardcore = hc;
            worker_->setHardcore(hc);
            saveConfig();
        }
        ImGui::EndDisabled();
        if (!kHardcoreErlaubt && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", T("RetroAchievements nimmt Hardcore von diesem Geraet noch nicht an. "
                                      "Freischaltungen werden als Softcore gebucht."));
        Txt(f_.ui, 10, kTextMuted, kHardcoreErlaubt ? T("(erfordert: Savestates und Cheats im SD2SNES/FXPak-Menue aus)")
                                                    : T("(von RetroAchievements noch nicht anerkannt)"));
    }

    Abschnitt(T("Fortschritt"));
    ZeichneFortschritt(w);

    if (!eingebettet_) {
        Abschnitt(T("Konto"));
        ZeichneKonto(w);
    }

    Abschnitt(T("Live-Werte"));
    ZeichneLiveWerte(w, std::max(60.0f * s, ImGui::GetContentRegionAvail().y));

    ImGui::PopStyleVar();
    ImGui::PopFont();
}

std::string App::AbdeckungText(bool lang) const {
    const Abdeckung& a = abdeckung_;
    std::string t = Fmt(T("%1 von %2 offenen Achievements werden ueberwacht."),
                        {i18n::Zahl(a.aktiv), i18n::Zahl(a.offen)});
    if (a.ohnePlatz) {
        if (lang)
            t += std::string("\n\n") +
                 T("Bei diesem Spiel mit Zusatzchip reicht der Platz nicht fuer alle Achievements. Das ist eine Grenze der "
                   "Originalfirmware des Moduls. "
                   "Die uebrigen werden erst ueberwacht, wenn durch Freischaltungen Platz frei wird. Was du vorher "
                   "bei ihnen erreichst, zaehlt nicht und wird auch nicht nachtraeglich freigeschaltet. In der Liste "
                   "sind sie als \"nicht ueberwacht\" markiert.");
        else
            t += std::string("\n") + T("Achievements mit \"nicht ueberwacht\" zaehlen gerade nicht.");
        t += std::string(lang ? "\n\n" : "\n") +
             T("Volle Unterstuetzung gibt es in der Master-Version. Sie ist fertig, wurde aber aus Lizenzgruenden "
               "noch nicht veroeffentlicht.");
    }
    if (a.nichtAuswertbar)
        t += std::string(lang ? "\n\n" : "\n") +
             Fmt(T("%1 Achievement(s) kann RAW-SNES hier nicht auswerten "
                   "(Adressen nicht lesbar oder unbekannte Schreibweise)."),
                 {i18n::Zahl(a.nichtAuswertbar)});
    return t;
}

// Roter Kasten unter der Cartridge, solange das Spiel nur teilweise
// ueberwacht wird.
void App::ZeichneAbdeckung(float breite) {
    const float s = dpi_;
    const float rand = 8 * s;
    const std::string kopf = T("Nur teilweise unterstuetzt");
    const std::string text = AbdeckungText(false);
    const float innen = breite - 2 * rand;
    const ImVec2 gk = f_.fett->CalcTextSizeA(12 * s, FLT_MAX, innen, kopf.c_str());
    const ImVec2 gt = f_.ui->CalcTextSizeA(11 * s, FLT_MAX, innen, text.c_str());
    const float hoehe = rand + gk.y + 4 * s + gt.y + rand;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, ImVec2(p.x + breite, p.y + hoehe), kTasteRot, 6 * s);
    dl->AddText(f_.fett, 12 * s, ImVec2(p.x + rand, p.y + rand), Rgb(0xffffff), kopf.c_str(), nullptr, innen);
    dl->AddText(f_.ui, 11 * s, ImVec2(p.x + rand, p.y + rand + gk.y + 4 * s), Rgb(0xffffff), text.c_str(), nullptr,
                innen);
    ImGui::Dummy(ImVec2(breite, hoehe));
}

// Das Modul als SNES-Cartridge: graues Gehaeuse mit Griffrillen, darauf das
// Etikett mit dem laufenden Spiel.
void App::ZeichneCartridge(float breite) {
    const float s = dpi_;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float h = 150 * s;
    const ImVec2 p1(p0.x + breite, p0.y + h);
    const float deck = connected_ ? 1.0f : 0.45f;
    auto D = [&](ImU32 c) { return MitDeckkraft(c, deck); };

    // Gehaeuse mit abgeschraegten oberen Ecken wie der Steckplatz-Rand
    const float r = 10 * s;
    dl->AddRectFilled(ImVec2(p0.x, p0.y + 8 * s), p1, D(kKarte), r);
    dl->AddRectFilled(ImVec2(p0.x + 18 * s, p0.y), ImVec2(p1.x - 18 * s, p0.y + 16 * s), D(kKarte), 6 * s);
    // Griffrillen links und rechts
    for (int i = 0; i < 5; ++i) {
        const float y = p0.y + 34 * s + i * 9 * s;
        dl->AddRectFilled(ImVec2(p0.x + 7 * s, y), ImVec2(p0.x + 15 * s, y + 4 * s), D(Rgb(0x75747f)), 2 * s);
        dl->AddRectFilled(ImVec2(p1.x - 15 * s, y), ImVec2(p1.x - 7 * s, y + 4 * s), D(Rgb(0x75747f)), 2 * s);
    }
    // Etikett
    const ImVec2 e0(p0.x + 24 * s, p0.y + 18 * s), e1(p1.x - 24 * s, p1.y - 34 * s);
    dl->AddRectFilled(e0, e1, D(kEtikett), 4 * s);
    dl->AddRectFilled(e0, ImVec2(e1.x, e0.y + 7 * s), D(kLila), 4 * s, ImDrawFlags_RoundCornersTop);

    std::string titel, unter;
    if (!connected_) {
        titel = T("Kein SD2SNES/FXPak");
        unter = T("USB-Kabel einstecken, Konsole an");
    } else if (!romBekannt_) {
        titel = T("frage Modul...");
    } else if (laufendeRom_.empty()) {
        titel = T("Menue");
        unter = T("Spiel am SNES starten");
    } else {
        titel = gameStatus_.empty() ? NurName(laufendeRom_) : gameStatus_;
        unter = gameStatus_.empty() ? std::string(T("noch nicht erkannt")) : NurName(laufendeRom_);
    }
    const float tw = e1.x - e0.x - 16 * s;
    dl->PushClipRect(e0, e1, true);
    dl->AddText(f_.fett, 14 * s, ImVec2(e0.x + 8 * s, e0.y + 14 * s), D(kLightText), titel.c_str(), nullptr, tw);
    if (!unter.empty())
        dl->AddText(f_.ui, 10 * s, ImVec2(e0.x + 8 * s, e1.y - 18 * s), D(kLightLeise), unter.c_str(), nullptr, tw);
    dl->PopClipRect();

    // Unter dem Etikett: Lampe und Anschluss
    {
        const float ly = p1.y - 17 * s;
        ImU32 lampe = kTextMuted;
        std::string zeile;
        if (connected_) {
            lampe = monitoring_ ? kTasteGruen : kTasteGelb;
            zeile = geraetText_.empty() ? connPort_ : connPort_ + "  \xC2\xB7  " + geraetText_;
        } else {
            const float a = 0.45f + 0.55f * (0.5f + 0.5f * std::sin((float)JetztMs() / 300.0f));
            lampe = MitDeckkraft(kTasteGelb, a);
            zeile = std::string(T("suche SD2SNES/FXPak")) + std::string((size_t)((JetztMs() / 400) % 4), '.');
        }
        dl->AddCircleFilled(ImVec2(p0.x + 30 * s, ly), 4.5f * s, lampe, 16);
        dl->PushClipRect(ImVec2(p0.x, ly - 10 * s), ImVec2(p1.x - 20 * s, ly + 10 * s), true);
        dl->AddText(f_.ui, 10 * s, ImVec2(p0.x + 40 * s, ly - 7 * s), Rgb(0xf4f3f8), zeile.c_str());
        dl->PopClipRect();
    }
    ImGui::Dummy(ImVec2(breite, h + 4 * s));
}

void App::ZeichneFortschritt(float breite) {
    const float s = dpi_;
    if (ach_.empty()) {
        Txt(f_.ui, 11, kTextMuted, keinSet_ == 1   ? T("kein Achievement-Set vorhanden")
                                   : keinSet_ == 2 ? T("Fassung nicht unterstuetzt")
                                                   : T("noch kein Spiel geladen"));
        return;
    }
    int frei = 0, punkte = 0, punkteFrei = 0;
    for (const auto& z : ach_) {
        punkte += z.points;
        if (z.state == AchState::Unlocked || z.state == AchState::JustUnlocked) {
            ++frei;
            punkteFrei += z.points;
        }
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const std::string gross = std::to_string(frei) + " / " + std::to_string(ach_.size());
    dl->AddText(f_.fett, 24 * s, p, kTextMain, gross.c_str());
    const float gw = f_.fett->CalcTextSizeA(24 * s, FLT_MAX, 0.0f, gross.c_str()).x;
    dl->AddText(f_.ui, 11 * s, ImVec2(p.x + gw + 8 * s, p.y + 10 * s), kTextMuted, T("Achievements"));
    ImGui::Dummy(ImVec2(breite, 30 * s));

    const ImVec2 b0 = ImGui::GetCursorScreenPos(), b1(b0.x + breite, b0.y + 8 * s);
    dl->AddRectFilled(b0, b1, kLockedIconBg, 4 * s);
    if (ach_.size()) {
        const float anteil = (float)frei / (float)ach_.size();
        if (anteil > 0) dl->AddRectFilled(b0, ImVec2(b0.x + breite * anteil, b1.y), kTasteGelb, 4 * s);
    }
    ImGui::Dummy(ImVec2(breite, 8 * s));
    Txt(f_.ui, 11, kTextMuted, Fmt(T("%1 von %2 Punkten"), {i18n::Zahl(punkteFrei), i18n::Zahl(punkte)}));
}

void App::ZeichneKonto(float breite) {
    const float s = dpi_;
    if (loginOk_) {
        const float x0 = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        Txt(f_.fett, 12, kUnlocked, loginStatusText_.empty() ? std::string(userBuf_) : loginStatusText_);
        const char* ab = T("Abmelden");
        const float bw = ImGui::CalcTextSize(ab).x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x0 + breite - bw));
        if (ImGui::Button(ab)) onLogout();
        (void)s;
        return;
    }
    ImGui::SetNextItemWidth(breite);
    ImGui::InputTextWithHint("##user", T("RA-Benutzername"), userBuf_, sizeof(userBuf_));
    ImGui::SetNextItemWidth(breite);
    const bool enter = ImGui::InputTextWithHint("##pass", T("RA-Passwort"), passBuf_, sizeof(passBuf_),
                                                ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::BeginDisabled(loginLaeuft_);
    if (FarbKnopf(T("Anmelden"), kLila, ImVec2(breite, 0)) || (enter && !loginLaeuft_)) onLogin();
    ImGui::EndDisabled();
    if (Haken(T("Eingeloggt bleiben"), &rememberLogin_)) saveConfig();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", T("Benutzername und Anmeldeschluessel speichern (das Passwort wird nicht gespeichert)"));
    const std::string status = loginStatusText_.empty() ? T(loginStatusKey_.c_str()) : loginStatusText_;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + breite);
    Txt(f_.ui, 10, kTextMuted, status);
    ImGui::PopTextWrapPos();
}

void App::ZeichneLiveWerte(float breite, float hoehe) {
    const float s = dpi_;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(Rgb(0x15131d)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * s, 6 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 8 * s);
    ImGui::BeginChild("live", ImVec2(breite, hoehe), ImGuiChildFlags_AlwaysUseWindowPadding);
    const float px = 11.0f * 96.0f / 72.0f;
    if (live_.empty()) {
        Txt(f_.mono, px, Rgb(0x8fe0a8), T("RAM: -"));
    } else {
        const unsigned long long now = JetztMs();
        const float zeile = px * s * 1.5f;
        const float spalte = ImGui::GetContentRegionAvail().x / 2.0f;
        const ImVec2 o = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        int i = 0;
        for (const auto& [slot, z] : live_) {
            (void)slot;
            const float x = o.x + (i % 2) * spalte;
            const float y = o.y + (i / 2) * zeile;
            const std::string adr = SnesAdresse(z.addr);
            char wert[4];
            if (z.value < 0) std::snprintf(wert, sizeof(wert), "--");
            else std::snprintf(wert, sizeof(wert), "%02X", z.value);
            dl->AddText(f_.mono, px * s, ImVec2(x, y), Rgb(0x7a7398), adr.c_str());
            const float aw = f_.mono->CalcTextSizeA(px * s, FLT_MAX, 0.0f, adr.c_str()).x + 6 * s;
            dl->AddText(f_.monoFett ? f_.monoFett : f_.mono, px * s, ImVec2(x + aw, y),
                        FarbeNachAlter(z.lastChangeMs, now), wert);
            ++i;
        }
        ImGui::Dummy(ImVec2(1, ((i + 1) / 2) * zeile));
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

// ============================================================================
// Hauptbereich mit Reitern
// ============================================================================

void App::ZeichneHauptbereich(ImVec2 groesse) {
    const float s = dpi_;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(kDark));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * s, 14 * s));
    ImGui::BeginChild("haupt", groesse, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushFont(f_.ui, 12);
    const float w = ImGui::GetContentRegionAvail().x;

    // Reiter als Pillen
    {
        const float zeileX = ImGui::GetCursorPosX();
        int frei = 0;
        for (const auto& z : ach_) if (z.state == AchState::Unlocked || z.state == AchState::JustUnlocked) ++frei;
        const std::string ta = ach_.empty() ? std::string(T("Achievements"))
                                            : Fmt(T("Achievements %1/%2"), {i18n::Zahl(frei), i18n::Zahl((long long)ach_.size())});
        const std::string tl = lbs_.empty() ? std::string(T("Leaderboards"))
                                            : std::string(T("Leaderboards")) + " " + std::to_string(lbs_.size());
        struct R { Reiter r; std::string t; } reiter[] = {
            {Reiter::Achievements, ta}, {Reiter::Leaderboards, tl}, {Reiter::Log, T("Protokoll")}};
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14 * s, 6 * s));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14 * s);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        bool erster = true;
        for (const auto& r : reiter) {
            if (!erster) ImGui::SameLine(0, 6 * s);
            erster = false;
            const bool an = reiter_ == r.r;
            ImGui::PushStyleColor(ImGuiCol_Button, V(an ? kLila : kPanel));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, V(an ? kLila : Rgb(0x45434f)));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, V(Rgb(0x4a4190)));
            ImGui::PushStyleColor(ImGuiCol_Text, V(an ? Rgb(0xffffff) : kTextMuted));
            ImGui::PushFont(an ? f_.fett : f_.ui, 12);
            if (ImGui::Button((r.t + "###reiter" + std::to_string((int)r.r)).c_str())) reiter_ = r.r;
            ImGui::PopFont();
            ImGui::PopStyleColor(4);
        }
        // Filter rechts, nur bei Achievements
        if (reiter_ == Reiter::Achievements && !ach_.empty()) {
            struct F { Filter f; const char* t; } filter[] = {
                {Filter::Alle, T("Alle")}, {Filter::Offen, T("Offen")}, {Filter::Frei, T("Freigeschaltet")}};
            float fb = 0;
            for (const auto& f : filter) fb += ImGui::CalcTextSize(f.t).x + 20 * s + 4 * s;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8 * s, zeileX + w - fb));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * s, 6 * s));
            bool ef = true;
            for (const auto& f : filter) {
                if (!ef) ImGui::SameLine(0, 4 * s);
                ef = false;
                const bool an = filter_ == f.f;
                ImGui::PushStyleColor(ImGuiCol_Button, V(an ? Rgb(0x4c4b57) : 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, V(Rgb(0x45434f)));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, V(Rgb(0x4c4b57)));
                ImGui::PushStyleColor(ImGuiCol_Text, V(an ? kTextMain : kTextMuted));
                if (ImGui::Button((std::string(f.t) + "###filter" + std::to_string((int)f.f)).c_str())) filter_ = f.f;
                ImGui::PopStyleColor(4);
            }
            ImGui::PopStyleVar();
        }
        ImGui::PopStyleVar(3);
    }
    ImGui::Dummy(ImVec2(0, 4 * s));

    switch (reiter_) {
        case Reiter::Achievements: ZeichneAchievements(w); break;
        case Reiter::Leaderboards: ZeichneLeaderboards(w); break;
        case Reiter::Log: ZeichneLog(); break;
    }

    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void App::ZeichneAchievements(float breite) {
    const float s = dpi_;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(kDark));
    ImGui::BeginChild("raster", ImVec2(breite, 0));
    if (ach_.empty()) {
        // Leerer Zustand: grosse Cartridge-Umrisse, darunter der Hinweis
        const ImVec2 r = ImGui::GetContentRegionAvail();
        const ImVec2 o = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float cw = 120 * s, ch = 90 * s;
        const ImVec2 c0(o.x + (r.x - cw) * 0.5f, o.y + r.y * 0.30f - ch * 0.5f), c1(c0.x + cw, c0.y + ch);
        dl->AddRect(ImVec2(c0.x, c0.y + 6 * s), c1, Rgb(0x55536a), 10 * s, 0, 2.0f * s);
        dl->AddRect(ImVec2(c0.x + 14 * s, c0.y), ImVec2(c1.x - 14 * s, c0.y + 12 * s), Rgb(0x55536a), 5 * s, 0, 2.0f * s);
        dl->AddRect(ImVec2(c0.x + 20 * s, c0.y + 20 * s), ImVec2(c1.x - 20 * s, c1.y - 20 * s), Rgb(0x55536a), 4 * s, 0,
                    1.5f * s);
        const ImU32 tasten[4] = {kTasteRot, kTasteGelb, kTasteBlau, kTasteGruen};
        for (int i = 0; i < 4; ++i)
            dl->AddCircleFilled(ImVec2(o.x + r.x * 0.5f + (i - 1.5f) * 16 * s, c1.y + 22 * s), 4.5f * s,
                                MitDeckkraft(tasten[i], 0.7f), 16);
        ImGui::SetCursorScreenPos(ImVec2(o.x, c1.y + 44 * s));
        // Spiel ohne Achievements: Meldung gelb und sanft pulsierend, damit sie
        // nicht wie "noch kein Spiel" aussieht.
        const float puls = 0.45f + 0.55f * (0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 6.2832f));
        Zentriert(f_.ui, 12, (keinSet_ && !monitoring_) ? MitDeckkraft(kTasteGelb, puls) : kTextMuted,
                  monitoring_   ? T("lade Achievements...")
                  : keinSet_ == 1 ? T("Fuer dieses Spiel ist bei RetroAchievements kein Achievement-Set vorhanden.")
                  : keinSet_ == 2 ? T("Diese Fassung des Spiels unterstuetzt RetroAchievements nicht.")
                                  : T("Noch kein Spiel geladen. Sobald am SNES ein Spiel laeuft,\n"
                                      "erscheinen hier seine Achievements."));
    } else {
        const float abstand = 10 * s;
        const float minBreite = 270 * s;
        const float voll = ImGui::GetContentRegionAvail().x;
        const int spalten = std::max(1, (int)((voll + abstand) / (minBreite + abstand)));
        const float kw = (voll - abstand * (spalten - 1)) / spalten;
        const float kh = 88 * s;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(abstand, abstand));
        int n = 0;
        for (auto& z : ach_) {
            const bool frei = z.state == AchState::Unlocked || z.state == AchState::JustUnlocked;
            if (filter_ == Filter::Offen && frei) continue;
            if (filter_ == Filter::Frei && !frei) continue;
            if (n % spalten) ImGui::SameLine(0, abstand);
            ZeichneKachel(z, ImVec2(kw, kh));
            ++n;
        }
        ImGui::PopStyleVar();
        if (n == 0) Txt(f_.ui, 12, kTextMuted, T("Keine Achievements in dieser Auswahl."));
        ImGui::Dummy(ImVec2(0, 4 * s));
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void App::ZeichneKachel(AchZeile& z, ImVec2 g) {
    const float s = dpi_;
    ImGui::PushID((int)(z.id & 0x7fffffff));
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##kachel", g);
    const bool ueber = ImGui::IsItemHovered();
    const bool klick = ImGui::IsItemActivated();
    const bool doppel = ueber && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    ImGui::PopID();
    const ImVec2 p1(p0.x + g.x, p0.y + g.y);

    ImU32 akzent = kTextMuted;
    float deck = 1.0f;
    std::string marke;
    switch (z.state) {
        case AchState::Unlocked:     akzent = kUnlocked; break;
        case AchState::JustUnlocked: akzent = kJustUnlocked; marke = T("neu!"); break;
        case AchState::Locked:       akzent = kTextMuted; deck = 0.8f; break;
        case AchState::Unsupported:  akzent = kTasteRot; deck = 0.55f; marke = T("nicht lesbar"); break;
        case AchState::Waiting:      akzent = kWaiting; deck = 0.9f; marke = T("wartet"); break;
    }
    if (z.ohnePlatz && z.state != AchState::Unlocked && z.state != AchState::JustUnlocked) {
        akzent = kTasteRot;
        deck = 0.55f;
        marke = T("nicht ueberwacht");
    }
    const bool frei = z.state == AchState::Unlocked || z.state == AchState::JustUnlocked;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, MitDeckkraft(ueber ? Rgb(0x34333d) : kPanel, deck), 8 * s);
    if (selectedId_ == z.id) dl->AddRect(p0, p1, kLilaHell, 8 * s, 0, 1.5f * s);
    else if (frei) dl->AddRect(p0, p1, MitDeckkraft(akzent, 0.55f), 8 * s, 0, 1.0f);

    // Badge 56x56 links, freigeschaltete mit farbigem Rand
    const ImVec2 b0(p0.x + 12 * s, p0.y + (g.y - 56 * s) * 0.5f), b1(b0.x + 56 * s, b0.y + 56 * s);
    dl->AddRectFilled(b0, b1, MitDeckkraft(kLockedIconBg, deck), 6 * s);
    if (!z.badge.empty()) {
        Badge* b = BadgeHolen(frei ? z.badge : z.badge + "_lock");
        if (b && b->tex.ok())
            dl->AddImageRounded((ImTextureID)(intptr_t)b->tex.id, b0, b1, ImVec2(0, 0), ImVec2(b->tex.u, b->tex.v),
                                MitDeckkraft(IM_COL32_WHITE, deck), 6 * s);
    }
    if (frei) dl->AddRect(b0, b1, akzent, 6 * s, 0, 2.0f * s);

    // Punkte als Plakette oben rechts
    const std::string punkte = Fmt(T("%1 Pkt"), {i18n::Zahl(z.points)});
    const float pw = f_.fett->CalcTextSizeA(10 * s, FLT_MAX, 0.0f, punkte.c_str()).x + 14 * s;
    const ImVec2 q0(p1.x - 10 * s - pw, p0.y + 10 * s), q1(p1.x - 10 * s, p0.y + 28 * s);
    dl->AddRectFilled(q0, q1, MitDeckkraft(frei ? akzent : Rgb(0x4a4858), deck), 9 * s);
    dl->AddText(f_.fett, 10 * s, ImVec2(q0.x + 7 * s, q0.y + 3 * s),
                frei ? Rgb(0x1d1c24) : MitDeckkraft(kTextMain, deck), punkte.c_str());

    // Titel und Beschreibung
    const float tx = b1.x + 12 * s;
    const float tw = std::max(10.0f, q0.x - 8 * s - tx);
    dl->PushClipRect(ImVec2(tx, p0.y), ImVec2(tx + tw, p1.y - 8 * s), true);
    dl->AddText(f_.fett, 12 * s, ImVec2(tx, p0.y + 11 * s), MitDeckkraft(kTextMain, deck), z.title.c_str());
    dl->PopClipRect();
    dl->PushClipRect(ImVec2(tx, p0.y), ImVec2(p1.x - 12 * s, p1.y - (z.showProgress ? 18 : 8) * s), true);
    dl->AddText(f_.ui, 10 * s, ImVec2(tx, p0.y + 31 * s), MitDeckkraft(kTextMuted, deck), z.desc.c_str(), nullptr,
                p1.x - 12 * s - tx);
    dl->PopClipRect();

    // Fortschritt unten, sonst ggf. Zustandsmarke
    if (z.showProgress && z.progTarget > 1) {
        const ImVec2 f0(tx, p1.y - 14 * s), f1(p1.x - 12 * s, p1.y - 9 * s);
        dl->AddRectFilled(f0, f1, kLockedIconBg, 3 * s);
        const float anteil = std::max(0.0f, std::min(1.0f, (float)z.progCur / (float)z.progTarget));
        if (anteil > 0) dl->AddRectFilled(f0, ImVec2(f0.x + (f1.x - f0.x) * anteil, f1.y), kInProgress, 3 * s);
        const std::string t = std::to_string(z.progCur) + "/" + std::to_string(z.progTarget);
        const float w2 = f_.ui->CalcTextSizeA(9 * s, FLT_MAX, 0.0f, t.c_str()).x;
        dl->AddText(f_.ui, 9 * s, ImVec2(f1.x - w2, f0.y - 12 * s), kInProgress, t.c_str());
    } else if (!marke.empty()) {
        const float w2 = f_.fett->CalcTextSizeA(9 * s, FLT_MAX, 0.0f, marke.c_str()).x;
        dl->AddText(f_.fett, 9 * s, ImVec2(p1.x - 12 * s - w2, p1.y - 17 * s), akzent, marke.c_str());
    }

    if (ueber && !z.desc.empty()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(320 * s);
        Txt(f_.fett, 12, kTextMain, z.title);
        ImGui::TextUnformatted(z.desc.c_str());
        Txt(f_.ui, 10, kTextMuted, T("Klick = Diagnose im Protokoll, Doppelklick = auf RetroAchievements oeffnen"));
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    if (klick) {
        selectedId_ = z.id;
        worker_->diagnoseAchievement(z.id);
    }
    if (doppel) plattform::UrlOeffnen("https://retroachievements.org/achievement/" + std::to_string(z.id));
}


void App::ZeichneLeaderboards(float breite) {
    const float s = dpi_;
    Txt(f_.ui, 11, kTextMuted, kHardcoreErlaubt ? T("Eingereicht wird nur im Hardcore-Modus.")
                                                : T("Eingereicht wird nur im Hardcore-Modus -- der ist fuer dieses "
                                                    "Geraet noch gesperrt. Laeufe werden hier trotzdem angezeigt."));
    ImGui::Dummy(ImVec2(0, 4 * s));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(kDark));
    ImGui::BeginChild("lbliste", ImVec2(breite, 0));
    if (lbs_.empty()) Txt(f_.ui, 12, kTextMuted, T("keine Leaderboards"));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    for (const auto& z : lbs_) {
        std::string status;
        const size_t trenn = z.status.find('\x01');
        if (trenn != std::string::npos) status = std::string(T(z.status.substr(0, trenn).c_str())) + z.status.substr(trenn + 1);
        else status = T(z.status.c_str());
        const ImU32 farbe = z.status == "laeuft" ? kTasteGelb
                          : z.status.rfind("eingereicht", 0) == 0 ? kUnlocked : kTextMuted;
        const ImVec2 p0 = ImGui::GetCursorScreenPos(), p1(p0.x + w, p0.y + 40 * s);
        dl->AddRectFilled(p0, p1, kPanel, 8 * s);
        dl->AddRectFilled(p0, ImVec2(p0.x + 4 * s, p1.y), farbe, 8 * s, ImDrawFlags_RoundCornersLeft);
        const float sw = f_.fett->CalcTextSizeA(10 * s, FLT_MAX, 0.0f, status.c_str()).x + 16 * s;
        const ImVec2 c0(p1.x - 10 * s - sw, p0.y + 11 * s), c1(p1.x - 10 * s, p0.y + 29 * s);
        dl->AddRectFilled(c0, c1, MitDeckkraft(farbe, 0.25f), 9 * s);
        dl->AddText(f_.fett, 10 * s, ImVec2(c0.x + 8 * s, c0.y + 3 * s), farbe, status.c_str());
        dl->PushClipRect(p0, ImVec2(c0.x - 8 * s, p1.y), true);
        dl->AddText(f_.fett, 12 * s, ImVec2(p0.x + 16 * s, p0.y + 12 * s), kTextMain, z.title.c_str());
        dl->PopClipRect();
        ImGui::Dummy(ImVec2(w, 40 * s));
        ImGui::Dummy(ImVec2(0, 2 * s));
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void App::ZeichneLog() {
    const float s = dpi_;
    auto kopieren = [this]() {
        std::string alles;
        for (const auto& z : log_) { alles += z; alles += "\r\n"; }
        ImGui::SetClipboardText(alles.c_str());
    };
    if (ImGui::Button(T("Log kopieren"))) kopieren();
    ImGui::SameLine(0, 10 * s);
    ImGui::AlignTextToFramePadding();
    Txt(f_.ui, 10, kTextMuted, T("Klick auf ein Achievement schreibt seine Diagnose hierher."));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(Rgb(0x15131d)));
    ImGui::PushStyleColor(ImGuiCol_Text, V(kTextMain));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * s, 6 * s));
    ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushFont(f_.mono, 11);
    ImGuiListClipper clip;
    clip.Begin((int)log_.size());
    while (clip.Step())
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) ImGui::TextUnformatted(log_[(size_t)i].c_str());
    ImGui::PopFont();
    if (logAmEnde_) ImGui::SetScrollHereY(1.0f);
    logAmEnde_ = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f;
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_C, false))
        kopieren();
    if (ImGui::BeginPopupContextWindow("logmenue")) {
        if (ImGui::MenuItem(T("Log kopieren"))) kopieren();
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}
