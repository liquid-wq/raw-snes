// Selbsttest der Kernschicht: kein Geraet, keine Oberflaeche.
#include "ConditionEngine.h"
#include "Md5.h"
#include "fxpak_port.h"
#include "fxpak_protokoll.h"
#include "fxpak_sim.h"
#include "nmi_kopie.h"
#include "ra_cache.h"
#include "ra_client.h"
#include "rom_vergleich.h"
#include "snes_kopf.h"
#include "snes_speicher.h"
#include "worker.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <cstring>
#include <string>
#include <thread>

namespace {

int fehler = 0;

void Pruefe(bool ok, const char* was) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FEHLER", was);
    if (!ok) ++fehler;
}

void TestPakete() {
    std::printf("Protokoll\n");
    const fxpak::Paket l = fxpak::BaueLesen(fxpak::Raum::Snes, 0xF50123, 0x0200);
    Pruefe(l.size() == 512, "Lesen: 512 Byte");
    Pruefe(std::memcmp(l.data(), "USBA", 4) == 0, "Lesen: Kennung");
    Pruefe(l[4] == 0 && l[5] == 1 && l[6] == 0, "Lesen: Befehl/Raum/Schalter");
    Pruefe(l[252] == 0 && l[253] == 0 && l[254] == 0x02 && l[255] == 0x00, "Lesen: Laenge big-endian");
    Pruefe(l[256] == 0 && l[257] == 0xF5 && l[258] == 0x01 && l[259] == 0x23, "Lesen: Adresse big-endian");

    const fxpak::Paket m = fxpak::BaueLesenMehrfach(
        fxpak::Raum::Snes, {{0xF50010, 4}, {0xF6FFF0, 255}});
    Pruefe(m.size() == 64, "Mehrfach: 64 Byte");
    Pruefe(m[4] == 2 && m[6] == 128, "Mehrfach: Befehl und kleine Bloecke");
    Pruefe(m[32] == 4 && m[33] == 0xF5 && m[34] == 0x00 && m[35] == 0x10, "Mehrfach: Eintrag 1");
    Pruefe(m[36] == 255 && m[37] == 0xF6 && m[38] == 0xFF && m[39] == 0xF0, "Mehrfach: Eintrag 2");
    Pruefe(m[40] == 0, "Mehrfach: Eintrag 3 leer");

    uint8_t a[512] = {};
    std::memcpy(a, "USBA", 4);
    a[4] = 15; a[5] = 0; a[254] = 0x01; a[255] = 0x04;
    const fxpak::Antwort an = fxpak::DeuteAntwort(a);
    Pruefe(an.gueltig && an.fehler == 0 && an.laenge == 0x104, "Antwort deuten");
    a[4] = 0;
    Pruefe(!fxpak::DeuteAntwort(a).gueltig, "Antwort mit falschem Befehl verworfen");

    const fxpak::Paket s = fxpak::BaueSchreiben(fxpak::Raum::Befehlspuffer, 0x2B00, 160);
    Pruefe(s.size() == 512 && s[4] == 1 && s[5] == 3 && s[6] == (64 | 128), "Schreiben: Befehl 1, Raum 3, ohne Antwort, 64er-Bloecke");
    Pruefe(s[255] == 160 && s[258] == 0x2B && s[259] == 0x00, "Schreiben: Laenge und Adresse");

    Pruefe(fxpak::GesendeteBytes(0, 512) == 0, "Bloecke: 0");
    Pruefe(fxpak::GesendeteBytes(1, 512) == 512, "Bloecke: 1 -> 512");
    Pruefe(fxpak::GesendeteBytes(259, 64) == 320, "Bloecke: 259 -> 320");

    uint8_t info[512] = {};
    std::memcpy(info, "USBA", 4);
    info[4] = 15;
    std::strcpy((char*)&info[16], "/spiele/test.sfc");
    info[256] = 0x00; info[257] = 0x01; info[258] = 0x0B; info[259] = 0x00;
    std::strcpy((char*)&info[260], "1.11.0");
    std::strcpy((char*)&info[324], "sd2snes Mk.III");
    info[10] = 0x01;
    const fxpak::GeraeteInfo g = fxpak::DeuteInfo(info);
    Pruefe(g.rom == "/spiele/test.sfc" && g.version == "1.11.0" && g.geraet == "sd2snes Mk.III" &&
           g.firmwareNr == 0x00010B00 && g.einstellungen == 1, "Info deuten");
}

// Baut einen Header-Ausschnitt mit stimmiger Pruefsumme.
std::vector<uint8_t> KopfMuster(const char* titel, uint8_t modus) {
    std::vector<uint8_t> k(snes::kKopfLaenge, 0);
    std::memset(k.data(), ' ', 21);
    std::memcpy(k.data(), titel, std::strlen(titel));
    k[0x15] = modus;
    k[0x17] = 0x0A;  // 1 MiB
    k[0x18] = 0x03;  // 8 KiB
    k[0x1C] = 0x34; k[0x1D] = 0x12;  // Komplement 0x1234
    k[0x1E] = 0xCB; k[0x1F] = 0xED;  // Pruefsumme 0xEDCB
    return k;
}

void TestKopf() {
    std::printf("ROM-Header\n");
    std::vector<uint8_t> muell(snes::kKopfLaenge, 0xFF);

    snes::Kopf k = snes::Waehle({KopfMuster("SUPER TEST", 0x20), muell, {}});
    Pruefe(k.gefunden && k.abbildung == snes::Abbildung::LoRom && k.lage == 0x7FC0, "LoROM erkannt");
    Pruefe(k.titel == "SUPER TEST", "Titel ohne Leerzeichen rechts");
    Pruefe(k.romBytes == 1024 * 1024 && k.sramBytes == 8 * 1024, "Groessen");

    k = snes::Waehle({muell, KopfMuster("HI TEST", 0x31), {}});
    Pruefe(k.gefunden && k.abbildung == snes::Abbildung::HiRom && k.lage == 0xFFC0, "HiROM erkannt");

    k = snes::Waehle({muell, muell, muell});
    Pruefe(!k.gefunden, "Kein Header in Fuelldaten");
}

void TestDateiUndPfade() {
    std::printf("Dateien und Pfade\n");
    const fxpak::Paket d = fxpak::BaueDateiLesen("/roms/Super Metroid.sfc");
    Pruefe(d.size() == 512 && d[4] == 0 && d[5] == 0, "Datei lesen: Befehl 0, Raum Datei");
    Pruefe(std::strcmp((const char*)&d[256], "/roms/Super Metroid.sfc") == 0, "Datei lesen: Pfad ab 256");
    const fxpak::Paket k = fxpak::BaueKonfigLesen("EnableCheats");
    Pruefe(k[5] == 5 && std::strcmp((const char*)&k[256], "EnableCheats") == 0, "Konfiguration lesen");

    Pruefe(fxpak::IstMenue("") && fxpak::IstMenue("/sd2snes/m3nu.bin") && fxpak::IstMenue("/SD2SNES/MENU.BIN"),
           "Menue erkannt");
    Pruefe(!fxpak::IstMenue("/roms/zelda.sfc"), "Spiel ist kein Menue");
    Pruefe(fxpak::IstSnesRom("/a/B.SFC") && fxpak::IstSnesRom("x.smc") && !fxpak::IstSnesRom("x.gb") &&
           !fxpak::IstSnesRom("x.spc"), "SNES-Endungen");
}

void TestSpeicher() {
    std::printf("Speicherkarte und Leseplan\n");
    Pruefe(snes::FxPakAdresse(0x000000) == 0xF50000u, "WRAM-Anfang -> F50000");
    Pruefe(snes::FxPakAdresse(0x01FFFF) == 0xF6FFFFu, "WRAM-Ende -> F6FFFF");
    Pruefe(snes::FxPakAdresse(0x020000) == 0xE00000u, "SRAM-Anfang -> E00000");
    Pruefe(snes::FxPakAdresse(0x09FFFF) == 0xE7FFFFu, "SRAM-Ende -> E7FFFF");
    Pruefe(!snes::FxPakAdresse(0x0A0000), "SA-1-I-RAM nicht lesbar");

    // Zwei nahe Adressen -> ein Bereich; eine ferne -> zweiter Bereich; die
    // SRAM-Adresse direkt hinter dem WRAM darf nicht im WRAM-Bereich landen.
    snes::Leseplan p({0x0010, 0x0012, 0x0010, 0x1000, 0x1FFFF, 0x20000, 0x0A0001});
    Pruefe(p.AnzahlAdressen() == 5, "Plan: doppelte und unlesbare Adressen entfernt");
    Pruefe(p.Lesungen().size() == 1, "Plan: eine Lesung");
    const auto& b = p.Lesungen()[0].bereiche;
    Pruefe(b.size() == 4, "Plan: vier Bereiche");
    Pruefe(b[0].adresse == 0xF50010 && b[0].laenge == 3, "Plan: 0010..0012 zusammengefasst");
    Pruefe(b[2].adresse == 0xF6FFFF && b[2].laenge == 1 && b[3].adresse == 0xE00000,
           "Plan: Grenze WRAM/SRAM nicht ueberspannt");

    std::vector<std::vector<uint8_t>> antwort = {{0xA0, 0xA1, 0xA2, 0xB0, 0xC0, 0xD0}};
    const auto ram = p.Verteile(antwort);
    Pruefe(ram.size() == 5 && ram.at(0x0010) == 0xA0 && ram.at(0x0012) == 0xA2 && ram.at(0x1000) == 0xB0 &&
           ram.at(0x1FFFF) == 0xC0 && ram.at(0x20000) == 0xD0, "Plan: Antwort richtig verteilt");
    Pruefe(ram.find(0x0011) == ram.end(), "Plan: Fuellbyte nicht als Adresse gemeldet");

    // Langer Lauf: wird in Stuecke zu 255 Byte geteilt, ueber 8 Bereiche hinaus
    // in eine zweite Lesung.
    std::vector<uint32_t> viele;
    for (uint32_t a = 0; a < 255 * 9; ++a) viele.push_back(a);
    snes::Leseplan lang(viele);
    Pruefe(lang.Lesungen().size() == 2 && lang.Lesungen()[0].bereiche.size() == 8 &&
           lang.Lesungen()[1].bereiche.size() == 1 && lang.Lesungen()[0].bytes == 255 * 8,
           "Plan: 9 x 255 Byte -> 8 + 1 Bereiche");
}

void TestHash() {
    std::printf("SNES-Hash\n");
    std::vector<uint8_t> rom(0x8000);
    for (size_t i = 0; i < rom.size(); ++i) rom[i] = (uint8_t)(i * 7);
    std::vector<uint8_t> mitKopf(512, 0xEE);
    mitKopf.insert(mitKopf.end(), rom.begin(), rom.end());
    const std::string h1 = RaClient::snes_hash(rom);
    Pruefe(h1.size() == 32, "Hash: 32 Hexzeichen");
    Pruefe(RaClient::snes_hash(mitKopf) == h1, "Hash: 512-Byte-Kopiererheader wird uebersprungen");
    std::vector<uint8_t> ungerade(rom.begin(), rom.begin() + 1000);
    Pruefe(RaClient::snes_hash(ungerade) == rawsnes::md5Hex(ungerade), "Hash: sonst ganze Datei");
    Pruefe(rawsnes::md5Hex(std::string("abc")) == "900150983cd24fb0d6963f7d28e17f72", "MD5-Pruefwert (RFC 1321)");
}

