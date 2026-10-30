#include "fxpak_port.h"

#include <windows.h>
#include <setupapi.h>
#include <devguid.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace {

constexpr const char* kKeineAnnahme = "nimmt keine Daten an";
constexpr const char* kBeschaeftigt = "Modul beschaeftigt (Antwort auf den vorigen Befehl steht noch aus)";

std::string NachUtf8(const wchar_t* w) {
    if (!w) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s((size_t)n - 1, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

// "USB\VID_1209&PID_5A22\..." -> vid/pid
bool LiesVidPid(const std::string& hardwareId, uint16_t* vid, uint16_t* pid) {
    std::string s = hardwareId;
    for (char& c : s) c = (char)::tolower((unsigned char)c);
    const size_t v = s.find("vid_");
    const size_t p = s.find("pid_");
    if (v == std::string::npos || p == std::string::npos) return false;
    *vid = (uint16_t)std::strtoul(s.substr(v + 4, 4).c_str(), nullptr, 16);
    *pid = (uint16_t)std::strtoul(s.substr(p + 4, 4).c_str(), nullptr, 16);
    return true;
}

// ---------------------------------------------------------------------------
// Serieller Anschluss unter Windows.
// ---------------------------------------------------------------------------
class Seriell : public Leitung {
public:
    explicit Seriell(std::string port) : port_(std::move(port)) {}
    ~Seriell() override { Schliessen(); }

    void Oeffnen() override {
        Schliessen();
        // Ab COM10 ist die \\.\-Schreibweise Pflicht.
        const std::string pfad = "\\\\.\\" + port_;
        HANDLE h = ::CreateFileA(pfad.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            char t[64];
            std::snprintf(t, sizeof(t), "Fehler %lu", ::GetLastError());
            throw std::runtime_error("Port " + port_ + " nicht zu oeffnen: " + t);
        }
        // USB-CDC: Baudrate ohne Bedeutung. DTR gesetzt -- daran erkennt das
        // Modul, dass auf der PC-Seite jemand zuhoert.
        DCB dcb = {};
        dcb.DCBlength = sizeof(dcb);
        ::GetCommState(h, &dcb);
        dcb.BaudRate = 921600;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fParity = FALSE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDsrSensitivity = FALSE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        dcb.fNull = FALSE;
        dcb.fAbortOnError = FALSE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        if (!::SetCommState(h, &dcb)) {
            ::CloseHandle(h);
            throw std::runtime_error("Port " + port_ + ": Rahmenformat nicht setzbar");
        }
        ::SetupComm(h, 1 << 16, 1 << 12);
        h_ = h;
        Zeitgrenzen(0);
    }

    void Schliessen() override {
        if (h_) {
            ::CloseHandle(h_);
            h_ = nullptr;
        }
    }

    bool Offen() const override { return h_ != nullptr; }

    void Schreiben(const uint8_t* d, size_t n) override {
        if (!h_) throw std::runtime_error("nicht verbunden");
        size_t raus = 0;
        while (raus < n) {
            DWORD k = 0;
            const bool ok = ::WriteFile(h_, d + raus, (DWORD)(n - raus), &k, nullptr) != FALSE;
            raus += k;
            if (ok && k > 0) continue;
            // Kein einziges Byte angenommen: das Modul liest gar nicht mehr.
            // Teilweise angenommen: der Befehl ist abgerissen, der Empfang des
            // Moduls ist jetzt verschoben (siehe FxPakPort::Synchronisieren).
            if (raus == 0) throw std::runtime_error(kKeineAnnahme);
            throw std::runtime_error("Senden abgebrochen nach " + std::to_string(raus) + " Byte");
        }
    }

    // Gewartet wird im Treiber (ReadFile mit Zeitgrenze), nicht mit Sleep.
    Bytes LesenBis(uint32_t anzahl, int zeitMs) override {
        Bytes aus;
        if (!h_) return aus;
        const ULONGLONG start = ::GetTickCount64();
        while (aus.size() < anzahl) {
            const long long rest = (long long)zeitMs - (long long)(::GetTickCount64() - start);
            if (rest <= 0) break;
            Zeitgrenzen((DWORD)rest);

            uint8_t puffer[4096];
            const DWORD will = (DWORD)std::min<size_t>(anzahl - aus.size(), sizeof(puffer));
            DWORD bekommen = 0;
            if (!::ReadFile(h_, puffer, will, &bekommen, nullptr)) break;
            if (bekommen == 0) break;
            aus.insert(aus.end(), puffer, puffer + bekommen);
        }
        return aus;
    }

    void EingangLeeren() override {
        if (h_) ::PurgeComm(h_, PURGE_RXCLEAR | PURGE_TXCLEAR);
    }

private:
    // Lesen: kehrt mit den vorhandenen Bytes zurueck, sonst wartet es auf das
    // erste, hoechstens `lesenMs`. Schreiben: grosszuegig -- das Modul nimmt
    // Daten nur an, solange es keinen Befehl offen hat; reisst ein Befehl
    // mittendrin ab, geraet der Empfang des Moduls aus dem Takt.
    void Zeitgrenzen(DWORD lesenMs) {
        COMMTIMEOUTS t = {};
        t.ReadIntervalTimeout = MAXDWORD;
        t.ReadTotalTimeoutMultiplier = MAXDWORD;
        t.ReadTotalTimeoutConstant = lesenMs ? lesenMs : 1;
        t.WriteTotalTimeoutConstant = 5000;
        ::SetCommTimeouts(h_, &t);
    }

    std::string port_;
    HANDLE h_ = nullptr;
};

}  // namespace

std::unique_ptr<Leitung> SerielleLeitung(const std::string& port) { return std::make_unique<Seriell>(port); }

// ---------------------------------------------------------------------------
// Protokollablauf
//
// Harte Regel: nie einen Befehl senden, solange die Antwort auf den vorigen
// aussteht. Kommen Daten, waehrend das Modul noch beschaeftigt ist, holt seine
// Firmware das USB-Paket nicht ab -- und spaeter auch nie mehr; der Empfang
// bleibt dann bis zum Ausschalten der Konsole verstopft. Deshalb wird jede
// angefangene Antwort vollstaendig abgewartet (und notfalls verworfen), bevor
// der naechste Befehl hinausgeht.
// ---------------------------------------------------------------------------

namespace {
long long JetztMs() { return (long long)::GetTickCount64(); }
}  // namespace

FxPakPort::FxPakPort(std::string port, std::unique_ptr<Leitung> leitung)
    : port_(std::move(port)), leitung_(leitung ? std::move(leitung) : SerielleLeitung(port_)) {}

FxPakPort::~FxPakPort() { Schliessen(); }

void FxPakPort::Oeffnen() {
    Schliessen();
    leitung_->Oeffnen();
    ausstehendKopf_ = false;
    ausstehendDaten_ = 0;
    // Reste einer abgebrochenen Uebertragung abwarten und verwerfen, sonst
    // haelt der erste Lesevorgang fremde Bytes fuer die Antwort.
    for (int i = 0; i < 20; ++i) {
        if (leitung_->LesenBis(1 << 16, 50).empty()) break;
    }
    leitung_->EingangLeeren();
    // Zwei Antworten in Folge: eine einzelne kann auch bei verschobenem
    // Empfang zufaellig zustande kommen (siehe Synchronisieren).
    if (InfoBeantwortet(3000) && InfoBeantwortet(3000)) return;
    // Vielleicht nur langsam: eine ausstehende Antwort ganz abwarten, bevor
    // irgendetwas nachgeschoben wird, dann noch einmal pruefen.
    if (!nimmtNichtsAn_ && Nachholen(ausstehendMaxMs_ + 500) && InfoBeantwortet(3000) && InfoBeantwortet(3000))
        return;
    if (nimmtNichtsAn_) {
        Schliessen();
        throw std::runtime_error(std::string(kKeineAnnahme) + " (Modul haengt? Konsole aus- und wieder einschalten)");
    }
    if (!Synchronisieren()) {
        Schliessen();
        throw std::runtime_error("Antwort unvollstaendig oder ohne USBA-Kennung");
    }
}

void FxPakPort::Schliessen() {
    if (leitung_) leitung_->Schliessen();
}

bool FxPakPort::InfoBeantwortet(int zeitMs) {
    try {
        std::lock_guard<std::mutex> sperre(lock_);
        uint8_t block[fxpak::kBlock];
        Austausch(fxpak::BaueInfo(), fxpak::kBlock, block, zeitMs, 1000);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// Das Modul zerlegt alles Empfangene stur in 512-Byte-Bloecke und nimmt nur
// Bloecke an, die mit "USBA" beginnen. Ist frueher ein Befehl mittendrin
// abgerissen, liegen Reste in seinem Empfangspuffer, und jeder weitere
// Befehl ist um diesen Rest verschoben. Abhilfe: in 64-Byte-Schritten
// Nullbytes nachschieben, bis der angefangene Block voll ist (er wird dann
// als ungueltig verworfen), und jedes Mal pruefen, ob Info-Anfragen wieder
// beantwortet werden. Erst zwei Antworten in Folge zaehlen: Liegt der Kopf
// eines abgerissenen Befehls im Puffer, ergibt er mit dem naechsten Befehl
// einmal zufaellig einen gueltigen Block -- der Versatz bleibt aber.
// Nullbytes gehen nur hinaus, wenn sicher keine Antwort mehr aussteht.
bool FxPakPort::Synchronisieren() {
    // Hier steht fest, dass das Modul ueber 10 s nichts beantwortet hat -- es
    // ist nicht beschaeftigt, sondern verschoben. Ausbleibende Antworten
    // gelten deshalb schon nach 2 s als verloren.
    struct Zurueck {
        int& wert;
        int alt;
        ~Zurueck() { wert = alt; }
    } zurueck{ausstehendMaxMs_, ausstehendMaxMs_};
    ausstehendMaxMs_ = 2000;

    const Bytes nullen(fxpak::kKleinBlock, 0);
    for (int schritt = 0; schritt <= fxpak::kBlock / fxpak::kKleinBlock; ++schritt) {
        if (schritt > 0) {
            if (!Nachholen(ausstehendMaxMs_ + 500)) return false;
            try {
                leitung_->Schreiben(nullen.data(), nullen.size());
            } catch (...) {
                return false;  // nimmt nichts mehr an -- weiter probieren ist zwecklos
            }
        }
        if (InfoBeantwortet(700) && InfoBeantwortet(700)) return true;
        if (nimmtNichtsAn_) return false;
    }
    return false;
}

bool FxPakPort::Nachholen(int zeitMs) {
    if (!ausstehendKopf_ && ausstehendDaten_ == 0) return true;
    const long long ende = JetztMs() + zeitMs;
    while (ausstehendKopf_ || ausstehendDaten_ > 0) {
        if (JetztMs() - ausstehendSeit_ > ausstehendMaxMs_) {
            // Kommt nicht mehr -- als verloren betrachten.
            ausstehendKopf_ = false;
            ausstehendDaten_ = 0;
            teil_.clear();
            break;
        }
        const long long rest = ende - JetztMs();
        if (rest <= 0) return false;
        if (ausstehendKopf_) {
            const Bytes b = leitung_->LesenBis(fxpak::kBlock - (uint32_t)teil_.size(), (int)std::min(rest, 200LL));
            teil_.insert(teil_.end(), b.begin(), b.end());
            if (teil_.size() == (size_t)fxpak::kBlock) {
                const fxpak::Antwort a = fxpak::DeuteAntwort(teil_.data());
                ausstehendKopf_ = false;
                ausstehendDaten_ = a.gueltig ? fxpak::GesendeteBytes(a.laenge, ausstehendBlock_) : 0;
                teil_.clear();
            }
        } else {
            const Bytes b = leitung_->LesenBis(std::min<uint32_t>(ausstehendDaten_, 1 << 16), (int)std::min(rest, 200LL));
            ausstehendDaten_ -= (uint32_t)b.size();
        }
    }
    return true;
}

Bytes FxPakPort::Austausch(const fxpak::Paket& p, int datenBlock, uint8_t* kopf, int kopfMs, int datenMs,
                           fxpak::Antwort* antwort) {
    if (!Nachholen(kopfMs)) throw std::runtime_error(kBeschaeftigt);

    nimmtNichtsAn_ = false;
    try {
        leitung_->Schreiben(p.data(), p.size());
    } catch (const std::exception& e) {
        nimmtNichtsAn_ = std::string(e.what()) == kKeineAnnahme;
        throw;
    }
    ausstehendSeit_ = JetztMs();
    ausstehendBlock_ = datenBlock;

    // Antwortkopf
    Bytes k = leitung_->LesenBis(fxpak::kBlock, kopfMs);
    if (k.size() < (size_t)fxpak::kBlock) {
        ausstehendKopf_ = true;
        teil_ = k;
        throw std::runtime_error("Antwort unvollstaendig: " + std::to_string(k.size()) + "/512 Byte");
    }
    const fxpak::Antwort a = fxpak::DeuteAntwort(k.data());
    if (!a.gueltig) throw std::runtime_error("Antwort ohne USBA-Kennung");
    if (kopf) std::copy(k.begin(), k.end(), kopf);
    if (antwort) *antwort = a;

    // Nutzdaten (immer vollstaendig abholen, auch bei Fehlermeldung)
    const uint32_t erwartet = fxpak::GesendeteBytes(a.laenge, datenBlock);
    Bytes d;
    if (erwartet) d = leitung_->LesenBis(erwartet, datenMs);
    if (d.size() < erwartet) {
        ausstehendDaten_ = erwartet - (uint32_t)d.size();
        throw std::runtime_error("Daten unvollstaendig: " + std::to_string(d.size()) + "/" + std::to_string(erwartet) +
                                 " Byte");
    }
    return d;
}

fxpak::GeraeteInfo FxPakPort::Info() {
    std::lock_guard<std::mutex> sperre(lock_);
    uint8_t block[fxpak::kBlock];
    fxpak::Antwort a;
    Austausch(fxpak::BaueInfo(), fxpak::kBlock, block, 3000, 1000, &a);
    if (a.fehler) throw std::runtime_error("Info: Modul meldet Fehler " + std::to_string(a.fehler));
    return fxpak::DeuteInfo(block);
}

Bytes FxPakPort::Lesen(uint32_t adresse, uint32_t laenge) { return LesenRaum(fxpak::Raum::Snes, adresse, laenge); }

Bytes FxPakPort::LesenRaum(fxpak::Raum raum, uint32_t adresse, uint32_t laenge) {
    std::lock_guard<std::mutex> sperre(lock_);
    fxpak::Antwort a;
    Bytes d = Austausch(fxpak::BaueLesen(raum, adresse, laenge), fxpak::kBlock, nullptr, 3000,
                        2000 + (int)(laenge / 256), &a);
    if (a.fehler) throw std::runtime_error("Lesen: Modul meldet Fehler " + std::to_string(a.fehler));
    d.resize(std::min(a.laenge, laenge));
    return d;
}

void FxPakPort::Schreiben(fxpak::Raum raum, uint32_t adresse, const Bytes& daten) {
    if (daten.empty()) return;
    std::lock_guard<std::mutex> sperre(lock_);
    // Ohne Antwort steht danach nichts aus; vorher muss aber alles da sein.
    if (!Nachholen(3000)) throw std::runtime_error(kBeschaeftigt);
    const fxpak::Paket befehl = fxpak::BaueSchreiben(raum, adresse, (uint32_t)daten.size());
    Bytes nutz = daten;
    nutz.resize(fxpak::GesendeteBytes((uint32_t)daten.size(), fxpak::kKleinBlock), 0);
    nimmtNichtsAn_ = false;
    try {
        // Kurze Pause zwischen Befehl und Daten: die Firmware schaltet erst
        // nach dem Befehl auf Datenempfang um.
        leitung_->Schreiben(befehl.data(), befehl.size());
        ::Sleep(5);
        leitung_->Schreiben(nutz.data(), nutz.size());
    } catch (const std::exception& e) {
        nimmtNichtsAn_ = std::string(e.what()) == kKeineAnnahme;
        throw;
    }
}

Bytes FxPakPort::LesenMehrfach(const std::vector<fxpak::Bereich>& bereiche) {
    if (bereiche.empty() || bereiche.size() > (size_t)fxpak::kMaxBereiche)
        throw std::invalid_argument("LesenMehrfach: 1 bis 8 Bereiche");
    uint32_t summe = 0;
    for (const auto& b : bereiche) {
        if (b.laenge == 0) throw std::invalid_argument("LesenMehrfach: Bereich ohne Laenge");
        summe += b.laenge;
    }
    std::lock_guard<std::mutex> sperre(lock_);
    fxpak::Antwort a;
    Bytes d = Austausch(fxpak::BaueLesenMehrfach(fxpak::Raum::Snes, bereiche), fxpak::kKleinBlock, nullptr, 3000,
                        1000, &a);
    if (a.fehler) throw std::runtime_error("LesenMehrfach: Modul meldet Fehler " + std::to_string(a.fehler));
    if (a.laenge != summe)
        throw std::runtime_error("LesenMehrfach: Modul meldet " + std::to_string(a.laenge) + " statt " +
                                 std::to_string(summe) + " Byte");
    d.resize(summe);
    return d;
}

Bytes FxPakPort::DateiLesen(const std::string& pfad, const std::function<void(uint32_t, uint32_t)>& fortschritt) {
    std::lock_guard<std::mutex> sperre(lock_);
    // Kopf wie bei Austausch, die Daten aber in Happen mit Fortschritt.
    if (!Nachholen(3000)) throw std::runtime_error(kBeschaeftigt);
    const fxpak::Paket p = fxpak::BaueDateiLesen(pfad);
    nimmtNichtsAn_ = false;
    leitung_->Schreiben(p.data(), p.size());
    ausstehendSeit_ = JetztMs();
    ausstehendBlock_ = fxpak::kBlock;
    Bytes k = leitung_->LesenBis(fxpak::kBlock, 5000);
    if (k.size() < (size_t)fxpak::kBlock) {
        ausstehendKopf_ = true;
        teil_ = k;
        throw std::runtime_error("Datei: Antwort unvollstaendig");
    }
    const fxpak::Antwort a = fxpak::DeuteAntwort(k.data());
    if (!a.gueltig) throw std::runtime_error("Datei: Antwort ohne USBA-Kennung");
    const uint32_t gesamt = a.laenge;
    const uint32_t erwartet = fxpak::GesendeteBytes(gesamt, fxpak::kBlock);
    if (a.fehler && erwartet == 0) {
        // Wie beim Konfigurationslesen kann trotz Laenge 0 ein Datenblock
        // folgen; abholen, sonst verschiebt er jede weitere Antwort.
        const Bytes rest = leitung_->LesenBis(fxpak::kBlock, 1000);
        if (!rest.empty() && rest.size() < (size_t)fxpak::kBlock) {
            ausstehendSeit_ = JetztMs();
            ausstehendDaten_ = fxpak::kBlock - (uint32_t)rest.size();
        }
        throw std::runtime_error("Datei nicht lesbar: " + pfad);
    }

    // In Happen lesen: jeder Happen hat eine eigene Zeitgrenze, so dass eine
    // grosse Datei nicht an einer festen Gesamtzeit scheitert, ein
    // abgerissener Strom aber schnell auffaellt.
    constexpr uint32_t kHappen = 64 * 1024;
    Bytes d;
    d.reserve(erwartet);
    while (d.size() < erwartet) {
        const uint32_t will = std::min<uint32_t>(kHappen, erwartet - (uint32_t)d.size());
        const Bytes h = leitung_->LesenBis(will, 5000);
        d.insert(d.end(), h.begin(), h.end());
        if (fortschritt && !a.fehler) fortschritt(std::min<uint32_t>((uint32_t)d.size(), gesamt), gesamt);
        if (h.size() != will) {
            ausstehendDaten_ = erwartet - (uint32_t)d.size();
            throw std::runtime_error("Datei: " + std::to_string(d.size()) + "/" + std::to_string(erwartet) + " Byte");
        }
    }
    if (a.fehler) throw std::runtime_error("Datei nicht lesbar: " + pfad);
    d.resize(gesamt);
    return d;
}

void FxPakPort::DateiSchreiben(const std::string& pfad, const Bytes& daten) {
    std::lock_guard<std::mutex> sperre(lock_);
    if (!Nachholen(3000)) throw std::runtime_error(kBeschaeftigt);
    const fxpak::Paket p = fxpak::BaueDateiSchreiben(pfad, (uint32_t)daten.size());
    nimmtNichtsAn_ = false;
    leitung_->Schreiben(p.data(), p.size());
    ausstehendSeit_ = JetztMs();
    // Die Antwort kommt vor den Daten; ihre Laenge ist die der Datei, es
    // folgen keine Nutzdaten vom Modul.
    Bytes k = leitung_->LesenBis(fxpak::kBlock, 5000);
    if (k.size() < (size_t)fxpak::kBlock) {
        ausstehendKopf_ = true;
        teil_ = k;
        throw std::runtime_error("Datei schreiben: Antwort unvollstaendig");
    }
    const fxpak::Antwort a = fxpak::DeuteAntwort(k.data());
    if (!a.gueltig) throw std::runtime_error("Datei schreiben: Antwort ohne USBA-Kennung");
    // Die Firmware wartet in jedem Fall auf die Daten: auch nach einer
    // Fehlermeldung senden, sonst bleibt sie haengen.
    Bytes nutz = daten;
    nutz.resize(fxpak::GesendeteBytes((uint32_t)daten.size(), fxpak::kBlock), 0);
    leitung_->Schreiben(nutz.data(), nutz.size());
    if (a.fehler) throw std::runtime_error("Datei schreiben: Modul meldet Fehler " + std::to_string(a.fehler) + " (" + pfad + ")");
}

void FxPakPort::Umbenennen(const std::string& pfad, const std::string& neuerName) {
    std::lock_guard<std::mutex> sperre(lock_);
    fxpak::Antwort a;
    Austausch(fxpak::BaueUmbenennen(pfad, neuerName), fxpak::kBlock, nullptr, 5000, 1000, &a);
    if (a.fehler) throw std::runtime_error("Umbenennen: Modul meldet Fehler " + std::to_string(a.fehler) + " (" + pfad + ")");
}

void FxPakPort::DateiLoeschen(const std::string& pfad) {
    std::lock_guard<std::mutex> sperre(lock_);
    fxpak::Antwort a;
    Austausch(fxpak::BaueLoeschen(pfad), fxpak::kBlock, nullptr, 5000, 1000, &a);
    if (a.fehler) throw std::runtime_error("Loeschen: Modul meldet Fehler " + std::to_string(a.fehler) + " (" + pfad + ")");
}

void FxPakPort::Starten(const std::string& pfad, uint8_t schalter) {
    {
        std::lock_guard<std::mutex> sperre(lock_);
        fxpak::Antwort a;
        try {
            Austausch(fxpak::BaueStarten(pfad, schalter),fxpak::kBlock, nullptr, 5000, 3000, &a);
        } catch (const std::exception&) {
            // keine oder unvollstaendige Antwort -- unten neu ausrichten
        }
        if (a.gueltig && a.fehler) throw std::runtime_error("Starten: Modul meldet Fehler " + std::to_string(a.fehler) + " (" + pfad + ")");
    }
    Synchronisieren();
}

void FxPakPort::Zuruecksetzen() {
    std::lock_guard<std::mutex> sperre(lock_);
    fxpak::Antwort a;
    Austausch(fxpak::BaueZuruecksetzen(), fxpak::kBlock, nullptr, 5000, 1000, &a);
    if (a.fehler) throw std::runtime_error("Zuruecksetzen: Modul meldet Fehler " + std::to_string(a.fehler));
}

std::optional<std::string> FxPakPort::KonfigLesen(const std::string& schluessel) {
    std::lock_guard<std::mutex> sperre(lock_);
    fxpak::Antwort a;
    Bytes d = Austausch(fxpak::BaueKonfigLesen(schluessel), fxpak::kBlock, nullptr, 3000, 2000, &a);
    // Die Firmware schickt immer einen Datenblock nach -- auch wenn der Kopf
    // (bei unbekanntem Schluessel) die Laenge 0 meldet. Abholen, sonst
    // verschiebt er jede weitere Antwort.
    if (d.empty()) {
        d = leitung_->LesenBis(fxpak::kBlock, 1000);
        if (!d.empty() && d.size() < (size_t)fxpak::kBlock) {
            ausstehendSeit_ = JetztMs();
            ausstehendDaten_ = fxpak::kBlock - (uint32_t)d.size();
        }
    }
    if (a.fehler || a.laenge == 0) return std::nullopt;
    size_t n = 0;
    while (n < d.size() && n < a.laenge && d[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(d.data()), n);
}

bool FxPakPort::Beschaeftigt(const std::exception& e) { return std::string(e.what()) == kBeschaeftigt; }

// ---------------------------------------------------------------------------
// Port-Suche
// ---------------------------------------------------------------------------

std::vector<PortInfo> AlleComPorts() {
    std::vector<PortInfo> aus;
    HDEVINFO info = ::SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (info == INVALID_HANDLE_VALUE) return aus;

    SP_DEVINFO_DATA daten = {};
    daten.cbSize = sizeof(daten);
    for (DWORD i = 0; ::SetupDiEnumDeviceInfo(info, i, &daten); ++i) {
        HKEY key = ::SetupDiOpenDevRegKey(info, &daten, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key == INVALID_HANDLE_VALUE) continue;
        wchar_t name[64] = {};
        DWORD typ = 0, groesse = sizeof(name);
        const LONG r = ::RegQueryValueExW(key, L"PortName", nullptr, &typ, (LPBYTE)name, &groesse);
        ::RegCloseKey(key);
        if (r != ERROR_SUCCESS || name[0] == 0) continue;

        PortInfo p;
        p.name = NachUtf8(name);
        wchar_t puffer[512] = {};
        if (::SetupDiGetDeviceRegistryPropertyW(info, &daten, SPDRP_HARDWAREID, nullptr, (PBYTE)puffer,
                                                sizeof(puffer), nullptr))
            p.hatKennung = LiesVidPid(NachUtf8(puffer), &p.vid, &p.pid);
        aus.push_back(std::move(p));
    }
    ::SetupDiDestroyDeviceInfoList(info);
    std::sort(aus.begin(), aus.end(), [](const PortInfo& a, const PortInfo& b) { return a.name < b.name; });
    return aus;
}

namespace {

std::string Kennung(const PortInfo& p) {
    if (!p.hatKennung) return "ohne USB-Kennung";
    char t[16];
    std::snprintf(t, sizeof(t), "%04X:%04X", p.vid, p.pid);
    return t;
}

// Kurzer Grund aus der Fehlermeldung beim Oeffnen.
std::string Grund(const std::string& meldung) {
    if (meldung.find("Fehler 5") != std::string::npos) return "belegt (anderes Programm?)";
    if (meldung.find("Fehler 2") != std::string::npos) return "nicht vorhanden";
    if (meldung.find("nicht zu oeffnen") != std::string::npos) return "nicht zu oeffnen";
    return "antwortet nicht wie ein SD2SNES/FXPak (" + meldung + ")";
}

}  // namespace

std::unique_ptr<FxPakPort> FindeFxPak(const std::string& bevorzugt, std::string* bericht) {
    const std::vector<PortInfo> ports = AlleComPorts();
    std::vector<std::string> kandidaten;
    auto dazu = [&](const std::string& n) {
        if (std::find(kandidaten.begin(), kandidaten.end(), n) == kandidaten.end()) kandidaten.push_back(n);
    };
    if (!bevorzugt.empty()) dazu(bevorzugt);
    bool kennungGefunden = false;
    for (const auto& p : ports) {
        if (p.hatKennung && p.vid == fxpak::kVid && p.pid == fxpak::kPid) {
            dazu(p.name);
            kennungGefunden = true;
        }
    }
    // Ohne Treffer: alle USB-Seriell-Ports. Ports ohne USB-Kennung
    // (Bluetooth, eingebaute Schnittstellen) werden nicht beschickt.
    if (!kennungGefunden && bevorzugt.empty())
        for (const auto& p : ports)
            if (p.hatKennung) dazu(p.name);

    std::string b;
    if (ports.empty()) b = "keine COM-Ports vorhanden";
    for (const auto& name : kandidaten) {
        std::string kennung = "?";
        for (const auto& p : ports) if (p.name == name) kennung = Kennung(p);
        auto port = std::make_unique<FxPakPort>(name);
        try {
            port->Oeffnen();
            return port;
        } catch (const std::exception& e) {
            port->Schliessen();
            if (!b.empty()) b += ", ";
            b += name + " (" + kennung + "): " + Grund(e.what());
        }
    }
    // Ports, die gar nicht angefragt wurden, mit aufzaehlen.
    for (const auto& p : ports) {
        if (std::find(kandidaten.begin(), kandidaten.end(), p.name) != kandidaten.end()) continue;
        if (!b.empty()) b += ", ";
        b += p.name + " (" + Kennung(p) + "): nicht angefragt";
    }
    if (bericht) *bericht = b;
    return nullptr;
}
