#include "fxpak_sim.h"

#include "cpu65816.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <stdexcept>

namespace {

uint32_t BE32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

void SetzeBE32(uint8_t* p, uint32_t w) {
    p[0] = (uint8_t)(w >> 24);
    p[1] = (uint8_t)(w >> 16);
    p[2] = (uint8_t)(w >> 8);
    p[3] = (uint8_t)w;
}

std::string Text(const uint8_t* p, size_t max) {
    size_t n = 0;
    while (n < max && p[n]) ++n;
    return std::string((const char*)p, n);
}

// Antwortkopf plus Nutzdaten, auf Blockgroesse aufgefuellt.
std::vector<uint8_t> Antwort(uint8_t fehler, const std::vector<uint8_t>& daten, int block,
                             const uint8_t* kopfZusatz = nullptr) {
    std::vector<uint8_t> a(512, 0);
    if (kopfZusatz) std::memcpy(a.data(), kopfZusatz, 512);
    std::memcpy(a.data(), "USBA", 4);
    a[4] = 15;
    a[5] = fehler;
    SetzeBE32(&a[252], (uint32_t)daten.size());
    a.insert(a.end(), daten.begin(), daten.end());
    const size_t rest = daten.size() % (size_t)block;
    if (rest) a.insert(a.end(), (size_t)block - rest, 0);
    return a;
}

}  // namespace

FxPakSim::FxPakSim() : speicher_(0x1000000, 0x55) { HookAnlegen(); }

// Ein eigener, verkuerzter Hook mit demselben Ende wie der der Firmware:
// php / rep #$20 / pha / sep #$20 / jmp $2A63, dort rep #$20 / pla / plp /
// sta $002BFD / jmp ($FF77). Dazu ein leerer Cheat-Block.
void FxPakSim::HookAnlegen() {
    std::fill(puffer_.begin() + 0x10, puffer_.begin() + 0xD0, 0);
    const uint8_t anfang[] = {0x08, 0xC2, 0x20, 0x48, 0xE2, 0x20, 0x4C, 0x63, 0x2A};
    const uint8_t ende[] = {0xC2, 0x20, 0x68, 0x28, 0x8F, 0xFD, 0x2B, 0x00, 0x6C, 0x77, 0xFF};
    std::copy(std::begin(anfang), std::end(anfang), puffer_.begin() + 0x10);
    std::copy(std::begin(ende), std::end(ende), puffer_.begin() + 0x63);
    // Wie am Geraet (1.11.0) gelesen: Ende des Reset-Hooks bis $2AD4,
    // Endmarke, bei $2AD8 das rts der leeren Cheat-Liste, danach Altbestand.
    const uint8_t rest[] = {0xFD, 0x2B, 0x6C, 0xFC, 0xFF, 0x62, 0x72, 0x61, 0x60, 0x5F, 0x65, 0x6E, 0x64, 0x08, 0xE2, 0x30};
    std::copy(std::begin(rest), std::end(rest), puffer_.begin() + 0xD0);
    puffer_[0x1FF] = 0;     // Anzahl WRAM-Cheats
}

void FxPakSim::HookNeuSchreiben() { std::lock_guard<std::mutex> l(m_); HookAnlegen(); }
void FxPakSim::SetzeSpiegel(bool an) { std::lock_guard<std::mutex> l(m_); spiegel_ = an; }
void FxPakSim::SetzeHook(bool an) { std::lock_guard<std::mutex> l(m_); hook_ = an; }
void FxPakSim::SetzeNurIrq(bool an) { std::lock_guard<std::mutex> l(m_); nurIrq_ = an; }
int FxPakSim::Bilder() { std::lock_guard<std::mutex> l(m_); return bilder_; }
uint8_t FxPakSim::Puffer(uint32_t adresse) { std::lock_guard<std::mutex> l(m_); return puffer_[adresse & 0x1FF]; }

void FxPakSim::IramSchreiben(uint32_t versatz, uint8_t wert) {
    std::lock_guard<std::mutex> l(m_);
    iram_[versatz & 0x7FF] = wert;
}