void TestBedingungen() {
    std::printf("Bedingungen mit SNES-Adressen\n");
    rawsnes::AchievementRuntime wram("0xH0010=5");
    rawsnes::RamMap ram{{0x0010, 4}};
    rawsnes::RamMap prev = ram;
    Pruefe(!wram.unsupported(), "WRAM-Bedingung unterstuetzt");
    Pruefe(!wram.update(ram, &prev), "WRAM: feuert bei 4 nicht");
    prev = ram;
    ram[0x0010] = 5;
    Pruefe(wram.update(ram, &prev), "WRAM: feuert bei 5");

    rawsnes::AchievementRuntime sram("0xH020005=1");
    Pruefe(!sram.unsupported(), "SRAM-Bedingung unterstuetzt");
    rawsnes::AchievementRuntime iram("0xH0a0000=1");
    Pruefe(!iram.unsupported(), "SA-1-I-RAM-Bedingung gueltig (lesbar ueber die Werte-Kopie)");
    rawsnes::AchievementRuntime ausserhalb("0xH0a0800=1");
    Pruefe(ausserhalb.unsupported(), "Adresse hinter dem I-RAM als nicht unterstuetzt erkannt");
    rawsnes::AchievementRuntime hoch("0xH1fffe=1");
    Pruefe(!hoch.unsupported(), "Adresse oberhalb 0xFFFF unterstuetzt (Bank 7F)");

    // AddAddress (Zeiger): $0010 enthaelt den Versatz, geprueft wird $0020+Versatz.
    {
        rawsnes::AchievementRuntime zeiger("I:0xH0010_0xH0020=5");
        Pruefe(!zeiger.unsupported(), "AddAddress (I:) unterstuetzt");
        rawsnes::RamMap r{{0x0010, 3}, {0x0023, 4}, {0x0020, 5}};
        rawsnes::RamMap v = r;
        Pruefe(!zeiger.update(r, &v), "AddAddress: Ziel $0023=4 -> feuert nicht (obwohl $0020=5)");
        std::set<uint32_t> ziele;
        zeiger.zielAdressen(r, ziele);
        Pruefe(ziele.count(0x0023) == 1, "AddAddress: Zieladresse $0023 wird gemeldet");
        v = r;
        r[0x0023] = 5;
        Pruefe(zeiger.update(r, &v), "AddAddress: Ziel $0023=5 -> feuert");
        rawsnes::AchievementRuntime boo("I:0xH000db3_0xH001f11=0_0xH0013bf=19_0xH000dd5=2_0xH000dda=0");
        Pruefe(!boo.unsupported(), "SMW 'Shoo! Shoo! Big Boo' jetzt auswertbar");
    }
    // Fortschritt nur fuer Measured (M:), nicht fuer beliebige Trefferziele.
    {
        rawsnes::AchievementRuntime ohne("0xH0010=0.20._0xH0011>d0xH0011.8.");
        rawsnes::AchievementRuntime mit("M:0xH0010=1.5.");
        Pruefe(!ohne.progress().hasProgress, "Kein Fortschritt ohne Measured (kein 20/20 mehr)");
        Pruefe(mit.progress().hasProgress && mit.progress().target == 5, "Fortschritt bei Measured");
    }

    const auto adr = rawsnes::collectAddresses({{"1", "0xH1fffe=1_0x 0010=2"}});
    Pruefe(std::find(adr.begin(), adr.end(), 0x1fffeu) != adr.end() &&
           std::find(adr.begin(), adr.end(), 0x0011u) != adr.end(), "Adressen gesammelt (16 Bit = 2 Byte)");

    // Kommazahlen und Division wie bei RetroAchievements.
    {
        rawsnes::RamMap r{{0x0010, 7}, {0x0011, 4}};
        rawsnes::AchievementRuntime faktor("A:0xH0010*f2.5_0xH0011=21.5");   // 7*2.5 + 4
        Pruefe(!faktor.unsupported() && faktor.update(r, &r), "Faktor als Kommazahl (*f2.5)");
        rawsnes::AchievementRuntime konst("0xH0010>f6.5");
        Pruefe(!konst.unsupported() && konst.update(r, &r), "Kommazahl als Konstante (f6.5)");
        rawsnes::AchievementRuntime ganz("0xH0010/2=3");
        Pruefe(ganz.update(r, &r), "Ganzzahlige Division: 7/2 = 3");
        rawsnes::AchievementRuntime komma("0xH0010/f2.0=3.5");
        Pruefe(komma.update(r, &r), "Division durch Kommazahl: 7/2.0 = 3.5");
    }
    // Leaderboard-Werte: alle vier Formen.
    {
        rawsnes::RamMap r{{0x0010, 3}, {0x0011, 2}, {0x0012, 9}};
        rawsnes::ValueRuntime summe("0xH0010*100_0xH0011*10_0xH0012");
        summe.update(r, &r);
        Pruefe(!summe.unsupported() && summe.value() == 329, "Wert, alte Summenform: 3*100 + 2*10 + 9 = 329");
        rawsnes::ValueRuntime kette("A:0xH0010*f2.5_B:0xH0011_M:0xH0012");
        kette.update(r, &r);
        Pruefe(!kette.unsupported() && kette.value() == 14.5, "Wert, Kette A:/B:/M: = 7.5 - 2 + 9");
        rawsnes::ValueRuntime groesster("M:0xH0010$M:0xH0012$M:0xH0011");
        groesster.update(r, &r);
        Pruefe(!groesster.unsupported() && groesster.value() == 9, "Wert, Alternativen mit $: der groesste (9)");
        rawsnes::ValueRuntime bilder("M:0=0");
        for (int i = 0; i < 5; ++i) bilder.update(r, &r);
        Pruefe(!bilder.unsupported() && bilder.value() == 5, "Wert, Bildzaehler M:0=0 nach 5 Bildern = 5");
        bilder.reset();
        bilder.update(r, &r);
        Pruefe(bilder.value() == 1, "Bildzaehler beginnt nach reset() neu");
        rawsnes::ValueRuntime wechsel("M:0xH0010!=d0xH0010");
        rawsnes::RamMap v = r;
        wechsel.update(r, &v);
        r[0x0010] = 4;
        wechsel.update(r, &v);
        Pruefe(wechsel.value() == 1, "Wert, Wechselzaehler mit Delta: 1 Wechsel");
        Pruefe(rawsnes::ValueRuntime("A:0xH0010_0xH0011").unsupported(), "Kette ohne M: als nicht unterstuetzt erkannt");
        Pruefe(rawsnes::ValueRuntime("M:p0xH0010").unsupported(), "Prior im Wert als nicht unterstuetzt erkannt");
        Pruefe(rawsnes::ValueRuntime("").unsupported(), "Leerer Wert als nicht unterstuetzt erkannt");
    }
    // Leaderboard von Start bis Einreichen, Zeit mit Kommafaktor wie bei Super Mario World.
    {
        rawsnes::LeaderboardRuntime lb(1, "t", "FRAMES", "0xH0001=1", "0xH0002=1", "0xH0003=1",
                                       "A:0xH0010*f2.5_M:0xH0011");
        Pruefe(!lb.unsupported(), "Leaderboard mit Kommafaktor auswertbar");
        rawsnes::RamMap r{{0x0001, 0}, {0x0002, 0}, {0x0003, 0}, {0x0010, 4}, {0x0011, 1}};
        rawsnes::RamMap v = r;
        double wert = -1;
        Pruefe(lb.update(r, &v, &wert) == rawsnes::LeaderboardRuntime::Event::None, "Leaderboard: ohne Start nichts");
        v = r; r[0x0001] = 1;
        Pruefe(lb.update(r, &v, &wert) == rawsnes::LeaderboardRuntime::Event::Start, "Leaderboard: Start");
        v = r; r[0x0003] = 1;
        Pruefe(lb.update(r, &v, &wert) == rawsnes::LeaderboardRuntime::Event::Submit && wert == 11,
               "Leaderboard: Einreichen mit Wert 4*2.5 + 1 = 11");
    }
}

// ---------------------------------------------------------------------------
// Werte-Kopie ueber den NMI-Hook: Routine im Interpreter ausfuehren
// ---------------------------------------------------------------------------

std::vector<uint8_t> LeseBlock(FxPakSim& sim) {
    std::vector<uint8_t> g;
    for (uint32_t i = 0; i < nmikopie::kLeseLaenge; ++i) g.push_back(sim.Puffer(nmikopie::kLeseStart + i));
    return g;
}

std::vector<uint8_t> GanzerPuffer(FxPakSim& sim) {
    std::vector<uint8_t> g;
    for (uint32_t i = 0; i < nmikopie::kPufferGroesse; ++i) g.push_back(sim.Puffer(nmikopie::kPuffer + i));
    return g;
}

void Einspielen(FxPakSim& sim, const nmikopie::Programm& p) {
    sim.Schreibdaten(3, nmikopie::kSprung, p.sprung);
    sim.Schreibdaten(3, nmikopie::kBlock, p.block);
    for (const auto& s : nmikopie::Einhaengen()) sim.Schreibdaten(3, s.adresse, {s.wert});
}

