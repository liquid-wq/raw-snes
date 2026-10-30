#include "nmi_kopie.h"

#include <algorithm>
#include <set>

namespace nmikopie {

namespace {

constexpr uint32_t kRaWramEnde = 0x020000;
constexpr uint32_t kRaIram = 0x0A0000;
constexpr uint32_t kRaIramEnde = 0x0A0800;
constexpr uint32_t kIramKonsole = 0x3000;   // SA-1-I-RAM aus Sicht der Konsole

// Groessen der Routine in Byte (siehe Erzeuge).
// Anfang: Sichern, Direktseite, Bildpruefung, Zaehler, Stapelzeiger.
// Zaehler: KOPF, SCHWANZ, Stapelzeiger (2), Vektor-Byte, Laeufe, bei
// Takt::Austastung dazu MERKER.
// Takt::Nachlauf: dazu Hook-Teil (13) und Sichern von A (3) bzw. am Ende
// pla / $2BFD / rti statt des Ruecksprungs in den Hook (+5).
constexpr int kHookTeil = 13;
int Anfang(Takt t) {
    return t == Takt::Nmi ? 43 : t == Takt::Austastung ? 64 : t == Takt::Nachlauf ? 36 + kHookTeil + 3 : 36;
}
int Zaehler(Takt t) { return t == Takt::Austastung ? 7 : 6; }
int Ende(Takt t) { return t == Takt::Nachlauf ? 20 : 15; }   // Zaehler, Wiederherstellen, Ruecksprung
constexpr int kJeSchleife = 22;   // Wortschleife je Datenbank
constexpr int kJeWort = 4;        // Zeiger + Wert
constexpr int kJeBlock = 12;      // Blockkopie: lda/ldx/ldy/mvn

uint8_t Bank(uint32_t ra) { return ra < kRaWramEnde ? (uint8_t)(0x7E + (ra >> 16)) : 0x00; }

uint16_t Zeiger(uint32_t ra) {
    return ra < kRaWramEnde ? (uint16_t)(ra & 0xFFFF) : (uint16_t)(kIramKonsole + (ra - kRaIram));
}

bool ZweitesGueltig(uint32_t ra) {
    // Das zweite Byte eines Wortes muss in derselben Bank liegen.
    if (ra < kRaWramEnde) return (ra & 0xFFFF) != 0xFFFF;
    return ra + 1 < kRaIramEnde;
}

// Woerter, die eine sortierte Adressfolge abdecken.
std::vector<uint32_t> Woerter(const std::vector<uint32_t>& a, size_t von, size_t bis) {
    std::vector<uint32_t> w;
    for (size_t i = von; i < bis;) {
        w.push_back(a[i]);
        const uint32_t ende = ZweitesGueltig(a[i]) ? a[i] + 1 : a[i];
        ++i;
        while (i < bis && a[i] <= ende) ++i;
    }
    return w;
}

// Ein Stueck der Adressfolge einer Bank, als Block oder als Woerter gelesen.
struct Stueck {
    size_t von, bis;   // Indizes in der sortierten Folge
    bool block;
};

struct BankPlan {
    uint8_t bank = 0;
    std::vector<uint32_t> adressen;   // sortiert
    std::vector<Stueck> stuecke;
    int kosten = 0;                   // Byte, inklusive Schleife
    bool schleife = false;
};

// Guenstigste Aufteilung einer Bank (dynamische Programmierung). Mit
// `woerterErlaubt` wird die Wortschleife einmal fuer die Bank bezahlt.
BankPlan TeileAuf(uint8_t bank, const std::vector<uint32_t>& a, bool woerterErlaubt) {
    const size_t n = a.size();
    const int unendlich = 1 << 28;
    std::vector<int> best(n + 1, unendlich);
    std::vector<Stueck> wahl(n + 1);
    best[0] = 0;
    // best[von] steht fest, sobald alle kleineren `von` durch sind.
    for (size_t von = 0; von < n; ++von) {
        if (best[von] >= unendlich) continue;
        int woerter = 0;
        uint32_t gedeckt = 0;
        for (size_t bis = von + 1; bis <= n; ++bis) {
            const uint32_t neu = a[bis - 1];
            if (woerter == 0 || neu > gedeckt) {
                ++woerter;
                gedeckt = ZweitesGueltig(neu) ? neu + 1 : neu;
            }
            const int block = best[von] + kJeBlock + (int)(neu - a[von] + 1);
            if (block < best[bis]) {
                best[bis] = block;
                wahl[bis] = {von, bis, true};
            }
            const int w = best[von] + kJeWort * woerter;
            if (woerterErlaubt && w < best[bis]) {
                best[bis] = w;
                wahl[bis] = {von, bis, false};
            }
        }
    }
    BankPlan p;
    p.bank = bank;
    p.adressen = a;
    for (size_t i = n; i > 0; i = wahl[i].von) p.stuecke.insert(p.stuecke.begin(), wahl[i]);
    for (const Stueck& s : p.stuecke) if (!s.block) p.schleife = true;
    p.kosten = best[n] + (p.schleife ? kJeSchleife : 0);
    return p;
}

BankPlan BesteAufteilung(uint8_t bank, const std::vector<uint32_t>& a) {
    BankPlan mitWoertern = TeileAuf(bank, a, true);
    BankPlan nurBloecke = TeileAuf(bank, a, false);
    return nurBloecke.kosten <= mitWoertern.kosten ? nurBloecke : mitWoertern;
}

struct Gesamtplan {
    std::vector<BankPlan> baenke;
    int groesse = 0;
};

Gesamtplan Plane(const std::set<uint32_t>& auswahl, Takt takt) {
    std::map<uint8_t, std::vector<uint32_t>> jeBank;
    for (uint32_t ra : auswahl) jeBank[Bank(ra)].push_back(ra);
    Gesamtplan g;
    g.groesse = Anfang(takt) + Ende(takt) + Zaehler(takt);
    for (auto& kv : jeBank) {
        g.baenke.push_back(BesteAufteilung(kv.first, kv.second));
        g.groesse += g.baenke.back().kosten;
    }
    return g;
}

}  // namespace

std::vector<ByteSchreiben> Einhaengen() { return {{kAusgang + 1, kOperandNeu}, {kAusgang, kBefehlNeu}}; }

std::vector<ByteSchreiben> Aushaengen() { return {{kAusgang, kBefehlOriginal}, {kAusgang + 1, kOperandOriginal}}; }

Zustand Pruefe(const std::vector<uint8_t>& puffer) {
    if (puffer.size() < kPufferGroesse) return Zustand::Unbekannt;
    const uint8_t* e = &puffer[kAusgang - kPuffer];
    if (!std::equal(e + 2, e + sizeof(kAusgangOriginal), kAusgangOriginal + 2)) return Zustand::Unbekannt;
    Zustand z;
    if (e[0] == kBefehlOriginal && e[1] == kOperandOriginal) z = Zustand::Original;
    else if (e[0] == kBefehlNeu && e[1] == kOperandNeu) z = Zustand::Eingehaengt;
    else return Zustand::Unbekannt;
    if (puffer[kCheatZahl - kPuffer] != 0) return Zustand::ZuVieleCheats;
    return z;
}

bool KonsoleLiest(uint32_t ra) { return ra < kRaWramEnde || (ra >= kRaIram && ra < kRaIramEnde); }

namespace {
constexpr int kPlatz = (int)(kBlockEnde - kBlock);
Programm Baue(const std::set<uint32_t>& auswahl, Programm p);
}  // namespace

Programm Erzeuge(const std::vector<uint32_t>& raAdressen, Takt takt) {
    Programm p;
    p.takt = takt;
    // Auswahl in der gegebenen Reihenfolge, solange die Routine passt.
    std::set<uint32_t> auswahl;
    for (uint32_t ra : raAdressen) {
        if (!KonsoleLiest(ra) || auswahl.count(ra)) continue;
        auswahl.insert(ra);
        if (Plane(auswahl, takt).groesse > kPlatz) {
            auswahl.erase(ra);
            p.ohnePlatz.push_back(ra);
        }
    }
    return Baue(auswahl, std::move(p));
}

int Platzbedarf(const std::vector<uint32_t>& raAdressen, Takt takt) {
    std::set<uint32_t> auswahl;
    for (uint32_t ra : raAdressen)
        if (KonsoleLiest(ra)) auswahl.insert(ra);
    return Plane(auswahl, takt).groesse;
}

Programm ErzeugeGruppen(const std::vector<Gruppe>& gruppen, std::vector<bool>* drin, Takt takt) {
    std::set<uint32_t> auswahl;
    std::vector<bool> dabei(gruppen.size(), false);
    std::vector<int> stufen;
    for (const Gruppe& g : gruppen) stufen.push_back(g.stufe);
    std::sort(stufen.begin(), stufen.end());
    stufen.erase(std::unique(stufen.begin(), stufen.end()), stufen.end());
    for (int stufe : stufen) {
        for (;;) {
            int beste = -1, besteGroesse = 0;
            for (size_t i = 0; i < gruppen.size(); ++i) {
                if (dabei[i] || gruppen[i].stufe != stufe) continue;
                std::set<uint32_t> probe = auswahl;
                for (uint32_t a : gruppen[i].adressen)
                    if (KonsoleLiest(a)) probe.insert(a);
                const int g = Plane(probe, takt).groesse;
                if (g <= kPlatz && (beste < 0 || g < besteGroesse)) {
                    beste = (int)i;
                    besteGroesse = g;
                }
            }
            if (beste < 0) break;
            dabei[(size_t)beste] = true;
            for (uint32_t a : gruppen[(size_t)beste].adressen)
                if (KonsoleLiest(a)) auswahl.insert(a);
        }
    }
    Programm p;
    p.takt = takt;
    for (size_t i = 0; i < gruppen.size(); ++i)
        if (!dabei[i])
            for (uint32_t a : gruppen[i].adressen)
                if (KonsoleLiest(a) && !auswahl.count(a)) p.ohnePlatz.push_back(a);
    std::sort(p.ohnePlatz.begin(), p.ohnePlatz.end());
    p.ohnePlatz.erase(std::unique(p.ohnePlatz.begin(), p.ohnePlatz.end()), p.ohnePlatz.end());
    if (drin) *drin = dabei;
    return Baue(auswahl, std::move(p));
}

namespace {

Programm Baue(const std::set<uint32_t>& auswahl, Programm p) {
    const Gesamtplan plan = Plane(auswahl, p.takt);

    // Lage der Daten: erst die Bloecke, dann die Woerter, Bank fuer Bank.
    int schleifen = 0, woerter = 0, bloecke = 0, blockBytes = 0;
    for (const BankPlan& b : plan.baenke) {
        if (b.schleife) ++schleifen;
        for (const Stueck& s : b.stuecke) {
            if (s.block) { ++bloecke; blockBytes += (int)(b.adressen[s.bis - 1] - b.adressen[s.von] + 1); }
            else woerter += (int)Woerter(b.adressen, s.von, s.bis).size();
        }
    }
    const int code = Anfang(p.takt) + kJeBlock * bloecke + kJeSchleife * schleifen + Ende(p.takt);
    const uint32_t tabelle = kBlock + (uint32_t)code;
    p.kopf = tabelle + 2u * (uint32_t)woerter;
    p.stapel = p.kopf + 1;
    p.vektor = p.stapel + 2;
    p.laeufe = p.vektor + 1;
    p.merker = p.takt == Takt::Austastung ? p.laeufe + 1 : 0;
    const uint32_t daten = p.laeufe + 1 + (p.takt == Takt::Austastung ? 1 : 0);
    const uint32_t wortDaten = daten + (uint32_t)blockBytes;
    p.schwanz = wortDaten + 2u * (uint32_t)woerter;
    const auto dp = [](uint32_t a) { return (uint8_t)(a - kBlock); };

    std::vector<uint8_t>& c = p.block;
    auto b1 = [&](std::initializer_list<int> bytes) { for (int x : bytes) c.push_back((uint8_t)x); };

    if (p.takt == Takt::Nachlauf) {
        // Hook-Teil, im IRQ des Spiels: Hook-Ende ausfuehren (rep #$20 / pla /
        // plp), dann einen zweiten Interrupt-Rahmen auf den Stapel legen
        // (PB $00, NACHLAUF, P $24: I gesetzt, A 8 Bit, X/Y 16 Bit, damit die
        // oberen Haelften von X/Y erhalten bleiben) und ohne den
        // Schreibzugriff auf $2BFD zum Vektor des Spiels springen ($2A6B:
        // jmp ($FF77)). Der Puffer bleibt eingeblendet; das rti des Spiels
        // fuehrt in den Nachlauf, darunter liegt unberuehrt der Originalrahmen.
        const uint32_t nachlauf = kBlock + kHookTeil;
        b1({0xC2, 0x20, 0x68, 0x28});
        b1({0xF4, (int)(nachlauf >> 8), 0x00});              // pea: PB $00, PCH
        b1({0xF4, 0x24, (int)(nachlauf & 0xFF)});            // pea: PCL, P
        b1({0x4C, (int)((kAusgang + 8) & 0xFF), (int)((kAusgang + 8) >> 8)});
        // Nachlauf: A sichern (16 Bit), weiter wie die Routine im Hook.
        b1({0xC2, 0x20, 0x48});
    }
    // Sichern; A/X/Y 16 Bit; Direktseite auf den Block.
    b1({0x8B, 0x0B, 0xC2, 0x30, 0xDA, 0x5A, 0xF4, 0x00, 0x2B, 0x2B});
    // lda $002A6C / sta <VEKTOR / inc <LAEUFE: Beim Lesen von $00:2A6C
    // liefert das FPGA das Low-Byte des benutzten Vektors -- $EA fuer NMI,
    // $EE fuer IRQ. VEKTOR und LAEUFE halten jeden Durchlauf fest.
    b1({0xE2, 0x20, 0xAF, 0x6C, 0x2A, 0x00, 0x85, dp(p.vektor), 0xE6, dp(p.laeufe)});
    std::vector<size_t> endeSprung;   // Stellen, an die die Adresse von ENDE kommt
    if (p.takt == Takt::JederLauf || p.takt == Takt::Nachlauf) {
        // Jeder Durchlauf ist ein neues Bild: keine Pruefung.
    } else if (p.takt == Takt::Nmi) {
        // Nur beim NMI kopieren: cmp #$EA / beq +3 / jmp ENDE
        b1({0xC9, 0xEA, 0xF0, 0x03, 0x4C});
        endeSprung.push_back(c.size());
        b1({0x00, 0x00});
    } else {
        // Einmal je Bild am Bildende: beim NMI, sonst beim ersten IRQ in der
        // Austastluecke ($4212 Bit 7, Lesen ohne Nebenwirkung) nach einem
        // Durchlauf ausserhalb. MERKER = "seit der letzten Kopie war ein
        // Durchlauf ausserhalb der Austastluecke".
        //       cmp #$EA / beq NMI
        //       lda $004212 / bpl AUSSERHALB
        //       lda <MERKER / bne +3 / jmp ENDE
        // NMI:  stz <MERKER / bra KOPIE
        // AUSSERHALB: lda #$01 / sta <MERKER / jmp ENDE
        b1({0xC9, 0xEA, 0xF0, 0x0D, 0xAF, 0x12, 0x42, 0x00, 0x10, 0x0B, 0xA5, dp(p.merker), 0xD0, 0x03, 0x4C});
        endeSprung.push_back(c.size());
        b1({0x00, 0x00, 0x64, dp(p.merker), 0x80, 0x07, 0xA9, 0x01, 0x85, dp(p.merker), 0x4C});
        endeSprung.push_back(c.size());
        b1({0x00, 0x00});
    }
    // SCHWANZ = ~(KOPF+1), KOPF = KOPF+1 (8 Bit)
    b1({0xA5, dp(p.kopf), 0x1A, 0x49, 0xFF, 0x85, dp(p.schwanz), 0x49, 0xFF, 0x85, dp(p.kopf)});
    // 16 Bit; Stapelzeiger merken (tsc / sta <STAPEL)
    b1({0xC2, 0x20, 0x3B, 0x85, dp(p.stapel)});

    // Blockkopien: lda #laenge-1 / ldx #quelle / ldy #ziel / mvn $00,quellbank
    uint32_t ziel = daten;
    for (const BankPlan& b : plan.baenke) {
        for (const Stueck& s : b.stuecke) {
            if (!s.block) continue;
            const uint32_t erste = b.adressen[s.von];
            const uint16_t laenge = (uint16_t)(b.adressen[s.bis - 1] - erste + 1);
            const uint16_t quelle = Zeiger(erste);
            b1({0xA9, (laenge - 1) & 0xFF, (laenge - 1) >> 8});
            b1({0xA2, quelle & 0xFF, quelle >> 8});
            b1({0xA0, (int)(ziel & 0xFF), (int)(ziel >> 8)});
            b1({0x54, 0x00, b.bank});
            for (uint16_t k = 0; k < laenge; ++k) p.lage[erste + k] = ziel + k;
            ziel += laenge;
        }
    }

    // Wortschleifen je Datenbank: LDA (TABELLE,X) / STA $00:WORTDATEN,X
    std::vector<uint16_t> zeiger;
    for (const BankPlan& b : plan.baenke) {
        if (!b.schleife) continue;
        const uint16_t von = (uint16_t)(2 * zeiger.size());
        for (const Stueck& s : b.stuecke) {
            if (s.block) continue;
            for (uint32_t ra : Woerter(b.adressen, s.von, s.bis)) {
                const uint32_t lage = wortDaten + 2u * (uint32_t)zeiger.size();
                if (!p.lage.count(ra)) p.lage[ra] = lage;
                if (ZweitesGueltig(ra) && !p.lage.count(ra + 1)) p.lage[ra + 1] = lage + 1;
                zeiger.push_back(Zeiger(ra));
            }
        }
        const uint16_t bis = (uint16_t)(2 * zeiger.size());
        b1({0xA9, b.bank, b.bank, 0x48, 0xAB, 0xAB});
        b1({0xA2, von & 0xFF, von >> 8});
        b1({0xA1, dp(tabelle)});
        b1({0x9F, (int)(wortDaten & 0xFF), (int)((wortDaten >> 8) & 0xFF), 0x00});
        b1({0xE8, 0xE8});
        b1({0xE0, bis & 0xFF, bis >> 8});
        b1({0xD0, 0xF3});   // zurueck zu LDA (TABELLE,X): 13 Byte
    }

    // SCHWANZ = KOPF; Wiederherstellen; weiter im Hook bei `pla`. Beim IRQ
    // geht es direkt hierher -- KOPF und SCHWANZ sind dann schon gleich.
    const uint32_t ende = kBlock + (uint32_t)c.size();
    for (size_t stelle : endeSprung) {
        c[stelle] = (uint8_t)(ende & 0xFF);
        c[stelle + 1] = (uint8_t)(ende >> 8);
    }
    b1({0xE2, 0x20, 0xA5, dp(p.kopf), 0x85, dp(p.schwanz), 0x7A, 0xFA, 0x2B, 0xAB, 0xC2, 0x20});
    if (p.takt == Takt::Nachlauf) {
        // pla (16 Bit) / sep #$20 / sta $2BFD (Puffer sechs Takte spaeter
        // ausgeblendet; nur ein Byte, A bleibt) / rti in den Originalrahmen
        b1({0x68, 0xE2, 0x20, 0x8F, (int)(kNmiVektorAus & 0xFF), (int)(kNmiVektorAus >> 8), 0x00, 0x40});
    } else {
        b1({0x4C, (int)(kRueckkehr & 0xFF), (int)(kRueckkehr >> 8)});
    }
    if (c.size() != (size_t)code) return Programm{};   // Groessenrechnung passt nicht zum Code

    // Zeigertabelle, KOPF, Daten, SCHWANZ
    for (uint16_t z : zeiger) b1({z & 0xFF, z >> 8});
    c.push_back(0x00);                                             // KOPF
    c.insert(c.end(), 2, 0);                                       // Stapelzeiger
    c.push_back(0x00);                                             // Vektor-Byte
    c.push_back(0x00);                                             // Laeufe
    if (p.takt == Takt::Austastung) c.push_back(0x00);             // MERKER
    c.insert(c.end(), (size_t)blockBytes + 2u * zeiger.size(), 0);  // Daten
    c.push_back(0xFF);                                             // SCHWANZ: noch kein Bild

    // Nur die verlangten Adressen melden.
    for (auto it = p.lage.begin(); it != p.lage.end();)
        it = auswahl.count(it->first) ? std::next(it) : p.lage.erase(it);

    p.eintraege = (int)zeiger.size() + bloecke;
    p.takte = 80 + 20 * bloecke + 7 * blockBytes + 15 * schleifen + 23 * (int)zeiger.size();
    p.sprung = {0x4C, (uint8_t)(kBlock & 0xFF), (uint8_t)(kBlock >> 8)};
    return p;
}

}  // namespace

namespace {

// Kleinroutine 26: Arbeit nach dem IRQ des Spiels statt davor.
//
// Hook-Teil (vom Hook-Ende aus, Stapel: A(2) und P vom Hook, darunter der
// Interrupt-Rahmen P, PCL, PCH, PB): merkt sich die Ruecksprungadresse des
// Spiels, setzt im Rahmen NACHLAUF ($00) ein und springt ohne den Schreib-
// zugriff auf $2BFD zum Vektor des Spiels ($2A6B: jmp ($FF77)). Der Puffer
// bleibt dadurch eingeblendet, bis der Nachlauf $2BFD schreibt. Steht im
// Rahmen schon NACHLAUF (Interrupt direkt nach dem rti des Spiels), bleibt er.
//
// Nachlauf (nach dem rti des Spiels, P = P des unterbrochenen Codes):
// sichert A/X/Y lang, baut aus der gemerkten Adresse einen Interrupt-Rahmen,
// wartet `nops` x nop, zaehlt, stellt A/X/Y her, schreibt $2BFD (Puffer wird
// sechs Takte spaeter ausgeblendet) und kehrt mit rti zurueck.
std::vector<uint8_t> Aufschub(uint32_t nops, uint32_t schleife) {
    std::vector<uint8_t> c;
    auto b = [&](std::initializer_list<int> x) { for (int v : x) c.push_back((uint8_t)v); };
    auto lang = [&](int op, uint32_t a) { b({op, (int)(a & 0xFF), (int)((a >> 8) & 0xFF), 0x00}); };

    // Hook-Teil; die Lage von NACHLAUF wird am Ende eingetragen.
    b({0xE2, 0x20});
    b({0xA3, 0x07, 0xD0, 0x0C});                 // lda 7,s (PB) / bne MERKEN
    b({0xA3, 0x06, 0xC9, 0x00, 0xD0, 0x06});     // lda 6,s / cmp #>NACHLAUF / bne MERKEN
    const size_t cmpHi = c.size() - 3;
    b({0xA3, 0x05, 0xC9, 0x00, 0xF0, 0x00});     // lda 5,s / cmp #<NACHLAUF / beq WEITER
    const size_t cmpLo = c.size() - 3, beqWeiter = c.size() - 1;
    const size_t merken = c.size();
    b({0xA3, 0x05}); lang(0x8F, kAufschubRueck);
    b({0xA3, 0x06}); lang(0x8F, kAufschubRueck + 1);
    b({0xA3, 0x07}); lang(0x8F, kAufschubRueck + 2);
    b({0xA9, 0x00, 0x83, 0x05});                 // lda #<NACHLAUF / sta 5,s
    const size_t setLo = c.size() - 3;
    b({0xA9, 0x00, 0x83, 0x06});                 // lda #>NACHLAUF / sta 6,s
    const size_t setHi = c.size() - 3;
    b({0xA9, 0x00, 0x83, 0x07});                 // lda #$00 / sta 7,s
    c[beqWeiter] = (uint8_t)(c.size() - merken);
    b({0xC2, 0x20, 0x68, 0x28});                 // WEITER: rep #$20 / pla / plp
    b({0x4C, (int)((kAusgang + 8) & 0xFF), (int)((kAusgang + 8) >> 8)});   // jmp $2A6B

    // Nachlauf
    const uint32_t nachlauf = kBlock + (uint32_t)c.size();
    c[cmpHi] = c[setHi] = (uint8_t)(nachlauf >> 8);
    c[cmpLo] = c[setLo] = (uint8_t)(nachlauf & 0xFF);
    b({0x08, 0x78, 0xC2, 0x30});                 // php / sei / rep #$30
    lang(0x8F, kAufschubA);                      // A, X, Y sichern (16 Bit)
    b({0x8A}); lang(0x8F, kAufschubX);
    b({0x98}); lang(0x8F, kAufschubY);
    b({0xE2, 0x20, 0x68}); lang(0x8F, kAufschubP);   // P des unterbrochenen Codes
    lang(0xAF, kAufschubRueck + 2); b({0x48});   // Rahmen: PB, PCH, PCL, P
    lang(0xAF, kAufschubRueck + 1); b({0x48});
    lang(0xAF, kAufschubRueck); b({0x48});
    lang(0xAF, kAufschubP); b({0x48});
    c.insert(c.end(), nops, 0xEA);
    if (schleife) b({0xA9, (int)(schleife & 0xFF), 0x3A, 0xD0, 0xFD});   // lda #n / dec / bne: 5 Takte je Runde
    lang(0xAF, kAufschubZaehler); b({0x1A}); lang(0x8F, kAufschubZaehler);
    b({0xC2, 0x30});
    lang(0xAF, kAufschubY); b({0xA8});
    lang(0xAF, kAufschubX); b({0xAA});
    lang(0xAF, kAufschubA);
    b({0xE2, 0x20}); lang(0x8F, kNmiVektorAus);  // nur ein Byte, A bleibt erhalten
    b({0x40});                                   // rti
    c.resize(kAufschubZaehler + 1 - kBlock, 0x00);
    return c;
}

}  // namespace

std::vector<uint8_t> Sonde() {
    const uint8_t al = kSondeAlle & 0xFF, ah = kSondeAlle >> 8, nl = kSondeNmi & 0xFF, nh = kSondeNmi >> 8;
    std::vector<uint8_t> c = {
        0xE2, 0x20,                                      // sep #$20
        0xAF, 0x6C, 0x2A, 0x00,                          // lda $002A6C (Vektor-Byte)
        0xC9, 0xEA, 0xD0, 0x09,                          // cmp #$EA / bne +9
        0xAF, nl, nh, 0x00, 0x1A, 0x8F, nl, nh, 0x00,    // NMI-Zaehler + 1
        0xAF, al, ah, 0x00, 0x1A, 0x8F, al, ah, 0x00,    // Durchlaeufe + 1
        0xC2, 0x20, 0x4C, (uint8_t)(kRueckkehr & 0xFF), (uint8_t)(kRueckkehr >> 8),
    };
    c.resize(kSondeNmi + 1 - kBlock, 0x00);
    return c;
}

std::vector<uint8_t> Kleinroutine(int art, uint32_t ziel) {
    if (art == 28) {
        // Zaehler + Warteschleife mit `ziel` Runden zu 5 Takten im Hook.
        const uint8_t zl0 = kKleinZaehler & 0xFF, zh0 = kKleinZaehler >> 8;
        std::vector<uint8_t> w = {0xE2, 0x20};
        if (ziel) for (int v : {0xA9, (int)(ziel & 0xFF), 0x3A, 0xD0, 0xFD}) w.push_back((uint8_t)v);
        for (int v : {0xAF, (int)zl0, (int)zh0, 0x00, 0x1A, 0x8F, (int)zl0, (int)zh0, 0x00, 0xC2, 0x20, 0x4C,
                      (int)(kRueckkehr & 0xFF), (int)(kRueckkehr >> 8)})
            w.push_back((uint8_t)v);
        w.resize(kKleinInAustastung + 1 - kBlock, 0x00);
        return w;
    }
    if (art == 26) return Aufschub(ziel, 0);
    if (art == 27) return Aufschub(0, ziel);
    constexpr uint32_t kAltD = kKleinZaehler + 1;   // gesicherte Direktseite (2 Byte)
    const uint8_t zl = kKleinZaehler & 0xFF, zh = kKleinZaehler >> 8, dl = kAltD & 0xFF, dh = kAltD >> 8;
    std::vector<uint8_t> c;
    auto b = [&](std::initializer_list<int> x) { for (int v : x) c.push_back((uint8_t)v); };
    const std::initializer_list<int> warten = {0xE2, 0x20, 0xA9, 0x10, 0x3A, 0xD0, 0xFD};   // sep #$20 / lda #16 / dec / bne
    if (art == 1) b(warten);
    if (art == 2) b({0x8B, 0x0B, 0x0B, 0x0B, 0x0B, 0x2B, 0x2B, 0x2B, 0x2B, 0xAB});   // phb, 4x phd, 4x pld, plb
    // 6-8: Anfang und Ende wie die Kopierroutine (DBR, D, X, Y sichern,
    // D = $2B00), dazwischen 7: Zugriffe ueber die Direktseite, 8: $2A6C lesen.
    const bool wieKopie = (art >= 6 && art <= 14) || (art >= 22 && art <= 25);
    if (wieKopie) b({0x8B, 0x0B, 0xC2, 0x30, 0xDA, 0x5A, 0xF4, 0x00, 0x2B, 0x2B, 0xE2, 0x20});
    if (art == 7) b({0xA9, 0xEE, 0x85, 0x3D, 0xE6, 0x3E, 0xA5, 0x3A, 0x85, 0x3F});
    if (art == 8) b({0xAF, 0x6C, 0x2A, 0x00, 0x8F, 0x3D, 0x2B, 0x00});
    // 22-24: je zwei der drei Zugriffe aus 7, in derselben Reihenfolge.
    if (art == 22) b({0xA9, 0xEE, 0x85, 0x3D, 0xE6, 0x3E});               // sta <$3D, inc <$3E
    if (art == 23) b({0xE6, 0x3E, 0xA5, 0x3A, 0x85, 0x3F});               // inc <$3E, lda <$3A / sta <$3F
    if (art == 24) b({0xA9, 0xEE, 0x85, 0x3D, 0xA5, 0x3A, 0x85, 0x3F});   // sta <$3D, lda <$3A / sta <$3F
    if (art == 25) c.insert(c.end(), ziel, 0xEA);  // `ziel` x nop, je 2 Takte, kein Speicherzugriff
    if (art == 10) b({0xA9, 0xEE, 0x85, 0x3D});   // lda #$EE / sta <$3D
    if (art == 11) b({0xE6, 0x3E});               // inc <$3E
    if (art == 12) b({0xA5, 0x3A, 0x85, 0x3F});   // lda <$3A / sta <$3F
    if (art == 13) b({0xA9, 0xEE, 0x8F, 0x30, 0x2B, 0x00});   // lda #$EE / sta $002B30
    if (art == 14) b({0xA9, 0xEE, 0x8F, 0x7D, 0x2B, 0x00});   // lda #$EE / sta $002B7D
    if (art == 15) b({0xE2, 0x20, 0xA9, 0xEE, 0x8F, 0x3D, 0x2B, 0x00});   // ohne Anfang: $EE nach $2B3D
    if (art == 17) {
        // S-DD1-Bankregister $4804-$4807 lesen und ablegen (Lesen ohne Nebenwirkung).
        b({0xE2, 0x20});
        for (uint32_t r = 0; r < 4; ++r) {
            const uint32_t a = kKleinBankregister + r;
            b({0xAF, (int)(0x04 + r), 0x48, 0x00, 0x8F, (int)(a & 0xFF), (int)(a >> 8), 0x00});
        }
    }
    if (art == 18) b({0xE2, 0x20, 0xA9, 0xEE, 0x8F, (int)(ziel & 0xFF), (int)((ziel >> 8) & 0xFF), 0x00});   // $EE nach ziel
    if (art == 16) {
        // Beginnt die Austastluecke (und damit der NMI) waehrend der Routine?
        //        sep #$20 / lda $004212 / bmi DRIN
        //        80 Takte warten / lda $004212 / bpl FERTIG
        //        UEBERLAPP + 1 / bra FERTIG
        // DRIN:  IN_AUSTASTUNG + 1
        // FERTIG:
        const uint8_t ul = kKleinUeberlapp & 0xFF, uh = kKleinUeberlapp >> 8;
        const uint8_t il = kKleinInAustastung & 0xFF, ih = kKleinInAustastung >> 8;
        b({0xE2, 0x20, 0xAF, 0x12, 0x42, 0x00, 0x30, 0x16});
        b({0xA9, 0x10, 0x3A, 0xD0, 0xFD, 0xAF, 0x12, 0x42, 0x00, 0x10, 0x14});
        b({0xAF, ul, uh, 0x00, 0x1A, 0x8F, ul, uh, 0x00, 0x80, 0x09});
        b({0xAF, il, ih, 0x00, 0x1A, 0x8F, il, ih, 0x00});
    }
    if (art == 4) b({0xE2, 0x20, 0xAF, 0x6C, 0x2A, 0x00});                 // lda $002A6C (Vektor-Byte)
    if (art == 5) b({0xC2, 0x30, 0xDA, 0x5A, 0x7A, 0xFA});                 // rep #$30 / phx / phy / ply / plx
    if (art == 3) {
        b({0xC2, 0x20, 0x7B, 0x8F, dl, dh, 0x00, 0xA9, 0x00, 0x2B, 0x5B});   // tdc / sta ALT_D / lda #$2B00 / tcd
        b(warten);
        b({0xC2, 0x20, 0xAF, dl, dh, 0x00, 0x5B});                           // lda ALT_D / tcd
    }
    b({0xE2, 0x20, 0xAF, zl, zh, 0x00, 0x1A, 0x8F, zl, zh, 0x00});            // Zaehler + 1
    if (wieKopie) b({0x7A, 0xFA, 0x2B, 0xAB});                               // ply / plx / pld / plb
    b({0xC2, 0x20, 0x4C, kRueckkehr & 0xFF, kRueckkehr >> 8});
    c.resize(kKleinInAustastung + 1 - kBlock, 0x00);
    return c;
}

std::vector<uint8_t> Messroutine() {
    static_assert(kMessRing + 4 * kMessEintraege <= kBlockEnde, "Messring passt nicht");
    const uint8_t idx = (uint8_t)(kMessIndex - kBlock), ring = (uint8_t)(kMessRing - kBlock);
    std::vector<uint8_t> c = {
        0x8B, 0x0B, 0xC2, 0x30, 0xDA, 0x5A, 0xF4, 0x00, 0x2B, 0x2B,  // sichern, D = $2B00
        0xE2, 0x30,                                                  // sep #$30
        0xAF, 0x37, 0x21, 0x00,                                      // lda $002137 (Zaehler einrasten)
        0xAF, 0x3F, 0x21, 0x00,                                      // lda $00213F (Status)
        0xA6, idx,                                                   // ldx <IDX
        0x95, (uint8_t)(ring + 3),                                   // sta <RING+3,x
        0xAF, 0x3D, 0x21, 0x00,                                      // lda $00213D (Zeile lo)
        0x95, (uint8_t)(ring + 1),                                   // sta <RING+1,x
        0xAF, 0x3D, 0x21, 0x00,                                      // lda $00213D (Zeile hi)
        0x29, 0x01,                                                  // and #$01
        0x95, (uint8_t)(ring + 2),                                   // sta <RING+2,x
        0xAF, 0x6C, 0x2A, 0x00,                                      // lda $002A6C (Vektor)
        0x95, ring,                                                  // sta <RING,x
        0x8A, 0x18, 0x69, 0x04, 0x29, (uint8_t)(4 * kMessEintraege - 1),  // txa / clc / adc #4 / and
        0x85, idx,                                                   // sta <IDX
        0xC2, 0x30, 0x7A, 0xFA, 0x2B, 0xAB, 0xC2, 0x20,              // wiederherstellen
        0x4C, (uint8_t)(kRueckkehr & 0xFF), (uint8_t)(kRueckkehr >> 8),
    };
    return c;
}

Bild Deute(const Programm& p, const std::vector<uint8_t>& gelesen) {
    Bild b;
    if (gelesen.size() < kLeseLaenge) return b;
    auto an = [&](uint32_t adresse) { return gelesen[adresse - kLeseStart]; };
    b.eingehaengt = an(kAusgang) == kBefehlNeu && an(kAusgang + 1) == kOperandNeu;
    b.zaehler = an(p.kopf);
    b.vollstaendig = an(p.kopf) == an(p.schwanz);
    b.stapel = (uint16_t)(an(p.stapel) | (an(p.stapel + 1) << 8));
    b.vektor = an(p.vektor);
    b.laeufe = an(p.laeufe);
    for (const auto& kv : p.lage) b.werte[kv.first] = an(kv.second);
    return b;
}

}  // namespace nmikopie