bool FxPakSim::Bild(std::string* fehler) {
    std::lock_guard<std::mutex> l(m_);
    if (!hook_) return true;
    ++bilder_;
    if (nurIrq_) return true;
    return HookLauf(0xEA, 225, fehler);
}

bool FxPakSim::Irq(std::string* fehler, int zeile) {
    std::lock_guard<std::mutex> l(m_);
    if (!hook_) return true;
    return HookLauf(0xEE, zeile, fehler);
}

// Fuehrt den Hook samt eingehaengter Routine aus; `vektor` ist das Low-Byte
// des Vektors ($EA NMI, $EE IRQ), das das FPGA beim Lesen von $2A6C liefert.
// Der Zeilenzaehler: $2137 rastet ihn ein, $213F setzt die Lesefolge
// zurueck, $213D liefert erst das untere, dann das obere Byte.
bool FxPakSim::HookLauf(uint8_t vektor, int zeile, std::string* fehler) {
    Cpu65816 cpu;
    int eingerastet = 0;
    bool oberes = false;
    // Konsolensicht: WRAM in 7E/7F und unten in den Systembaenken,
    // Befehlspuffer $2A00-$2BFF, SA-1-I-RAM $3000-$37FF, ROM ab $8000.
    auto ort = [this](uint32_t ad) -> uint8_t* {
        const uint32_t bank = ad >> 16, o = ad & 0xFFFF;
        if (bank == 0x7E || bank == 0x7F) return &wram_[((bank - 0x7E) << 16) | o];
        if ((bank & 0x40) == 0) {
            if (o < 0x2000) return &wram_[o];
            if (o >= 0x2A00 && o < 0x2C00) return &puffer_[o - 0x2A00];
            if (o >= 0x3000 && o < 0x3800) return &iram_[o - 0x3000];
        }
        return nullptr;
    };
    cpu.lies = [&](uint32_t ad) -> uint8_t {
        if (ad == 0x002A6C) return vektor;
        if (ad == 0x004212) return zeile >= 225 ? 0x80 : 0x00;   // Austastluecke
        if (ad == 0x002137) { eingerastet = zeile; return 0xFF; }
        if (ad == 0x00213F) { oberes = false; return 0x45; }
        if (ad == 0x00213D) {
            const uint8_t v = oberes ? (uint8_t)((eingerastet >> 8) & 1) : (uint8_t)eingerastet;
            oberes = !oberes;
            return v;
        }
        const uint8_t* q = ort(ad);
        return q ? *q : 0xFF;
    };
    cpu.schreibe = [&](uint32_t ad, uint8_t v) { if (uint8_t* q = ort(ad)) *q = v; };

    // Zustand des Spiels; abwechselnd 8- und 16-Bit-Register.
    const bool breit = bilder_ % 2 == 0;
    const uint16_t A = breit ? 0xBEEF : 0x12EF, X = breit ? 0x1234 : 0x0056, Y = breit ? 0x5678 : 0x0078;
    const uint16_t D = 0x0100;
    const uint8_t DBR = 0x80, P = breit ? 0x05 : 0x31;
    cpu.a = A; cpu.x = X; cpu.y = Y; cpu.d = D; cpu.dbr = DBR; cpu.p = P;
    cpu.s = 0x1FF0;
    // NMI im nativen Modus: Programmbank, PC, P auf den Stapel.
    cpu.schreibe(cpu.s--, 0x80);
    cpu.schreibe(cpu.s--, 0x81);
    cpu.schreibe(cpu.s--, 0x23);
    cpu.schreibe(cpu.s--, P);
    const uint16_t S = cpu.s;
    // Der Hook sichert P, wie es nach dem Interrupt steht; das RTI des
    // Spiels stellt spaeter das eigene her.
    const uint8_t pNmi = (uint8_t)((P | 0x04) & ~0x08);
    cpu.p = pNmi;
    cpu.pbr = 0;
    cpu.pc = 0x2A10;
    std::string f;
    const bool ok = cpu.LaufeBis((uint16_t)(0xFF00 | vektor), 20000, &f);
    hookBefehle_ = cpu.Schritte();
    seitenGeaendert_ = seitenGeaendert_ || cpu.seitenGeaendert;
    if (!ok) {
        if (fehler) *fehler = f;
        return false;
    }
    char t[160];
    // Takt::Nachlauf legt einen zweiten Rahmen (4 Byte) auf den Stapel.
    if ((cpu.s != S && cpu.s != (uint16_t)(S - 4)) || cpu.a != A || cpu.x != X || cpu.y != Y || cpu.d != D ||
        cpu.dbr != DBR || cpu.p != pNmi) {
        std::snprintf(t, sizeof(t), "Zustand nicht wiederhergestellt: S %04X/%04X A %04X/%04X X %04X/%04X D %04X DBR %02X P %02X/%02X",
                      cpu.s, S, cpu.a, A, cpu.x, X, cpu.d, cpu.dbr, cpu.p, pNmi);
        if (fehler) *fehler = t;
        return false;
    }
    // Hat der Hook den Ruecksprung umgelenkt (Aufschub, Nachlauf), folgt der
    // IRQ des Spiels -- hier nur sein rti -- und danach der Nachlauf. Am Ende
    // muss das Spiel genau dort weiterlaufen, wo es unterbrochen wurde, mit
    // allen Registern und dem Stapel wie vor dem Interrupt.
    const uint16_t oben = cpu.s;
    const uint8_t pcl = cpu.lies((uint16_t)(oben + 2)), pch = cpu.lies((uint16_t)(oben + 3)),
                  pb = cpu.lies((uint16_t)(oben + 4));
    if (pcl != 0x23 || pch != 0x81 || pb != 0x80) {
        cpu.p = cpu.lies((uint16_t)(oben + 1));
        if (cpu.p & 0x10) { cpu.x &= 0xFF; cpu.y &= 0xFF; }
        cpu.pc = (uint16_t)(pcl | (pch << 8));
        cpu.pbr = pb;
        cpu.s = (uint16_t)(oben + 4);
        cpu.stopBei = 0x808123;
        f.clear();
        const bool zurueck = cpu.LaufeBis(0x0000, 20000, &f);
        seitenGeaendert_ = seitenGeaendert_ || cpu.seitenGeaendert;
        if (!zurueck || cpu.pbr != 0x80 || cpu.pc != 0x8123) {
            if (fehler) *fehler = "Nachlauf kehrt nicht zurueck: " + f;
            return false;
        }
        if (cpu.s != 0x1FF0 || cpu.a != A || cpu.x != X || cpu.y != Y || cpu.d != D || cpu.dbr != DBR || cpu.p != P) {
            std::snprintf(t, sizeof(t), "Nachlauf: Zustand falsch: S %04X A %04X/%04X X %04X/%04X Y %04X/%04X P %02X/%02X",
                          cpu.s, cpu.a, A, cpu.x, X, cpu.y, Y, cpu.p, P);
            if (fehler) *fehler = t;
            return false;
        }
        ++nachlaeufe_;
    }
    return true;
}