void TestNmiKopie() {
    std::printf("Werte-Kopie ueber den NMI-Hook\n");
    FxPakSim sim;
    sim.SetzeSpiegel(false);
    sim.SetzeHook(true);

    Pruefe(nmikopie::Pruefe(GanzerPuffer(sim)) == nmikopie::Zustand::Original, "Hook-Ende im Original erkannt");
    {
        std::vector<uint8_t> falsch = GanzerPuffer(sim);
        falsch[nmikopie::kAusgang - nmikopie::kPuffer + 9] = 0x00;
        Pruefe(nmikopie::Pruefe(falsch) == nmikopie::Zustand::Unbekannt, "Fremdes Hook-Ende abgelehnt");
        std::vector<uint8_t> cheats = GanzerPuffer(sim);
        cheats[nmikopie::kCheatZahl - nmikopie::kPuffer] = 1;
        Pruefe(nmikopie::Pruefe(cheats) == nmikopie::Zustand::ZuVieleCheats, "WRAM-Cheat aktiv erkannt (belegt den Sprung)");
    }
    const std::vector<uint8_t> resetHook = [&]() {
        std::vector<uint8_t> r;
        for (uint32_t a = 0x2AA0; a < nmikopie::kSprung; ++a) r.push_back(sim.Puffer(a));
        return r;
    }();

    // WRAM unten, WRAM Bank 7F, I-RAM, dazu eine SRAM-Adresse (geht nicht in die Routine).
    const std::vector<uint32_t> adressen = {0x0010, 0x0011, 0x1FFFF, 0x10000, 0x0A0005, 0x020000, 0x0013};
    const nmikopie::Programm p = nmikopie::Erzeuge(adressen);
    Pruefe(p.block.size() <= nmikopie::kBlockEnde - nmikopie::kBlock, "Routine passt in $2B00-$2B9F");
    Pruefe(p.ohnePlatz.empty() && !p.lage.count(0x020000) && p.lage.count(0x0A0005) && p.lage.count(0x0013),
           "Auswahl: WRAM und I-RAM ja, SRAM nein");

    // Ohne Einhaengen bleibt alles beim Alten.
    sim.Schreibdaten(3, nmikopie::kSprung, p.sprung);
    sim.Schreibdaten(3, nmikopie::kBlock, p.block);
    std::string fehlerText;
    bool ok = sim.Bild(&fehlerText);
    Pruefe(ok && !nmikopie::Deute(p, LeseBlock(sim)).eingehaengt, "Nicht eingehaengt: Hook laeuft unveraendert");
    // Zwischenstand beim Einhaengen: nur der Operand geaendert.
    sim.Schreibdaten(3, nmikopie::kAusgang + 1, {nmikopie::kOperandNeu});
    ok = sim.Bild(&fehlerText);
    Pruefe(ok, ("Zwischenstand (rep #$78) laeuft sauber " + fehlerText).c_str());
    Einspielen(sim, p);
    Pruefe(nmikopie::Pruefe(GanzerPuffer(sim)) == nmikopie::Zustand::Eingehaengt, "Eingehaengt erkannt");
    {
        bool unberuehrt = true;
        for (uint32_t a = 0x2AA0; a < nmikopie::kSprung; ++a)
            if (sim.Puffer(a) != resetHook[a - 0x2AA0]) unberuehrt = false;
        Pruefe(unberuehrt, "Reset-Hook, Endmarke und Cheat-rts bis $2ADC unberuehrt");
    }

    sim.WramSchreiben(0x0010, 0x11);
    sim.WramSchreiben(0x0011, 0x22);
    sim.WramSchreiben(0x0013, 0x44);
    sim.WramSchreiben(0x1FFFF, 0x99);
    sim.WramSchreiben(0x10000, 0x77);
    sim.IramSchreiben(0x0005, 0x5A);
    bool alleOk = true, werteOk = true;
    std::set<uint16_t> stapelWerte;
    uint8_t zaehlerVorher = 0;
    for (int bild = 0; bild < 6; ++bild) {
        sim.WramSchreiben(0x0013, (uint8_t)(0x40 + bild));
        if (!sim.Bild(&fehlerText)) { alleOk = false; break; }
        const nmikopie::Bild b = nmikopie::Deute(p, LeseBlock(sim));
        if (!b.eingehaengt || !b.vollstaendig || (bild > 0 && b.zaehler != (uint8_t)(zaehlerVorher + 1))) werteOk = false;
        zaehlerVorher = b.zaehler;
        stapelWerte.insert(b.stapel);
        if (b.werte.at(0x0010) != 0x11 || b.werte.at(0x0011) != 0x22 || b.werte.at(0x0013) != 0x40 + bild ||
            b.werte.at(0x1FFFF) != 0x99 || b.werte.at(0x10000) != 0x77 || b.werte.at(0x0A0005) != 0x5A)
            werteOk = false;
    }
    Pruefe(alleOk, ("Sechs Bilder (8- und 16-Bit-Register): Zustand des Spiels wiederhergestellt " + fehlerText).c_str());
    Pruefe(werteOk, "Werte, Zaehler und Vollstaendigkeit stimmen in jedem Bild");
    Pruefe(stapelWerte.size() == 1 && *stapelWerte.begin() > 0x1F00 && *stapelWerte.begin() < 0x1FF0,
           "Stapelzeiger je Bild erfasst (Ruhestellung gleich)");
    {
        // IRQs laufen durch denselben Hook, duerfen aber nichts kopieren.
        const uint8_t vorIrq = nmikopie::Deute(p, LeseBlock(sim)).zaehler;
        sim.WramSchreiben(0x0013, 0x77);
        bool irqOk = true;
        for (int i = 0; i < 4; ++i) irqOk = irqOk && sim.Irq(&fehlerText);
        const nmikopie::Bild nachIrq = nmikopie::Deute(p, LeseBlock(sim));
        Pruefe(irqOk && nachIrq.zaehler == vorIrq && nachIrq.vollstaendig && nachIrq.werte.at(0x0013) != 0x77,
               ("IRQ durch den Hook: nichts kopiert, Zustand wiederhergestellt " + fehlerText).c_str());
    }

    // Halb geschriebenes Bild wird erkannt.
    {
        std::vector<uint8_t> g = LeseBlock(sim);
        g[p.schwanz - nmikopie::kLeseStart] ^= 0xFF;
        Pruefe(!nmikopie::Deute(p, g).vollstaendig, "Halbes Bild (Schwanz != Kopf) verworfen");
    }

    // Aushaengen stellt das Original wieder her, die Routine laeuft nicht mehr.
    for (const auto& s : nmikopie::Aushaengen()) sim.Schreibdaten(3, s.adresse, {s.wert});
    const uint8_t vorher = nmikopie::Deute(p, LeseBlock(sim)).zaehler;
    ok = sim.Bild(&fehlerText);
    Pruefe(ok && nmikopie::Pruefe(GanzerPuffer(sim)) == nmikopie::Zustand::Original &&
           nmikopie::Deute(p, LeseBlock(sim)).zaehler == vorher, "Ausgehaengt: Original, Routine ruht");

    // Zusammenhaengender Bereich: Blockkopie statt Woerter, viel mehr Platz.
    {
        std::vector<uint32_t> lauf;
        for (uint32_t a = 0x0200; a < 0x022B; ++a) lauf.push_back(a);
        for (uint32_t a = 0x11000; a < 0x11010; ++a) lauf.push_back(a);
        lauf.push_back(0x0A07FF);
        const nmikopie::Programm blk = nmikopie::Erzeuge(lauf);
        Pruefe(blk.ohnePlatz.empty() && blk.lage.size() == lauf.size(),
               "Blockkopie: 60 Byte aus drei Bereichen passen komplett");
        Einspielen(sim, blk);
        for (uint32_t a : lauf) {
            if (a < 0x20000) sim.WramSchreiben(a, (uint8_t)(a ^ 0x5A));
            else sim.IramSchreiben(a - 0x0A0000, 0xC3);
        }
        ok = sim.Bild(&fehlerText);
        const nmikopie::Bild bb = nmikopie::Deute(blk, LeseBlock(sim));
        bool richtig = ok && bb.vollstaendig && bb.werte.size() == lauf.size();
        for (uint32_t a : lauf)
            if (bb.werte.at(a) != (a < 0x20000 ? (uint8_t)(a ^ 0x5A) : 0xC3)) richtig = false;
        Pruefe(richtig, ("Blockkopie: Werte stimmen, Zustand wiederhergestellt " + fehlerText).c_str());
        std::printf("      (%d Eintraege, ca. %d Takte je Bild, %zu Byte)\n", blk.eintraege, blk.takte, blk.block.size());
    }

    // Ganze Achievements statt einzelner Adressen: jede Gruppe ganz oder gar nicht.
    {
        std::vector<nmikopie::Gruppe> gruppen;
        for (uint32_t i = 0; i < 30; ++i) {
            const uint32_t a = 0x1000 + i * 0x10;
            gruppen.push_back({1, {a, a + 0x400, a + 0x800}});
        }
        gruppen.push_back({2, {0x5000}});   // Leaderboard: erst nach allen Achievements
        std::vector<bool> drin;
        const nmikopie::Programm gp = nmikopie::ErzeugeGruppen(gruppen, &drin);
        int ganz = 0;
        bool halbe = false;
        for (size_t i = 0; i < gruppen.size(); ++i) {
            int n = 0;
            for (uint32_t a : gruppen[i].adressen) n += gp.lage.count(a) ? 1 : 0;
            if (n == (int)gruppen[i].adressen.size()) ++ganz;
            else if (n > 0) halbe = true;
            if ((n > 0) != drin[i]) halbe = true;
        }
        Pruefe(!halbe && ganz >= 5 && gp.block.size() <= nmikopie::kBlockEnde - nmikopie::kBlock,
               ("Gruppen: " + std::to_string(ganz) + " ganz dabei, keine halb").c_str());
        Einspielen(sim, gp);
        ok = sim.Bild(&fehlerText);
        Pruefe(ok && nmikopie::Deute(gp, LeseBlock(sim)).vollstaendig, ("Gruppen-Routine laeuft " + fehlerText).c_str());
    }

    // Messroutine: haelt je Durchlauf Vektor, Zeile, Status fest.
    {
        sim.Schreibdaten(3, nmikopie::kSprung, {0x4C, 0x00, 0x2B});
        sim.Schreibdaten(3, nmikopie::kBlock, nmikopie::Messroutine());
        sim.Schreibdaten(3, nmikopie::kMessIndex, {0});
        for (const auto& s : nmikopie::Einhaengen()) sim.Schreibdaten(3, s.adresse, {s.wert});
        bool mok = sim.Bild(&fehlerText) && sim.Irq(&fehlerText) && sim.Irq(&fehlerText);
        const bool ring = sim.Puffer(nmikopie::kMessIndex) == 12 && sim.Puffer(nmikopie::kMessRing) == 0xEA &&
                          sim.Puffer(nmikopie::kMessRing + 4) == 0xEE && sim.Puffer(nmikopie::kMessRing + 8) == 0xEE;
        Pruefe(mok && ring, ("Messroutine: drei Durchlaeufe festgehalten, Zustand wiederhergestellt " + fehlerText).c_str());
        for (const auto& s : nmikopie::Aushaengen()) sim.Schreibdaten(3, s.adresse, {s.wert});
    }

    // Kleinroutinen: zaehlen, Zustand bleibt erhalten.
    for (int art : {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16, 17, 18, 22, 23, 24, 25, 28}) {
        sim.Schreibdaten(3, nmikopie::kSprung, {0x4C, 0x00, 0x2B});
        sim.Schreibdaten(3, nmikopie::kBlock, nmikopie::Kleinroutine(art, art == 25 ? 8 : art == 28 ? 100 : 0x2B6E));
        for (const auto& s : nmikopie::Einhaengen()) sim.Schreibdaten(3, s.adresse, {s.wert});
        sim.NimmSeitenGeaendert();
        const bool kok = sim.Bild(&fehlerText) && sim.Irq(&fehlerText, 120) && sim.Irq(&fehlerText, 250);
        Pruefe(kok && sim.Puffer(nmikopie::kKleinZaehler) == 3,
               ("Kleinroutine " + std::to_string(art) + ": zaehlt, Zustand wiederhergestellt " + fehlerText).c_str());
        const bool verstellt = sim.NimmSeitenGeaendert();
        const bool sollVerstellen = art == 3 || (art >= 6 && art <= 14) || (art >= 22 && art <= 25);
        Pruefe(verstellt == sollVerstellen,
               ("Kleinroutine " + std::to_string(art) + (sollVerstellen ? ": verstellt die Direktseite" : ": Direktseite bleibt unveraendert")).c_str());
        if (art == 18)
            Pruefe(sim.Puffer(0x2B6E) == 0xEE && sim.Puffer(0x2B6D) == 0x00 && sim.Puffer(0x2B6F) == 0x00,
                   "Kleinroutine 18: schreibt genau an die verlangte Stelle");
        if (art == 17)   // Der Simulator liefert fuer $480x $FF (nicht belegt).
            Pruefe(sim.Puffer(nmikopie::kKleinBankregister) == 0xFF && sim.Puffer(nmikopie::kKleinBankregister + 3) == 0xFF,
                   "Kleinroutine 17: Bankregister abgelegt");
        if (art == 16)
            Pruefe(sim.Puffer(nmikopie::kKleinUeberlapp) == 0 && sim.Puffer(nmikopie::kKleinInAustastung) == 2,
                   "Kleinroutine 16: NMI und IRQ in Zeile 250 in der Austastluecke, IRQ in Zeile 120 nicht");
        for (const auto& s : nmikopie::Aushaengen()) sim.Schreibdaten(3, s.adresse, {s.wert});
    }

    // Kleinroutine 28 (Zeitbudget-Test): `ziel` Runden zu je 2 Befehlen
    // (dec/bne, 5 Takte) zusaetzlich zur Grundform ohne Warteschleife.
    {
        int grund = 0;
        bool alle = true;
        std::string werte;
        for (uint32_t ziel : {0u, 50u, 100u, 200u}) {
            sim.Schreibdaten(3, nmikopie::kSprung, {0x4C, 0x00, 0x2B});
            sim.Schreibdaten(3, nmikopie::kBlock, nmikopie::Kleinroutine(28, ziel));
            for (const auto& s : nmikopie::Einhaengen()) sim.Schreibdaten(3, s.adresse, {s.wert});
            const uint8_t vorher = sim.Puffer(nmikopie::kKleinZaehler);
            const bool ok = sim.Bild(&fehlerText) && sim.Puffer(nmikopie::kKleinZaehler) == (uint8_t)(vorher + 1);
            if (ziel == 0) grund = sim.HookBefehle();
            const int mehr = sim.HookBefehle() - grund;
            alle = alle && ok && mehr == (ziel ? 1 + 2 * (int)ziel : 0);   // lda # + Runden
            werte += " " + std::to_string(ziel) + ":+" + std::to_string(mehr);
            for (const auto& s : nmikopie::Aushaengen()) sim.Schreibdaten(3, s.adresse, {s.wert});
        }
        Pruefe(alle, ("Kleinroutine 28: Warteschleife mit 0/50/100/200 Runden, zusaetzliche Befehle" + werte + " " +
                      fehlerText).c_str());
    }

    // Sonde: zaehlt Durchlaeufe und NMIs, Zustand bleibt erhalten.
    {
        sim.Schreibdaten(3, nmikopie::kSprung, {0x4C, 0x00, 0x2B});
        sim.Schreibdaten(3, nmikopie::kBlock, nmikopie::Sonde());
        for (const auto& s : nmikopie::Einhaengen()) sim.Schreibdaten(3, s.adresse, {s.wert});
        sim.NimmSeitenGeaendert();
        const bool sok = sim.Bild(&fehlerText) && sim.Irq(&fehlerText, 120) && sim.Bild(&fehlerText);
        Pruefe(sok && sim.Puffer(nmikopie::kSondeAlle) == 3 && sim.Puffer(nmikopie::kSondeNmi) == 2 &&
                   !sim.NimmSeitenGeaendert(),
               ("Sonde: 3 Durchlaeufe, davon 2 NMI, Zustand wiederhergestellt " + fehlerText).c_str());
        for (const auto& s : nmikopie::Aushaengen()) sim.Schreibdaten(3, s.adresse, {s.wert});
    }

    // Kleinroutine 26 (Aufschub): wartet erst nach dem IRQ des Spiels; danach
    // muss das Spiel genau dort weiterlaufen, wo es unterbrochen wurde.
    {
        const std::vector<uint8_t> k = nmikopie::Kleinroutine(26, 8);
        bool frei = k.size() == nmikopie::kAufschubZaehler + 1 - nmikopie::kBlock;
        for (uint32_t a = nmikopie::kAufschubRueck; a <= nmikopie::kAufschubZaehler; ++a)
            frei = frei && k[a - nmikopie::kBlock] == 0;
        Pruefe(frei, "Kleinroutine 26: Code endet vor den Variablen");
        sim.Schreibdaten(3, nmikopie::kSprung, {0x4C, 0x00, 0x2B});
        sim.Schreibdaten(3, nmikopie::kBlock, k);
        for (const auto& s : nmikopie::Einhaengen()) sim.Schreibdaten(3, s.adresse, {s.wert});
        const int vorher = sim.Nachlaeufe();
        sim.NimmSeitenGeaendert();
        const bool aok = sim.Irq(&fehlerText, 120) && sim.Irq(&fehlerText, 250) && sim.Bild(&fehlerText);
        Pruefe(aok && sim.Nachlaeufe() == vorher + 3 && sim.Puffer(nmikopie::kAufschubZaehler) == 3,
               ("Kleinroutine 26: Nachlauf nach dem IRQ, Rueckkehr mit allen Registern " + fehlerText).c_str());
        Pruefe(!sim.NimmSeitenGeaendert(), "Kleinroutine 26: Direktseite und Datenbank unveraendert");
        for (const auto& s : nmikopie::Aushaengen()) sim.Schreibdaten(3, s.adresse, {s.wert});

        const std::vector<uint8_t> k27 = nmikopie::Kleinroutine(27, 200);
        bool frei27 = true;
        for (uint32_t a = nmikopie::kAufschubRueck; a <= nmikopie::kAufschubZaehler; ++a)
            frei27 = frei27 && k27[a - nmikopie::kBlock] == 0;
        sim.Schreibdaten(3, nmikopie::kBlock, k27);
        for (const auto& s : nmikopie::Einhaengen()) sim.Schreibdaten(3, s.adresse, {s.wert});
        const int vor27 = sim.Nachlaeufe();
        const uint8_t z27 = sim.Puffer(nmikopie::kAufschubZaehler);
        const bool ok27 = sim.Irq(&fehlerText, 120) && sim.Bild(&fehlerText);
        Pruefe(frei27 && ok27 && sim.Nachlaeufe() == vor27 + 2 && sim.Puffer(nmikopie::kAufschubZaehler) == (uint8_t)(z27 + 2),
               ("Kleinroutine 27: Warteschleife im Nachlauf " + fehlerText).c_str());
        for (const auto& s : nmikopie::Aushaengen()) sim.Schreibdaten(3, s.adresse, {s.wert});
    }

    // Jeder Durchlauf ein Bild (S-DD1: ein IRQ je Bild, kein NMI im Hook).
    {
        const nmikopie::Programm jp = nmikopie::Erzeuge({0x0040, 0x0123}, nmikopie::Takt::JederLauf);
        Einspielen(sim, jp);
        const uint8_t z0 = nmikopie::Deute(jp, LeseBlock(sim)).zaehler;
        sim.WramSchreiben(0x0040, 7);
        bool jok = sim.Irq(&fehlerText, 120);
        const nmikopie::Bild b1 = nmikopie::Deute(jp, LeseBlock(sim));
        sim.WramSchreiben(0x0040, 8);
        jok = jok && sim.Irq(&fehlerText, 120);
        const nmikopie::Bild b2 = nmikopie::Deute(jp, LeseBlock(sim));
        Pruefe(jok && b1.zaehler == (uint8_t)(z0 + 1) && b1.werte.at(0x0040) == 7 && b2.zaehler == (uint8_t)(z0 + 2) &&
               b2.werte.at(0x0040) == 8 && b2.vollstaendig && b2.vektor == 0xEE,
               ("Takt jeder Durchlauf: Kopie bei jedem IRQ " + fehlerText).c_str());
    }

    // Nachlauf (S-DD1): die Routine laeuft erst nach dem IRQ des Spiels.
    {
        std::vector<uint32_t> adr = {0x0040, 0x0123};
        for (uint32_t i = 0; i < 12; ++i) adr.push_back(0x0300 + i * 0x31);
        const nmikopie::Programm np = nmikopie::Erzeuge(adr, nmikopie::Takt::Nachlauf);
        Pruefe(!np.block.empty() && np.block.size() <= nmikopie::kBlockEnde - nmikopie::kBlock && np.ohnePlatz.empty(),
               ("Nachlauf: Routine passt (" + std::to_string(np.block.size()) + " Byte)").c_str());
        Einspielen(sim, np);
        const int vorher = sim.Nachlaeufe();
        const uint8_t z0 = nmikopie::Deute(np, LeseBlock(sim)).zaehler;
        sim.WramSchreiben(0x0040, 0x11);
        sim.WramSchreiben(0x0300 + 5 * 0x31, 0x77);
        bool nok = sim.Irq(&fehlerText, 120);
        const nmikopie::Bild n1 = nmikopie::Deute(np, LeseBlock(sim));
        sim.WramSchreiben(0x0040, 0x12);
        nok = nok && sim.Irq(&fehlerText, 120);
        const nmikopie::Bild n2 = nmikopie::Deute(np, LeseBlock(sim));
        Pruefe(nok && sim.Nachlaeufe() == vorher + 2, ("Nachlauf: nach dem IRQ, Rueckkehr mit allen Registern " + fehlerText).c_str());
        Pruefe(n1.zaehler == (uint8_t)(z0 + 1) && n1.werte.at(0x0040) == 0x11 && n1.werte.at(0x0300 + 5 * 0x31) == 0x77 &&
                   n2.zaehler == (uint8_t)(z0 + 2) && n2.werte.at(0x0040) == 0x12 && n2.vollstaendig && n2.vektor == 0xEE,
               "Nachlauf: Werte je IRQ kopiert");
        for (const auto& s : nmikopie::Aushaengen()) sim.Schreibdaten(3, s.adresse, {s.wert});
    }

    // Takt ueber die Austastluecke (Super FX): nur IRQs, mehrere je Bild.
    // Die Austastluecke beginnt im Simulator bei Zeile 225.
    {
        const nmikopie::Programm zp = nmikopie::Erzeuge({0x0040}, nmikopie::Takt::Austastung);
        Einspielen(sim, zp);
        bool zok = true;
        auto irq = [&](int zeile) { zok = zok && sim.Irq(&fehlerText, zeile); };
        const uint8_t z0 = nmikopie::Deute(zp, LeseBlock(sim)).zaehler;
        // Bild A: ausserhalb, dann zweimal in der Austastluecke -> eine Kopie (die erste).
        irq(30);
        sim.WramSchreiben(0x0040, 2);
        irq(250);
        sim.WramSchreiben(0x0040, 102);
        irq(280);
        const nmikopie::Bild ba = nmikopie::Deute(zp, LeseBlock(sim));
        // Bild B: kein IRQ in der Austastluecke -> keine Kopie.
        irq(40);
        irq(207);
        const nmikopie::Bild bb = nmikopie::Deute(zp, LeseBlock(sim));
        // Bild C: wieder einer in der Austastluecke.
        sim.WramSchreiben(0x0040, 3);
        irq(260);
        const nmikopie::Bild bc = nmikopie::Deute(zp, LeseBlock(sim));
        Pruefe(zok && ba.zaehler == (uint8_t)(z0 + 1) && ba.werte.at(0x0040) == 2 && bb.zaehler == ba.zaehler &&
               bc.zaehler == (uint8_t)(z0 + 2) && bc.werte.at(0x0040) == 3 && bc.vollstaendig,
               ("Takt ueber die Austastluecke: eine Kopie je Bild, die erste in der Luecke " + fehlerText).c_str());
        // Laeuft das NMI doch durch den Hook: es kopiert, ein IRQ danach nicht mehr.
        bool nok = sim.Bild(&fehlerText);
        irq(270);
        const uint8_t nach = nmikopie::Deute(zp, LeseBlock(sim)).zaehler;
        nok = nok && sim.Bild(&fehlerText);
        Pruefe(nok && zok && nach == (uint8_t)(z0 + 3) && nmikopie::Deute(zp, LeseBlock(sim)).zaehler == (uint8_t)(z0 + 4),
               "Takt ueber die Austastluecke: NMI kopiert, IRQ danach im selben Bild nicht");
    }

    // Viele verstreute Adressen: was nicht passt, wird gemeldet; der Rest laeuft.
    std::vector<uint32_t> viele;
    for (uint32_t a = 0; a < 200; ++a) viele.push_back(a * 37);
    viele.push_back(0x0A0100);
    const nmikopie::Programm gross = nmikopie::Erzeuge(viele);
    Pruefe(gross.block.size() <= nmikopie::kBlockEnde - nmikopie::kBlock && !gross.ohnePlatz.empty() &&
           gross.eintraege > 10, "Zu viele Adressen: Routine passt, Rest als 'ohne Platz' gemeldet");
    Einspielen(sim, gross);
    for (uint32_t a : viele) if (a < 0x20000) sim.WramSchreiben(a, (uint8_t)(a * 3));
    ok = sim.Bild(&fehlerText);
    const nmikopie::Bild b = nmikopie::Deute(gross, LeseBlock(sim));
    bool stimmt = ok && b.vollstaendig;
    for (const auto& kv : b.werte)
        if (kv.first < 0x20000 && kv.second != (uint8_t)(kv.first * 3) &&
            std::find(viele.begin(), viele.end(), kv.first) != viele.end()) stimmt = false;
    Pruefe(stimmt, ("Volle Routine laeuft, Werte stimmen " + fehlerText).c_str());    std::printf("      (%d Woerter, ca. %d Takte je Bild, %zu Byte)\n", gross.eintraege, gross.takte, gross.block.size());
}

// ---------------------------------------------------------------------------
// Gegen das simulierte Modul
// ---------------------------------------------------------------------------

// Eine kleine LoROM mit gueltigem Header.
std::vector<uint8_t> TestRom() {
    std::vector<uint8_t> rom(0x8000);
    for (size_t i = 0; i < rom.size(); ++i) rom[i] = (uint8_t)(i * 13 + 1);
    uint8_t* k = &rom[0x7FC0];
    std::memset(k, ' ', 21);
    std::memcpy(k, "RAWSNES TEST", 12);
    k[0x15] = 0x20;
    k[0x17] = 0x05;  // 32 KiB
    k[0x18] = 0x01;
    k[0x1C] = 0x34; k[0x1D] = 0x12;
    k[0x1E] = 0xCB; k[0x1F] = 0xED;
    return rom;
}

std::shared_ptr<FxPakSim> NeuesSim(const std::string& romPfad, const std::vector<uint8_t>& datei,
                                   const std::vector<uint8_t>& rom) {
    auto sim = std::make_shared<FxPakSim>();
    sim->SetzeRom(romPfad);
    sim->Datei(romPfad, datei);
    sim->Speicher(0, rom);
    sim->Konfig("EnableCheats", "false");
    return sim;
}

void TestPortGegenSim() {
    std::printf("Port gegen simuliertes Modul\n");
    const auto rom = TestRom();
    std::vector<uint8_t> gross(300000);
    for (size_t i = 0; i < gross.size(); ++i) gross[i] = (uint8_t)(i ^ (i >> 8));
    auto sim = NeuesSim("/roms/test.sfc", gross, rom);
    sim->WramSchreiben(0x0010, 0x42);
    sim->WramSchreiben(0x1FFFF, 0x99);

    FxPakPort p("SIM", SimLeitung(sim));
    bool ok = true;
    try { p.Oeffnen(); } catch (...) { ok = false; }
    Pruefe(ok, "Oeffnen mit Info-Probe");
    if (!ok) return;
    const auto info = p.Info();
    Pruefe(info.rom == "/roms/test.sfc" && info.version == "1.11.0", "Info: ROM und Firmware");
    const Bytes w = p.Lesen(fxpak::kWram + 0x10, 1);
    Pruefe(w.size() == 1 && w[0] == 0x42, "Lesen: WRAM-Byte");
    const Bytes r = p.Lesen(0, 2048);
    Pruefe(r.size() == 2048 && std::equal(r.begin(), r.end(), rom.begin()), "Lesen: 2 KiB ROM (vier Bloecke)");
    const Bytes m = p.LesenMehrfach({{fxpak::kWram + 0x10, 1}, {fxpak::kWram + 0x1FFFF, 1}, {0x7FC0, 12}});
    Pruefe(m.size() == 14 && m[0] == 0x42 && m[1] == 0x99 && std::memcmp(&m[2], "RAWSNES TEST", 12) == 0,
           "LesenMehrfach: drei Bereiche lueckenlos");
    uint32_t letzterStand = 0;
    const Bytes d = p.DateiLesen("/roms/test.sfc", [&](uint32_t g, uint32_t) { letzterStand = g; });
    Pruefe(d == gross && letzterStand == gross.size(), "Datei: 300000 Byte mit Fortschritt");
    {
        // Dateibefehle: umbenennen, schreiben, zuruecklesen, loeschen.
        std::vector<uint8_t> kern(142565);
        for (size_t i = 0; i < kern.size(); ++i) kern[i] = (uint8_t)(i * 13 ^ (i >> 7));
        sim->Datei("/test/datei.bin", {1, 2, 3});
        bool ok = true;
        try {
            p.Umbenennen("/test/datei.bin", "datei.bin.orig");
            p.DateiSchreiben("/test/datei.bin", kern);
        } catch (...) { ok = false; }
        Pruefe(ok && p.DateiLesen("/test/datei.bin") == kern &&
                   p.DateiLesen("/test/datei.bin.orig") == std::vector<uint8_t>{1, 2, 3},
               "Datei schreiben (142565 Byte) und umbenennen, zurueckgelesen");
        bool geworfenZiel = false;
        try { p.Umbenennen("/test/datei.bin", "datei.bin.orig"); } catch (...) { geworfenZiel = true; }
        Pruefe(geworfenZiel, "Umbenennen auf vorhandenen Namen: Fehler gemeldet");
        bool weg = false;
        try {
            p.DateiLoeschen("/test/datei.bin");
            p.Umbenennen("/test/datei.bin.orig", "datei.bin");
            weg = p.DateiLesen("/test/datei.bin") == std::vector<uint8_t>{1, 2, 3};
        } catch (...) {}
        Pruefe(weg, "Loeschen und zurueckbenennen: Original wieder da");
        Pruefe(p.Info().rom == "/roms/test.sfc", "Nach Dateibefehlen: Leitung synchron");
    }
    bool geworfen = false;
    try { p.DateiLesen("/fehlt.sfc"); } catch (...) { geworfen = true; }
    Pruefe(geworfen, "Datei fehlt: Fehler gemeldet");
    Pruefe(p.Info().rom == "/roms/test.sfc", "Nach Fehler: Leitung weiter synchron");
    Pruefe(p.KonfigLesen("EnableCheats") == std::optional<std::string>("false"), "Konfiguration: Wert");
    Pruefe(!p.KonfigLesen("GibtEsNicht"), "Konfiguration: unbekannter Schluessel");
    Pruefe(p.Info().rom == "/roms/test.sfc" && p.Lesen(fxpak::kWram + 0x10, 1).at(0) == 0x42,
           "Nach unbekanntem Schluessel: nachgeschickter Block abgeholt, Leitung synchron");

    // ROM-Vergleich: 196 KiB, also ein angebrochener letzter
    // Happen; Datei mit Kopierer-Vorspann.
    {
        std::vector<uint8_t> big(196 * 1024);
        for (size_t i = 0; i < big.size(); ++i) big[i] = (uint8_t)(i * 7 ^ (i >> 9));
        std::vector<uint8_t> datei(512, 0xEE);
        datei.insert(datei.end(), big.begin(), big.end());
        sim->Speicher(0, big);
        const romvergleich::Ergebnis gleich = romvergleich::Vergleiche(p, datei);
        Pruefe(gleich.verglichen == big.size() && gleich.abweichend == 0 && gleich.kurz == 0,
               "ROM-Vergleich: gleiche ROM, Vorspann uebersprungen, keine Abweichung");
        sim->Speicher(0x01234, {(uint8_t)(big[0x01234] ^ 0xFF)});
        sim->Speicher(0x30D3F, {(uint8_t)(big[0x30D3F] ^ 0x01)});
        const romvergleich::Ergebnis zwei = romvergleich::Vergleiche(p, datei);
        Pruefe(zwei.abweichend == 2 && zwei.bleibend == 2 && zwei.beispiele.size() == 2 &&
                   zwei.beispiele[0].rfind("$001234:", 0) == 0 && zwei.beispiele[1].rfind("$030D3F:", 0) == 0,
               "ROM-Vergleich: zwei veraenderte Byte gefunden (auch im letzten Happen)");
        sim->Speicher(0, std::vector<uint8_t>(big.size(), 0));
        sim->Speicher(0, rom);
    }

    // Langsame Antwort (Menue beschaeftigt): 4 s statt der erwarteten 3 s.
    // Der Port darf in der Zwischenzeit nichts senden, sonst verstopft das
    // Modul -- er muss die verspaetete Antwort abwarten und verwerfen.
    {
        sim->NaechsteAntwortVerzoegern(4000);
        bool ersteScheitert = false;
        try { p.Info(); } catch (...) { ersteScheitert = true; }
        bool zweiteGeht = false;
        try { zweiteGeht = p.Info().rom == "/roms/test.sfc"; } catch (...) {}
        Pruefe(ersteScheitert, "Langsame Antwort: erste Anfrage laeuft in die Zeitgrenze");
        Pruefe(zweiteGeht && !sim->Verstopft(), "Langsame Antwort abgewartet, Modul nicht verstopft");
    }

    // Ein frueher abgerissener Befehl: 128 Byte liegen noch im Empfang des
    // Moduls und verschieben jeden weiteren Befehl. Oeffnen muss sich davon
    // selbst erholen.
    for (int rest : {64, 448}) {
        auto l = SimLeitung(sim);
        l->Oeffnen();
        const fxpak::Paket halb = fxpak::BaueInfo();
        l->Schreiben(halb.data(), (size_t)rest);
        FxPakPort q("SIM", std::move(l));
        bool ok2 = true;
        try {
            q.Oeffnen();
            // Danach muessen mehrere Befehle hintereinander klappen.
            for (int i = 0; i < 3 && ok2; ++i)
                ok2 = q.Info().rom == "/roms/test.sfc" && q.Lesen(fxpak::kWram + 0x10, 1).at(0) == 0x42;
        } catch (...) {
            ok2 = false;
        }
        Pruefe(ok2, (std::string("Aus dem Takt (") + std::to_string(rest) + " Byte Rest): neu synchronisiert").c_str());
    }
}