void FxPakSim::Schreibdaten(uint8_t raum, uint32_t adresse, const std::vector<uint8_t>& daten) {
    std::lock_guard<std::mutex> l(m_);
    for (size_t i = 0; i < daten.size(); ++i) {
        if (raum == 3) {
            puffer_[(adresse + i) & 0x1FF] = daten[i];
            if (((adresse + i) & 0x1FF) == 0 && daten[i] && beimBefehl) beimBefehl(daten[i]);
        }
        else if (raum == 1 && adresse + i < speicher_.size()) speicher_[adresse + i] = daten[i];
    }
}

void FxPakSim::SetzeRom(const std::string& pfad) { std::lock_guard<std::mutex> l(m_); rom_ = pfad; }
void FxPakSim::SetzeEinstellungen(uint16_t e) { std::lock_guard<std::mutex> l(m_); einstellungen_ = e; }
void FxPakSim::SetzeAngesteckt(bool an) { std::lock_guard<std::mutex> l(m_); angesteckt_ = an; }
bool FxPakSim::Angesteckt() { std::lock_guard<std::mutex> l(m_); return angesteckt_; }
void FxPakSim::NaechsteAntwortVerzoegern(int ms) { std::lock_guard<std::mutex> l(m_); verzoegerung_ = ms; }
int FxPakSim::NimmVerzoegerung() {
    std::lock_guard<std::mutex> l(m_);
    const int v = verzoegerung_;
    verzoegerung_ = 0;
    return v;
}
void FxPakSim::Verstopfen() { std::lock_guard<std::mutex> l(m_); verstopft_ = true; }
bool FxPakSim::Verstopft() { std::lock_guard<std::mutex> l(m_); return verstopft_; }
int FxPakSim::Befehle() { std::lock_guard<std::mutex> l(m_); return befehle_; }