struct Beobachter {
    std::mutex m;
    std::vector<std::string> log;
    std::vector<long long> freigeschaltet;
    std::vector<std::string> roms;
    int fertig = 0, geladen = 0, verloren = 0, herabgestuft = 0;
    std::vector<std::map<std::string, std::string>> anfragen;
    std::vector<Abdeckung> abdeckungen;

    template <class F> bool Warte(F bedingung, int ms) {
        for (int i = 0; i < ms / 20; ++i) {
            {
                std::lock_guard<std::mutex> l(m);
                if (bedingung()) return true;
            }
            ::Sleep(20);
        }
        std::lock_guard<std::mutex> l(m);
        return bedingung();
    }
    bool LogEnthaelt(const std::string& s) {
        for (const auto& z : log) if (z.find(s) != std::string::npos) return true;
        return false;
    }
    int Anfragen(const std::string& r) {
        int n = 0;
        for (const auto& a : anfragen) if (a.count("r") && a.at("r") == r) ++n;
        return n;
    }
};

Json Achievement(long long id, const char* mem, const char* titel) {
    Json a = Json::object();
    a.set("ID", Json::integer(id));
    a.set("Flags", Json::integer(3));
    a.set("MemAddr", Json::string(mem));
    a.set("Title", Json::string(titel));
    a.set("Description", Json::string("Test"));
    a.set("Points", Json::integer(5));
    a.set("BadgeName", Json::string(""));
    return a;
}

void TestEndeZuEnde() {
    std::printf("Ende zu Ende: Autostart, Erkennung, Freischaltung, Spielwechsel\n");
    const std::string romPfad = "/roms/Test Spiel.sfc";
    const auto rom = TestRom();
    std::vector<uint8_t> datei(512, 0xEE);  // mit Kopierer-Vorspann
    datei.insert(datei.end(), rom.begin(), rom.end());
    const std::string md5 = RaClient::snes_hash(datei);
    auto sim = NeuesSim(romPfad, datei, rom);

    auto b = std::make_shared<Beobachter>();
    RequestFn ra = [b, md5](const std::map<std::string, std::string>& p) {
        {
            std::lock_guard<std::mutex> l(b->m);
            b->anfragen.push_back(p);
        }
        Json d = Json::object();
        const std::string r = p.count("r") ? p.at("r") : "";
        if (r == "gameid") {
            d.set("Success", Json::boolean(true));
            d.set("GameID", Json::integer(p.at("m") == md5 ? 4711 : 0));
        } else if (r == "patch") {
            Json pd = Json::object();
            pd.set("ID", Json::integer(4711));
            pd.set("Title", Json::string("RAW-SNES Testspiel"));
            Json achs = Json::array();
            achs.push_back(Achievement(101, "0xH0010=5", "WRAM"));
            achs.push_back(Achievement(102, "0xH1fff0=1_0xH020000=7", "Bank 7F und SRAM"));
            achs.push_back(Achievement(103, "0xH0a0000=1", "SA-1-I-RAM"));
            pd.set("Achievements", achs);
            pd.set("Leaderboards", Json::array());
            d.set("Success", Json::boolean(true));
            d.set("PatchData", pd);
        } else {
            d.set("Success", Json::boolean(true));
        }
        return d;
    };

    const std::string cachePfad = []() {
        wchar_t t[MAX_PATH] = {};
        ::GetTempPathW(MAX_PATH, t);
        char s[MAX_PATH * 3] = {};
        ::WideCharToMultiByte(CP_UTF8, 0, t, -1, s, sizeof(s), nullptr, nullptr);
        return std::string(s) + "rawsnes_selbsttest_cache.json";
    }();
    std::remove(cachePfad.c_str());
    RaCache cache(cachePfad);
    auto client = std::make_shared<RaClient>(cache, ra);

    {
        MonitorWorker w(client, [sim]() -> std::unique_ptr<FxPakPort> {
            auto p = std::make_unique<FxPakPort>("SIM", SimLeitung(sim));
            try { p->Oeffnen(); return p; } catch (...) { return nullptr; }
        });
        w.log = [b](const std::string& s) { std::lock_guard<std::mutex> l(b->m); b->log.push_back(s); };
        w.unlocked = [b](const std::string&, int, long long id, bool) {
            std::lock_guard<std::mutex> l(b->m);
            b->freigeschaltet.push_back(id);
        };
        w.romChanged = [b](const std::string& r) { std::lock_guard<std::mutex> l(b->m); b->roms.push_back(r); };
        w.finished = [b]() { std::lock_guard<std::mutex> l(b->m); ++b->fertig; };
        w.achievementsReady = [b](const GameData&, const std::set<long long>&) {
            std::lock_guard<std::mutex> l(b->m);
            ++b->geladen;
        };
        w.connectionLost = [b](const std::string&) { std::lock_guard<std::mutex> l(b->m); ++b->verloren; };
        w.hardcoreDowngraded = [b](const std::string&) { std::lock_guard<std::mutex> l(b->m); ++b->herabgestuft; };
        w.abdeckungGeaendert = [b](const Abdeckung& a) { std::lock_guard<std::mutex> l(b->m); b->abdeckungen.push_back(a); };
        w.setAnmeldung("tester", "token");
        w.setAutostart(true);
        w.setHardcore(true);

        Pruefe(b->Warte([&]() { return b->geladen == 1; }, 15000), "Autostart: Spiel erkannt und Set geladen");
        Pruefe(b->Warte([&]() { return b->LogEnthaelt("Hash: " + md5); }, 3000), "Hash aus der SD-Datei (Kopierer-Vorspann uebersprungen)");
        Pruefe(b->Warte([&]() { return b->LogEnthaelt("Hardcore-Voraussetzung erfuellt"); }, 3000), "Hardcore-Pruefung bestanden");
        Pruefe(b->Warte([&]() { return b->LogEnthaelt("1 Achievement(s) nicht auswertbar"); }, 3000), "SA-1-I-RAM-Achievement als nicht auswertbar gemeldet");
        Pruefe(b->Warte([&]() {
                   return !b->abdeckungen.empty() && !b->abdeckungen.back().voll() &&
                          b->abdeckungen.back().nichtAuswertbar == 1 && b->abdeckungen.back().ohnePlatz == 0 &&
                          b->abdeckungen.back().aktiv == 2 && b->abdeckungen.back().offen == 3;
               }, 3000),
               "Abdeckung: 2 von 3 aktiv, 1 nicht auswertbar, kein Platzmangel");
        Pruefe(b->Warte([&]() { return b->LogEnthaelt("Nur teilweise unterstuetzt: 2 von 3"); }, 100),
               "Abdeckung: 'Nur teilweise unterstuetzt' im Protokoll");

        // Altwerte im Spiegel duerfen nichts ausloesen: erst Aenderungen zaehlen.
        sim->WramSchreiben(0x0010, 1);
        ::Sleep(150);
        sim->WramSchreiben(0x0010, 3);
        ::Sleep(150);
        Pruefe(b->Warte([&]() { return b->freigeschaltet.empty(); }, 100), "Noch keine Freischaltung bei Wert 3");
        sim->WramSchreiben(0x0010, 5);
        Pruefe(b->Warte([&]() { return std::count(b->freigeschaltet.begin(), b->freigeschaltet.end(), 101) == 1; }, 5000),
               "WRAM-Achievement freigeschaltet");

        sim->WramSchreiben(0x1FFF0, 0);
        sim->Speicher(fxpak::kSram, {0});
        ::Sleep(150);
        sim->WramSchreiben(0x1FFF0, 1);
        sim->Speicher(fxpak::kSram, {7});
        Pruefe(b->Warte([&]() { return std::count(b->freigeschaltet.begin(), b->freigeschaltet.end(), 102) == 1; }, 5000),
               "Bank-7F-/SRAM-Achievement freigeschaltet");
        {
            std::lock_guard<std::mutex> l(b->m);
            bool award101 = false;
            for (const auto& a : b->anfragen)
                if (a.count("r") && a.at("r") == "awardachievement" && a.at("a") == "101" && a.at("h") == "1")
                    award101 = true;
            Pruefe(award101, "Buchung an RA gesendet (Hardcore)");
            Pruefe(std::count(b->freigeschaltet.begin(), b->freigeschaltet.end(), 103) == 0,
                   "Nicht lesbares Achievement nie ausgeloest");
        }

        // Spielwechsel ins Menue beendet die Ueberwachung.
        sim->SetzeRom("/sd2snes/m3nu.bin");
        Pruefe(b->Warte([&]() { return b->fertig == 1; }, 5000), "Menue: Ueberwachung endet");
        Pruefe(b->Warte([&]() { return !b->roms.empty() && b->roms.back().empty(); }, 6000), "Menue gemeldet");

        // Dasselbe Spiel wieder: Autostart, Hash aus dem Zwischenspeicher.
        sim->SetzeRom(romPfad);
        Pruefe(b->Warte([&]() { return b->geladen == 2; }, 8000), "Wieder im Spiel: Autostart erneut");
        Pruefe(b->Warte([&]() { return b->LogEnthaelt("Hash bekannt"); }, 3000), "Hash aus dem Zwischenspeicher");
        Pruefe(b->Warte([&]() { return b->Anfragen("gameid") == 1; }, 100), "GameID nur einmal bei RA abgefragt");

        // Kabel ab und wieder dran.
        sim->SetzeAngesteckt(false);
        Pruefe(b->Warte([&]() { return b->verloren >= 1; }, 3000), "Kabel ab: Verbindungsverlust erkannt");
        sim->SetzeAngesteckt(true);
        Pruefe(b->Warte([&]() { return b->LogEnthaelt("Verbindung wiederhergestellt"); }, 5000),
               "Kabel dran: Verbindung wiederhergestellt");

        // Savestates mitten in der Sitzung -> Hardcore endet.
        sim->SetzeEinstellungen(0x0003);
        Pruefe(b->Warte([&]() { return b->herabgestuft == 1; }, 5000), "Savestates eingeschaltet: Hardcore beendet");

        // Stopp durch den Nutzer: kein erneuter Autostart fuer dieselbe ROM.
        w.stop();
        Pruefe(b->Warte([&]() { return b->fertig == 2; }, 3000), "Stopp beendet die Ueberwachung");
        ::Sleep(2500);
        Pruefe(b->Warte([&]() { return b->geladen == 2; }, 10), "Nach Stopp kein ungewollter Neustart");
    }
    std::remove(cachePfad.c_str());

    if (fehler) {
        std::printf("\n--- Log des Workers ---\n");
        std::lock_guard<std::mutex> l(b->m);
        for (const auto& z : b->log) std::printf("  %s\n", z.c_str());
    }
}