void FxPakSim::Datei(const std::string& pfad, std::vector<uint8_t> inhalt) {
    std::lock_guard<std::mutex> l(m_);
    dateien_[pfad] = std::move(inhalt);
}

void FxPakSim::Konfig(const std::string& schluessel, const std::string& wert) {
    std::lock_guard<std::mutex> l(m_);
    konfig_[schluessel] = wert;
}

void FxPakSim::WramSchreiben(uint32_t ra, uint8_t wert) {
    std::lock_guard<std::mutex> l(m_);
    ra &= 0x1FFFF;
    wram_[ra] = wert;
    if (spiegel_) speicher_[0xF50000 + ra] = wert;
}

bool FxPakSim::NimmStart(Startauftrag& s) {
    std::lock_guard<std::mutex> l(m_);
    if (starts_.empty()) return false;
    s = starts_.front();
    starts_.erase(starts_.begin());
    return true;
}

void FxPakSim::Speicher(uint32_t adresse, const std::vector<uint8_t>& daten) {
    std::lock_guard<std::mutex> l(m_);
    for (size_t i = 0; i < daten.size() && adresse + i < speicher_.size(); ++i) speicher_[adresse + i] = daten[i];
}

std::vector<uint8_t> FxPakSim::Bearbeite(const std::vector<uint8_t>& b) {
    std::lock_guard<std::mutex> l(m_);
    ++befehle_;
    if (b.size() < 64 || std::memcmp(b.data(), "USBA", 4) != 0) return {};
    const uint8_t befehl = b[4], raum = b[5], schalter = b[6];
    const int block = (schalter & 128) ? 64 : 512;

    switch (befehl) {
        case 11: {  // Info
            uint8_t k[512] = {};
            k[6] = 0x01;  // Funktionsbits, beliebig
            k[10] = (uint8_t)(einstellungen_ | (hook_ ? 0x01 : 0x00));
            k[11] = (uint8_t)(einstellungen_ >> 8);
            std::strncpy((char*)&k[16], rom_.c_str(), 235);
            SetzeBE32(&k[256], 0x010B00);
            std::strcpy((char*)&k[260], "1.11.0");
            std::strcpy((char*)&k[324], "sd2snes Mk.III");
            std::vector<uint8_t> a = Antwort(0, {}, 512, k);
            return a;
        }
        case 0: {  // Lesen
            if (b.size() < 512) return {};
            if (raum == 1) {
                const uint32_t laenge = BE32(&b[252]);
                const uint32_t adresse = BE32(&b[256]);
                std::vector<uint8_t> d;
                for (uint32_t i = 0; i < laenge; ++i)
                    d.push_back(adresse + i < speicher_.size() ? speicher_[adresse + i] : 0);
                return Antwort(0, d, block);
            }
            if (raum == 0) {
                auto it = dateien_.find(Text(&b[256], 256));
                if (it == dateien_.end()) return Antwort(1, {}, block);
                return Antwort(0, it->second, block);
            }
            if (raum == 5) {
                auto it = konfig_.find(Text(&b[256], 256));
                if (it == konfig_.end()) {
                    // Wie die Firmware: Fehler, Laenge 0 -- und trotzdem ein Datenblock.
                    std::vector<uint8_t> a = Antwort(1, {}, block);
                    a.insert(a.end(), 512, 0);
                    return a;
                }
                std::vector<uint8_t> d(it->second.begin(), it->second.end());
                d.push_back(0);
                return Antwort(0, d, block);
            }
            if (raum == 3) {  // Befehlspuffer
                const uint32_t laenge = BE32(&b[252]);
                const uint32_t adresse = BE32(&b[256]);
                std::vector<uint8_t> d;
                for (uint32_t i = 0; i < laenge; ++i) d.push_back(puffer_[(adresse + i) & 0x1FF]);
                return Antwort(0, d, block);
            }
            return Antwort(1, {}, block);
        }
        case 1:  // Schreiben: Antwort vor den Daten, mit Schalter 64 gar keine
            if (schalter & 64) return {};
            return Antwort(0, {}, 512);
        case 8: {  // Konsole zuruecksetzen: kurzer Reset, ROM bleibt (fuer beimStart: Schalter kNurReset)
            Startauftrag s{std::string(), kNurReset};
            if (beimStart) beimStart(s);
            starts_.push_back(s);
            return Antwort(0, {}, 512);
        }
        case 9: {  // Spiel starten (Schalter 1: Konsole im Reset halten, 2: nur loslassen)
            if (raum != 0 || b.size() < 512) return Antwort(1, {}, 512);
            Startauftrag s{Text(&b[256], 256), schalter};
            if (!(schalter & 2)) rom_ = s.pfad;
            if (beimStart) beimStart(s);
            starts_.push_back(s);
            return Antwort(0, {}, 512);
        }
        case 6: {  // Datei loeschen
            if (raum != 0 || b.size() < 512) return Antwort(1, {}, 512);
            return Antwort(dateien_.erase(Text(&b[256], 256)) ? 0 : 1, {}, 512);
        }
        case 7: {  // Datei umbenennen: neuer Name (ohne Ordner) ab Byte 8
            if (raum != 0 || b.size() < 512) return Antwort(1, {}, 512);
            const std::string alt = Text(&b[256], 256);
            const size_t schnitt = alt.rfind('/');
            const std::string neu = (schnitt == std::string::npos ? std::string() : alt.substr(0, schnitt + 1)) + Text(&b[8], 240);
            auto it = dateien_.find(alt);
            if (it == dateien_.end() || dateien_.count(neu)) return Antwort(1, {}, 512);
            dateien_[neu] = std::move(it->second);
            dateien_.erase(alt);
            return Antwort(0, {}, 512);
        }
        case 2: {  // Lesen, mehrere Bereiche
            if (raum == 0) return Antwort(1, {}, block);
            std::vector<uint8_t> d;
            for (int i = 0; i < 8; ++i) {
                const uint8_t n = b[32 + i * 4];
                const uint32_t adr = ((uint32_t)b[33 + i * 4] << 16) | ((uint32_t)b[34 + i * 4] << 8) | b[35 + i * 4];
                for (uint32_t k = 0; k < n; ++k) d.push_back(speicher_[(adr + k) & 0xFFFFFF]);
            }
            return Antwort(0, d, block);
        }
        default:
            return Antwort(1, {}, 512);
    }
}