// Spiel mit Zusatzchip: das Modul fuehrt keine WRAM-Kopie, die Werte kommen
// ueber die eingehaengte Routine.
void TestZusatzchip() {
    std::printf("Ende zu Ende: Spiel mit Zusatzchip (SA-1), Werte-Kopie ueber den NMI-Hook\n");
    const std::string romPfad = "/roms/Chip Spiel.sfc";
    std::vector<uint8_t> rom = TestRom();
    rom[0x7FC0 + 0x15] = 0x23;   // SA-1
    rom[0x7FC0 + 0x16] = 0x35;   // ROM + SA-1 + RAM + Batterie
    const std::string md5 = RaClient::snes_hash(rom);
    auto sim = NeuesSim(romPfad, rom, rom);
    sim->SetzeSpiegel(false);
    sim->SetzeHook(true);
    sim->Konfig("EnableIngameHook", "true");

    // Die Konsole: ein NMI je Bild.
    std::atomic<bool> laeuft{true};
    std::atomic<int> nmiFehler{0};
    std::string ersterFehler;
    std::mutex fm;
    std::thread konsole([&]() {
        while (laeuft) {
            std::string f;
            if (!sim->Bild(&f)) {
                if (nmiFehler++ == 0) { std::lock_guard<std::mutex> l(fm); ersterFehler = f; }
            }
            // Wie Star Fox: mehrere IRQs je Bild durch denselben Hook.
            for (int i = 0; i < 3; ++i) {
                ::Sleep(4);
                if (!sim->Irq(&f) && nmiFehler++ == 0) { std::lock_guard<std::mutex> l(fm); ersterFehler = f; }
            }
            ::Sleep(4);
        }
    });

    auto b = std::make_shared<Beobachter>();
    RequestFn ra = [b, md5](const std::map<std::string, std::string>& p) {
        { std::lock_guard<std::mutex> l(b->m); b->anfragen.push_back(p); }
        Json d = Json::object();
        const std::string r = p.count("r") ? p.at("r") : "";
        d.set("Success", Json::boolean(true));
        if (r == "gameid") {
            d.set("GameID", Json::integer(p.at("m") == md5 ? 815 : 0));
        } else if (r == "patch") {
            Json pd = Json::object();
            pd.set("ID", Json::integer(815));
            pd.set("Title", Json::string("Chip-Testspiel"));
            Json achs = Json::array();
            achs.push_back(Achievement(201, "0xH0010=5", "WRAM"));
            achs.push_back(Achievement(202, "0xH0a0010=7", "SA-1-I-RAM"));
            achs.push_back(Achievement(203, "0xH020000=3", "BW-RAM ueber USB"));
            achs.push_back(Achievement(204, "0xH1fff0=9", "nach dem Reset"));
            achs.push_back(Achievement(205, "0xH0030=1.3.", "drei Bilder lang"));
            pd.set("Achievements", achs);
            pd.set("Leaderboards", Json::array());
            d.set("PatchData", pd);
        }
        return d;
    };
    const std::string cachePfad = []() {
        wchar_t t[MAX_PATH] = {};
        ::GetTempPathW(MAX_PATH, t);
        char s[MAX_PATH * 3] = {};
        ::WideCharToMultiByte(CP_UTF8, 0, t, -1, s, sizeof(s), nullptr, nullptr);
        return std::string(s) + "rawsnes_selbsttest_cache.json";
    }();
    std::remove(cachePfad.c_str());
    const std::string diagPfad = cachePfad.substr(0, cachePfad.find_last_of("\\/") + 1) + "rawsnes_selbsttest_diag.txt";
    std::remove(diagPfad.c_str());
    RaCache cache(cachePfad);
    auto client = std::make_shared<RaClient>(cache, ra);
    const auto zahl = [&](long long id) { return std::count(b->freigeschaltet.begin(), b->freigeschaltet.end(), id); };
    {
        MonitorWorker w(client, [sim]() -> std::unique_ptr<FxPakPort> {
            auto p = std::make_unique<FxPakPort>("SIM", SimLeitung(sim));
            try { p->Oeffnen(); return p; } catch (...) { return nullptr; }
        });
        w.log = [b](const std::string& s) { std::lock_guard<std::mutex> l(b->m); b->log.push_back(s); };
        w.unlocked = [b](const std::string&, int, long long id, bool) {
            std::lock_guard<std::mutex> l(b->m);
            b->freigeschaltet.push_back(id);
        };
        w.finished = [b]() { std::lock_guard<std::mutex> l(b->m); ++b->fertig; };
        w.achievementsReady = [b](const GameData&, const std::set<long long>&) {
            std::lock_guard<std::mutex> l(b->m);
            ++b->geladen;
        };
        w.abdeckungGeaendert = [b](const Abdeckung& a) { std::lock_guard<std::mutex> l(b->m); b->abdeckungen.push_back(a); };
        w.setAnmeldung("tester", "token");
        w.setDiagnoseDatei(diagPfad);
        w.setAutostart(true);

        Pruefe(b->Warte([&]() { return b->geladen == 1; }, 15000), "Autostart: Chip-Spiel erkannt und Set geladen");
        Pruefe(b->Warte([&]() { return b->LogEnthaelt("Zusatzchip (SA-1)"); }, 100), "Zusatzchip SA-1 am Header erkannt");
        Pruefe(b->Warte([&]() { return b->LogEnthaelt("Werte-Kopie eingehaengt"); }, 100), "Werte-Kopie eingehaengt");
        Pruefe(b->Warte([&]() { return !b->LogEnthaelt("nicht auswertbar"); }, 100), "Alle Achievements auswertbar (auch I-RAM)");
        Pruefe(b->Warte([&]() { return !b->abdeckungen.empty() && b->abdeckungen.back().voll(); }, 100) &&
                   !b->LogEnthaelt("Nur teilweise"),
               "Abdeckung: alles passt in den Hook, kein Hinweis");
        Pruefe(sim->Puffer(nmikopie::kAusgang) == nmikopie::kBefehlNeu, "Hook-Ende zeigt auf die Routine");

        sim->WramSchreiben(0x0010, 5);
        Pruefe(b->Warte([&]() { return zahl(201) == 1; }, 3000), "WRAM-Achievement ueber die Werte-Kopie freigeschaltet");
        sim->IramSchreiben(0x0010, 7);
        Pruefe(b->Warte([&]() { return zahl(202) == 1; }, 3000), "SA-1-I-RAM-Achievement freigeschaltet");
        sim->Speicher(fxpak::kSram, {3});
        Pruefe(b->Warte([&]() { return zahl(203) == 1; }, 3000), "BW-RAM-Achievement (USB) freigeschaltet");

        // Ein einzelnes Bild mit 1 darf ".3." nicht erfuellen; drei Bilder schon.
        sim->WramSchreiben(0x0030, 1);
        ::Sleep(20);
        sim->WramSchreiben(0x0030, 0);
        ::Sleep(200);
        Pruefe(b->Warte([&]() { return zahl(205) == 0; }, 50), "Trefferzaehler: kurzer Ausreisser loest nichts aus");
        sim->WramSchreiben(0x0030, 1);
        Pruefe(b->Warte([&]() { return zahl(205) == 1; }, 3000), "Trefferzaehler: nach drei Bildern freigeschaltet");

        // Reset des Spiels: die Firmware schreibt den Hook neu.
        sim->HookNeuSchreiben();
        Pruefe(b->Warte([&]() { return b->LogEnthaelt("wieder eingehaengt"); }, 4000), "Nach Reset: Werte-Kopie wieder eingehaengt");
        sim->WramSchreiben(0x1FFF0, 9);
        Pruefe(b->Warte([&]() { return zahl(204) == 1; }, 3000), "Nach Reset: Achievement freigeschaltet");

        Pruefe(b->Warte([&]() { return b->LogEnthaelt("verspaetete Bilder (Ruckeln) 0 = 0 %"); }, 12000),
               "Diagnose nach 10 s im Log, keine verspaeteten Bilder");
        {
            double bps = 0;
            std::lock_guard<std::mutex> l(b->m);
            for (const auto& z : b->log)
                if (z.rfind("Diagnose (10 s): ", 0) == 0) bps = std::atof(z.c_str() + 17);
            Pruefe(bps > 40 && bps < 80, ("Trotz 3 IRQs je Bild: " + std::to_string((int)bps) + " Bilder/s (nur NMI zaehlt)").c_str());
        }

        // Hook am Modul aus: es kommen keine Bilder mehr, das wird gemeldet.
        // Das zuletzt gelesene Vektor-Byte ist dabei oft $EE (3 IRQs je Bild);
        // das darf nicht als "nur noch IRQs" gelten.
        sim->SetzeHook(false);
        Pruefe(b->Warte([&]() { return b->LogEnthaelt("kein Bild vom NMI-Hook"); }, 6000), "Hook aus: Stillstand gemeldet");
        Pruefe(b->Warte([&]() { return !b->LogEnthaelt("neu messen"); }, 0), "Hook aus: keine neue Messung");
        sim->SetzeHook(true);
        Pruefe(b->Warte([&]() { return b->LogEnthaelt("liefert wieder Bilder"); }, 3000), "Hook wieder an: Bilder kommen");

        // Das Spiel schaltet den NMI ab, der Hook laeuft nur noch ueber IRQs.
        sim->SetzeNurIrq(true);
        Pruefe(b->Warte([&]() { return b->LogEnthaelt("nur ueber IRQs -- neu messen"); }, 5000), "Nur noch IRQs: neu gemessen");
        Pruefe(b->Warte([&]() { return b->LogEnthaelt("Kopie in der Austastluecke"); }, 3000), "Nur noch IRQs: Kopie in der Austastluecke");
        sim->SetzeNurIrq(false);

        w.stop();
        Pruefe(b->Warte([&]() { return b->fertig == 1; }, 3000), "Stopp beendet die Ueberwachung");
        Pruefe(sim->Puffer(nmikopie::kAusgang) == nmikopie::kBefehlOriginal &&
               sim->Puffer(nmikopie::kAusgang + 1) == nmikopie::kOperandOriginal, "Nach Stopp: Hook-Ende im Original");
    }
    laeuft = false;
    konsole.join();
    Pruefe(nmiFehler == 0, ("Kein Fehler in " + std::to_string(sim->Bilder()) + " simulierten NMIs " + ersterFehler).c_str());
    {
        std::string inhalt;
        if (std::FILE* f = std::fopen(diagPfad.c_str(), "rb")) {
            char puf[4096];
            size_t n;
            while ((n = std::fread(puf, 1, sizeof(puf), f)) > 0) inhalt.append(puf, n);
            std::fclose(f);
        }
        int zeilen = 0;
        for (size_t p = inhalt.find("frames/s="); p != std::string::npos; p = inhalt.find("frames/s=", p + 1)) ++zeilen;
        Pruefe(inhalt.find("=== session") != std::string::npos && inhalt.find("mode copy") != std::string::npos &&
               inhalt.find("busy=0") != std::string::npos && zeilen >= 10 && inhalt.find("=== end") != std::string::npos,
               ("Diagnosedatei: Kopf, " + std::to_string(zeilen) + " Sekundenzeilen, Ende").c_str());
    }
    std::remove(diagPfad.c_str());
    std::remove(cachePfad.c_str());

    if (fehler) {
        std::printf("\n--- Log des Workers ---\n");
        std::lock_guard<std::mutex> l(b->m);
        for (const auto& z : b->log) std::printf("  %s\n", z.c_str());
    }
}

// Spiel mit Zusatzchip und so vielen verstreuten Adressen, dass nicht alles in
// den Hook passt: das muss als "nur teilweise unterstuetzt" mit Platzmangel
// gemeldet werden, und nach jeder Freischaltung neu.
void TestZuWenigPlatz() {
    std::printf("Ende zu Ende: Spiel mit Zusatzchip, zu wenig Platz im Hook\n");
    const std::string romPfad = "/roms/Voll.sfc";
    std::vector<uint8_t> rom = TestRom();
    rom[0x7FC0 + 0x15] = 0x23;
    rom[0x7FC0 + 0x16] = 0x35;
    const std::string md5 = RaClient::snes_hash(rom);
    auto sim = NeuesSim(romPfad, rom, rom);
    sim->SetzeSpiegel(false);
    sim->SetzeHook(true);
    sim->Konfig("EnableIngameHook", "true");
    std::atomic<bool> laeuft{true};
    std::thread konsole([&]() {
        while (laeuft) {
            sim->Bild(nullptr);
            ::Sleep(16);
        }
    });
    auto b = std::make_shared<Beobachter>();
    RequestFn ra = [md5](const std::map<std::string, std::string>& p) {
        Json d = Json::object();
        const std::string r = p.count("r") ? p.at("r") : "";
        d.set("Success", Json::boolean(true));
        if (r == "gameid") {
            d.set("GameID", Json::integer(p.at("m") == md5 ? 816 : 0));
        } else if (r == "patch") {
            Json pd = Json::object();
            pd.set("ID", Json::integer(816));
            pd.set("Title", Json::string("Volles Chip-Testspiel"));
            Json achs = Json::array();
            static std::vector<std::string> mems;
            mems.clear();
            for (int i = 0; i < 80; ++i) {
                char m[32];
                std::snprintf(m, sizeof(m), "0xH%04x=1", 0x0100 + i * 0x101);
                mems.push_back(m);
            }
            for (int i = 0; i < 80; ++i) achs.push_back(Achievement(300 + i, mems[i].c_str(), "verstreut"));
            pd.set("Achievements", achs);
            pd.set("Leaderboards", Json::array());
            d.set("PatchData", pd);
        }
        return d;
    };
    const std::string cachePfad = []() {
        wchar_t t[MAX_PATH] = {};
        ::GetTempPathW(MAX_PATH, t);
        char s[MAX_PATH * 3] = {};
        ::WideCharToMultiByte(CP_UTF8, 0, t, -1, s, sizeof(s), nullptr, nullptr);
        return std::string(s) + "rawsnes_selbsttest_cache.json";
    }();
    std::remove(cachePfad.c_str());
    RaCache cache(cachePfad);
    auto client = std::make_shared<RaClient>(cache, ra);
    {
        MonitorWorker w(client, [sim]() -> std::unique_ptr<FxPakPort> {
            auto p = std::make_unique<FxPakPort>("SIM", SimLeitung(sim));
            try { p->Oeffnen(); return p; } catch (...) { return nullptr; }
        });
        w.log = [b](const std::string& s) { std::lock_guard<std::mutex> l(b->m); b->log.push_back(s); };
        w.unlocked = [b](const std::string&, int, long long id, bool) {
            std::lock_guard<std::mutex> l(b->m);
            b->freigeschaltet.push_back(id);
        };
        w.finished = [b]() { std::lock_guard<std::mutex> l(b->m); ++b->fertig; };
        w.abdeckungGeaendert = [b](const Abdeckung& a) { std::lock_guard<std::mutex> l(b->m); b->abdeckungen.push_back(a); };
        w.setAnmeldung("tester", "token");
        w.setAutostart(true);
        Pruefe(b->Warte([&]() { return !b->abdeckungen.empty(); }, 15000), "Abdeckung gemeldet");
        Abdeckung erst;
        {
            std::lock_guard<std::mutex> l(b->m);
            if (!b->abdeckungen.empty()) erst = b->abdeckungen.back();
        }
        Pruefe(!erst.voll() && erst.ohnePlatz > 0 && erst.nichtAuswertbar == 0 && erst.offen == 80 &&
                   erst.aktiv + erst.ohnePlatz == 80,
               ("Zu wenig Platz erkannt: " + std::to_string(erst.aktiv) + " von 80 aktiv, " +
                std::to_string(erst.ohnePlatz) + " ohne Platz").c_str());
        Pruefe(b->Warte([&]() { return b->LogEnthaelt("Nur teilweise unterstuetzt"); }, 100),
               "Zu wenig Platz: Hinweis im Protokoll");
        // Eine Freischaltung: die Abdeckung wird neu gemeldet.
        sim->WramSchreiben(0x0100, 1);
        Pruefe(b->Warte([&]() { return b->freigeschaltet.size() == 1 && b->abdeckungen.size() >= 2 &&
                                       b->abdeckungen.back().offen == 79; }, 5000),
               "Nach einer Freischaltung: Abdeckung neu gemeldet (79 offen)");
        w.stop();
        Pruefe(b->Warte([&]() { return b->fertig == 1; }, 3000), "Stopp beendet die Ueberwachung");
    }
    laeuft = false;
    konsole.join();
    std::remove(cachePfad.c_str());
    if (fehler) {
        std::lock_guard<std::mutex> l(b->m);
        for (const auto& z : b->log) std::printf("  %s\n", z.c_str());
    }
}

// Nur IRQs laufen durch den Hook (am Geraet bei Super FX gemessen), mehrere je
// Bild in wechselnden Zeilen. `chip`: 0x15 = Super FX (Takt ueber die
// Austastluecke von Anfang an), 0x35 = SA-1 (RAW-SNES muss selbst umschalten).
void TestNurIrq(uint8_t chip, const char* name) {
    std::printf("Ende zu Ende: %s, nur IRQs durch den Hook\n", name);
    const std::string romPfad = std::string("/roms/") + name + ".sfc";
    std::vector<uint8_t> rom = TestRom();
    rom[0x7FC0 + 0x15] = chip == 0x35 ? 0x23 : 0x20;
    rom[0x7FC0 + 0x16] = chip;
    const std::string md5 = RaClient::snes_hash(rom);
    auto sim = NeuesSim(romPfad, rom, rom);
    sim->SetzeSpiegel(false);
    sim->SetzeHook(true);
    sim->SetzeNurIrq(true);

    std::atomic<bool> laeuft{true};
    std::atomic<int> nmiFehler{0};
    std::thread konsole([&]() {
        int bild = 0;
        while (laeuft) {
            std::string f;
            sim->Bild(&f);
            for (int z : {30 + bild % 7, 120 + bild % 50, 250 + bild % 20}) {
                ::Sleep(4);
                if (!sim->Irq(&f, z)) ++nmiFehler;
            }
            ::Sleep(4);
            ++bild;
        }
    });

    auto b = std::make_shared<Beobachter>();
    RequestFn ra = [b, md5](const std::map<std::string, std::string>& p) {
        Json d = Json::object();
        const std::string r = p.count("r") ? p.at("r") : "";
        d.set("Success", Json::boolean(true));
        if (r == "gameid") {
            d.set("GameID", Json::integer(p.at("m") == md5 ? 999 : 0));
        } else if (r == "patch") {
            Json pd = Json::object();
            pd.set("ID", Json::integer(999));
            pd.set("Title", Json::string("IRQ-Testspiel"));
            Json achs = Json::array();
            achs.push_back(Achievement(301, "0xH0010=5", "WRAM"));
            achs.push_back(Achievement(302, "0xH0050=1.3.", "drei Bilder lang"));
            pd.set("Achievements", achs);
            pd.set("Leaderboards", Json::array());
            d.set("PatchData", pd);
        }
        return d;
    };
    const std::string cachePfad = []() {
        wchar_t t[MAX_PATH] = {};
        ::GetTempPathW(MAX_PATH, t);
        char s[MAX_PATH * 3] = {};
        ::WideCharToMultiByte(CP_UTF8, 0, t, -1, s, sizeof(s), nullptr, nullptr);
        return std::string(s) + "rawsnes_selbsttest_cache.json";
    }();
    std::remove(cachePfad.c_str());
    RaCache cache(cachePfad);
    auto client = std::make_shared<RaClient>(cache, ra);
    const auto zahl = [&](long long id) { return std::count(b->freigeschaltet.begin(), b->freigeschaltet.end(), id); };
    {
        MonitorWorker w(client, [sim]() -> std::unique_ptr<FxPakPort> {
            auto p = std::make_unique<FxPakPort>("SIM", SimLeitung(sim));
            try { p->Oeffnen(); return p; } catch (...) { return nullptr; }
        });
        w.log = [b](const std::string& s) { std::lock_guard<std::mutex> l(b->m); b->log.push_back(s); };
        w.unlocked = [b](const std::string&, int, long long id, bool) {
            std::lock_guard<std::mutex> l(b->m);
            b->freigeschaltet.push_back(id);
        };
        w.finished = [b]() { std::lock_guard<std::mutex> l(b->m); ++b->fertig; };
        w.achievementsReady = [b](const GameData&, const std::set<long long>&) {
            std::lock_guard<std::mutex> l(b->m);
            ++b->geladen;
        };
        w.setAnmeldung("tester", "token");
        w.setAutostart(true);

        Pruefe(b->Warte([&]() { return b->geladen == 1; }, 15000), "Spiel erkannt und Set geladen");
        Pruefe(b->Warte([&]() { return b->LogEnthaelt("Kopie in der Austastluecke."); }, 6000),
               "Sonde: nur IRQs, mehrere je Bild -> Takt ueber die Austastluecke");
        sim->WramSchreiben(0x0010, 5);
        Pruefe(b->Warte([&]() { return zahl(301) == 1; }, 4000), "WRAM-Achievement freigeschaltet");
        sim->WramSchreiben(0x0050, 1);
        Pruefe(b->Warte([&]() { return zahl(302) == 1; }, 3000), "Trefferzaehler ueber drei Bilder");
        Pruefe(b->Warte([&]() { return b->LogEnthaelt("Bilder je Sekunde ausgewertet"); }, 12000),
               "Bildtakt im Log");
        double bps = 0;
        bool diagnosezeile = false;
        {
            std::lock_guard<std::mutex> l(b->m);
            for (const auto& z : b->log) {
                if (z.rfind("Werte-Kopie: ", 0) == 0 && z.find("Bilder je Sekunde") != std::string::npos)
                    bps = std::atof(z.c_str() + 13);
                if (z.rfind("Diagnose (10 s)", 0) == 0) diagnosezeile = true;
            }
        }
        Pruefe(!diagnosezeile, "Ohne uebersprungene Bilder keine Diagnosezeile im Protokoll");
        Pruefe(bps > 40 && bps < 80, ("Bilder/s trotz drei IRQs je Bild: " + std::to_string((int)bps)).c_str());
        w.stop();
        Pruefe(b->Warte([&]() { return b->fertig == 1; }, 3000), "Stopp beendet die Ueberwachung");
    }
    laeuft = false;
    konsole.join();
    Pruefe(nmiFehler == 0, "Kein Fehler in den simulierten IRQs");
    std::remove(cachePfad.c_str());
    if (fehler) {
        std::lock_guard<std::mutex> l(b->m);
        for (const auto& z : b->log) std::printf("  %s\n", z.c_str());
    }
}

// S-DD1: Die Sonde waehlt den Takt. Wie Star Ocean (nur IRQ, einer je Bild):
// Kopie nach dem IRQ des Spiels (Takt::Nachlauf). Wie Street Fighter Alpha 2
// (NMI durch den Hook): Kopie im NMI.
void TestSdd1(bool nurIrq) {
    std::printf(nurIrq ? "Ende zu Ende: S-DD1 nur IRQ, Kopie nach dem IRQ des Spiels\n"
                       : "Ende zu Ende: S-DD1 mit NMI, Kopie im NMI\n");
    std::vector<uint8_t> rom = TestRom();
    rom[0x7FC0 + 0x15] = 0x32;
    rom[0x7FC0 + 0x16] = 0x45;
    const std::string md5 = RaClient::snes_hash(rom);
    auto sim = NeuesSim("/roms/S-DD1.sfc", rom, rom);
    sim->SetzeSpiegel(false);
    sim->SetzeHook(true);
    sim->SetzeNurIrq(nurIrq);

    std::atomic<bool> laeuft{true};
    std::atomic<int> irqFehler{0};
    std::thread konsole([&]() {
        // Mit NMI wie Street Fighter Alpha 2: Der Hook laeuft erst nach dem
        // Laden, die Sonde darf vorher nicht entscheiden.
        if (!nurIrq)
            for (int i = 0; i < 150 && laeuft; ++i) ::Sleep(10);
        while (laeuft) {
            std::string f;
            if (!sim->Bild(&f)) ++irqFehler;   // bei nurIrq laeuft der NMI am Hook vorbei
            ::Sleep(4);
            if (nurIrq && !sim->Irq(&f, 120)) ++irqFehler;
            ::Sleep(12);
        }
    });

    auto b = std::make_shared<Beobachter>();
    RequestFn ra = [md5](const std::map<std::string, std::string>& p) {
        Json d = Json::object();
        const std::string r = p.count("r") ? p.at("r") : "";
        d.set("Success", Json::boolean(true));
        if (r == "gameid") {
            d.set("GameID", Json::integer(p.at("m") == md5 ? 998 : 0));
        } else if (r == "patch") {
            Json pd = Json::object();
            pd.set("ID", Json::integer(998));
            pd.set("Title", Json::string("S-DD1-Testspiel"));
            Json achs = Json::array();
            achs.push_back(Achievement(401, "0xH0010=5", "WRAM"));
            pd.set("Achievements", achs);
            pd.set("Leaderboards", Json::array());
            d.set("PatchData", pd);
        }
        return d;
    };
    const std::string cachePfad = []() {
        wchar_t t[MAX_PATH] = {};
        ::GetTempPathW(MAX_PATH, t);
        char s[MAX_PATH * 3] = {};
        ::WideCharToMultiByte(CP_UTF8, 0, t, -1, s, sizeof(s), nullptr, nullptr);
        return std::string(s) + "rawsnes_selbsttest_cache.json";
    }();
    std::remove(cachePfad.c_str());
    RaCache cache(cachePfad);
    auto client = std::make_shared<RaClient>(cache, ra);
    {
        MonitorWorker w(client, [sim]() -> std::unique_ptr<FxPakPort> {
            auto p = std::make_unique<FxPakPort>("SIM", SimLeitung(sim));
            try { p->Oeffnen(); return p; } catch (...) { return nullptr; }
        });
        w.log = [b](const std::string& s) { std::lock_guard<std::mutex> l(b->m); b->log.push_back(s); };
        w.unlocked = [b](const std::string&, int, long long id, bool) {
            std::lock_guard<std::mutex> l(b->m);
            b->freigeschaltet.push_back(id);
        };
        w.finished = [b]() { std::lock_guard<std::mutex> l(b->m); ++b->fertig; };
        w.setAnmeldung("tester", "token");
        w.setAutostart(true);
        if (nurIrq) {
            Pruefe(b->Warte([&]() { return b->LogEnthaelt("Kopie nach dem IRQ des Spiels."); }, 15000),
                   "S-DD1 nur IRQ: Sonde waehlt Kopie nach dem IRQ des Spiels");
            Pruefe(b->Warte([&]() { return sim->Nachlaeufe() > 20; }, 6000), "S-DD1 nur IRQ: Nachlauf nach jedem IRQ");
        } else {
            const int vorher = sim->Nachlaeufe();
            Pruefe(b->Warte([&]() { return b->LogEnthaelt("Kopie im NMI."); }, 15000), "S-DD1 mit NMI: Sonde waehlt Kopie im NMI");
            Pruefe(b->Warte([&]() { return b->LogEnthaelt("Werte-Kopie eingehaengt"); }, 6000) && sim->Nachlaeufe() == vorher,
                   "S-DD1 mit NMI: kein Nachlauf");
        }
        sim->WramSchreiben(0x0010, 5);
        // Warte haelt den Mutex schon.
        Pruefe(b->Warte([&]() { return std::count(b->freigeschaltet.begin(), b->freigeschaltet.end(), 401LL) == 1; }, 4000),
               "S-DD1: WRAM-Achievement freigeschaltet");
        if (nurIrq) {
            // Laeuft spaeter doch der NMI durch den Hook: sofort auf die Kopie im NMI.
            sim->SetzeNurIrq(false);
            Pruefe(b->Warte([&]() { return b->LogEnthaelt("NMI im Hook gesehen"); }, 4000) &&
                       b->Warte([&]() { return b->LogEnthaelt("Werte-Kopie eingehaengt") && sim->Puffer(nmikopie::kAusgang) == nmikopie::kBefehlNeu; }, 4000),
                   "S-DD1: NMI taucht auf -> auf die Kopie im NMI umgestellt");
        }
        w.stop();
        Pruefe(b->Warte([&]() { return b->fertig == 1; }, 3000), "S-DD1: Stopp beendet die Ueberwachung");
    }
    laeuft = false;
    konsole.join();
    Pruefe(irqFehler == 0, "S-DD1: kein Fehler in den simulierten IRQs und Nachlaeufen");
    std::remove(cachePfad.c_str());
    if (fehler) {
        std::lock_guard<std::mutex> l(b->m);
        for (const auto& z : b->log) std::printf("  %s\n", z.c_str());
    }
}

}  // namespace

int main() {
    TestPakete();
    TestKopf();
    TestDateiUndPfade();
    TestSpeicher();
    TestHash();
    TestBedingungen();
    TestNmiKopie();
    TestPortGegenSim();
    TestEndeZuEnde();
    TestZusatzchip();
    TestZuWenigPlatz();
    TestNurIrq(0x15, "Super FX");
    TestNurIrq(0x35, "SA-1 ohne NMI");
    TestSdd1(true);
    TestSdd1(false);
    std::printf(fehler ? "\n%d Fehler\n" : "\nAlles in Ordnung\n", fehler);
    return fehler ? 1 : 0;
}