namespace {

class Sim : public Leitung {
public:
    explicit Sim(std::shared_ptr<FxPakSim> sim) : sim_(std::move(sim)) {}

    void Oeffnen() override {
        if (!sim_->Angesteckt()) throw std::runtime_error("Sim: nicht angesteckt");
        offen_ = true;
    }
    void Schliessen() override { offen_ = false; }
    bool Offen() const override { return offen_; }

    void Schreiben(const uint8_t* d, size_t n) override {
        if (!offen_ || !sim_->Angesteckt()) throw std::runtime_error("Sim: Leitung getrennt");
        // Firmware-Verhalten: Daten, waehrend eine Antwort noch in Arbeit
        // ist, verstopfen den Empfang fuer immer.
        if (::GetTickCount64() < freiAb_) sim_->Verstopfen();
        if (sim_->Verstopft()) throw std::runtime_error("nimmt keine Daten an");
        befehl_.insert(befehl_.end(), d, d + n);
        // Mehrbereichs-Befehle sind 64 Byte lang, alle anderen 512.
        for (;;) {
            // Nutzdaten eines Schreibbefehls: in Bloecken, der letzte aufgefuellt.
            if (datenRest_ > 0) {
                const size_t k = std::min(befehl_.size(), datenRest_);
                daten_.insert(daten_.end(), befehl_.begin(), befehl_.begin() + (long)k);
                befehl_.erase(befehl_.begin(), befehl_.begin() + (long)k);
                datenRest_ -= k;
                if (datenRest_ > 0) break;
                daten_.resize(datenLaenge_);
                if (datenRaum_ == 0) sim_->Datei(datenPfad_, daten_);
                else sim_->Schreibdaten(datenRaum_, datenAdresse_, daten_);
                daten_.clear();
                continue;
            }
            if (befehl_.size() < 5) break;
            const size_t noetig = (befehl_[4] == 2 || befehl_[4] == 3) ? 64 : 512;
            if (befehl_.size() < noetig) break;
            std::vector<uint8_t> b(befehl_.begin(), befehl_.begin() + (long)noetig);
            befehl_.erase(befehl_.begin(), befehl_.begin() + (long)noetig);
            const std::vector<uint8_t> a = sim_->Bearbeite(b);
            if (b[4] == 1 && std::memcmp(b.data(), "USBA", 4) == 0) {
                const uint32_t laenge = BE32(&b[252]);
                const size_t blk = (b[6] & 128) ? 64 : 512;
                datenLaenge_ = laenge;
                datenRest_ = (laenge + blk - 1) / blk * blk;
                datenRaum_ = b[5];
                datenAdresse_ = BE32(&b[256]);
                datenPfad_ = Text(&b[256], 256);
            }
            if (!a.empty()) {
                const int v = sim_->NimmVerzoegerung();
                if (v > 0) freiAb_ = ::GetTickCount64() + (ULONGLONG)v;
            }
            ausgang_.insert(ausgang_.end(), a.begin(), a.end());
        }
    }

    Bytes LesenBis(uint32_t anzahl, int zeitMs) override {
        Bytes aus;
        if (!offen_ || !sim_->Angesteckt()) return aus;
        // Verzoegerte Antwort: wie am echten Port bis zur Zeitgrenze warten.
        const ULONGLONG jetzt = ::GetTickCount64();
        if (jetzt < freiAb_) {
            const ULONGLONG warten = std::min<ULONGLONG>(freiAb_ - jetzt, (ULONGLONG)std::max(zeitMs, 0));
            ::Sleep((DWORD)warten);
            if (::GetTickCount64() < freiAb_) return aus;
        }
        while (aus.size() < anzahl && !ausgang_.empty()) {
            aus.push_back(ausgang_.front());
            ausgang_.pop_front();
        }
        return aus;
    }

    void EingangLeeren() override { ausgang_.clear(); }

private:
    std::shared_ptr<FxPakSim> sim_;
    bool offen_ = false;
    std::vector<uint8_t> befehl_;
    std::deque<uint8_t> ausgang_;
    size_t datenRest_ = 0;
    uint32_t datenLaenge_ = 0;
    uint8_t datenRaum_ = 0;
    uint32_t datenAdresse_ = 0;
    std::string datenPfad_;
    std::vector<uint8_t> daten_;
    ULONGLONG freiAb_ = 0;  // Antwort erst ab diesem Zeitpunkt lesbar
};

}  // namespace

std::unique_ptr<Leitung> SimLeitung(std::shared_ptr<FxPakSim> sim) { return std::make_unique<Sim>(std::move(sim)); }
