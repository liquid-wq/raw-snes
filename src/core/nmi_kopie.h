// Werte-Kopie ueber den NMI-Hook der FXPak-Firmware.
//
// Bei Spielen mit Zusatzchip (SA-1, Super FX, Cx4, S-DD1, OBC1, SPC7110)
// laedt das FXPak einen eigenen FPGA-Kern, der den WRAM der Konsole nicht
// mitschreibt; die WRAM-Kopie ab 0xF50000 bleibt dann stehen. Stattdessen
// kopiert die Konsole die benoetigten Werte selbst: Eine kleine 65816-Routine
// wird an das Ende des NMI-Hooks der Firmware gehaengt, liest die Adressen
// einmal je Bild und legt sie im Befehlspuffer des Moduls ab. Diesen Puffer
// ($2A00-$2BFF) erreicht der PC ueber USB (Raum Befehlspuffer).
//
// Belegung des Puffers durch die Firmware (am Geraet mit Firmware 1.11.0
// nachgelesen):
//   $2A00-$2A0F  Befehlsbytes
//   $2A10-$2AD4  NMI-Hook (Ende bei $2A63) und Reset-Hook, danach Endmarke
//   $2AD8-       WRAM-Cheats, je 6 Byte, mit rts abgeschlossen
//   $2BA0-$2BB3  Tastenkombinationen, $2BF0-$2BFF Variablen
// Benutzt werden $2ADD-$2ADF im Cheat-Bereich (Sprung; deshalb nur ohne
// WRAM-Cheats) und $2B00-$2B9F (Routine, Zeigertabelle, Daten).
//
// Einhaengen: `rep #$20` am Hook-Ende ($2A63) wird zu `bra $2ADD`. Das
// geschieht in zwei Schritten zu je einem Byte -- erst der Operand ($2A64,
// $20 -> $78; `rep #$78` setzt ebenfalls 16-Bit-A und laesst die
// Interrupt-Sperre stehen, die Flags stellt das folgende `plp` wieder her),
// dann der Befehl ($2A63, $C2 -> $80). Jeder Zwischenstand ist damit
// ausfuehrbar. Die Routine endet mit `rep #$20` und springt nach $2A65
// zurueck; der Ruecksprung ins Spiel bleibt der der Firmware.
//
// Der Hook laeuft bei manchen Spielen (z. B. Super FX) auch beim IRQ, mehrmals
// je Bild. Die Routine kopiert nur, wenn das FPGA beim Lesen von $00:2A6C den
// NMI-Vektor ($EA) meldet; beim IRQ ($EE) springt sie sofort zurueck.
//
// Jedes Bild ist an zwei Zaehlern erkennbar: die Routine setzt am Anfang
// SCHWANZ auf einen ungueltigen Wert und KOPF auf den neuen Stand, kopiert,
// und setzt am Ende SCHWANZ = KOPF. Nur wenn beide gleich sind, ist das Bild
// vollstaendig.
//
// Ohne Betriebssystem-Bezug.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nmikopie {

constexpr uint32_t kPuffer = 0x2A00;
constexpr uint32_t kPufferGroesse = 0x200;
constexpr uint32_t kHookAnfang = 0x2A10;
constexpr uint32_t kAusgang = 0x2A63;      // nmi_exit
constexpr uint32_t kRueckkehr = 0x2A65;    // nmi_exit + 2 (pla)
constexpr uint32_t kVektorByte = 0x2A6C;   // liefert beim Lesen $EA (NMI) bzw. $EE (IRQ)
constexpr uint32_t kCheats = 0x2AD8;
constexpr uint32_t kSprung = 0x2ADD;       // im Cheat-Bereich, $2A65 + $78
constexpr uint32_t kBlock = 0x2B00;
constexpr uint32_t kBlockEnde = 0x2BA0;
constexpr uint32_t kCheatZahl = 0x2BFF;

// Ein Lesevorgang deckt den ganzen Puffer ab (beim USB-Lesen gleich teuer
// wie ein Teil: ein Block zu 512 Byte).
constexpr uint32_t kLeseStart = kPuffer;
constexpr uint32_t kLeseLaenge = kPufferGroesse;

// Hook-Ende der Firmware: rep #$20 / pla / plp / sta $002BFD / jmp ($FF77)
constexpr uint8_t kAusgangOriginal[11] = {0xC2, 0x20, 0x68, 0x28, 0x8F, 0xFD, 0x2B, 0x00, 0x6C, 0x77, 0xFF};
constexpr uint8_t kBefehlOriginal = 0xC2, kOperandOriginal = 0x20;
constexpr uint8_t kBefehlNeu = 0x80, kOperandNeu = 0x78;   // bra $2ADD bzw. rep #$78
static_assert(kRueckkehr + kOperandNeu == kSprung, "Sprungweite passt nicht zum Sprungziel");

struct ByteSchreiben {
    uint32_t adresse;
    uint8_t wert;
};
// Reihenfolge einhalten.
std::vector<ByteSchreiben> Einhaengen();
std::vector<ByteSchreiben> Aushaengen();

// Prueft den Puffer (kPufferGroesse Byte ab kPuffer): Hook-Ende im Original
// oder schon eingehaengt, und keine WRAM-Cheats (die belegen den Sprung).
enum class Zustand { Original, Eingehaengt, Unbekannt, ZuVieleCheats };
Zustand Pruefe(const std::vector<uint8_t>& puffer);

// Kann die Konsole diese RA-Adresse selbst lesen? WRAM und SA-1-I-RAM.
bool KonsoleLiest(uint32_t ra);

// Woran die Routine ein neues Bild erkennt.
//  Nmi:        nur beim NMI kopieren (Vektor-Byte $EA). Ueblicher Fall.
//  Austastung: beim NMI, sonst beim ersten IRQ in der Austastluecke
//              ($4212 Bit 7) nach einem Durchlauf ausserhalb. Fuer Spiele,
//              bei denen nur IRQs durch den Hook laufen, mehrere je Bild
//              (Super FX). $4212 wird nur gelesen, ohne Nebenwirkung.
//  JederLauf:  bei jedem Durchlauf. Fuer Spiele mit genau einem IRQ je Bild
//              und ohne NMI durch den Hook.
//  Nachlauf:   wie JederLauf, aber die Routine laeuft erst NACH dem IRQ des
//              Spiels: Im Hook liegt nur ein zweiter Ruecksprungrahmen auf dem
//              Stapel, das rti des Spiels fuehrt in die Routine, deren rti in
//              den Originalrahmen. Fuer Spiele, deren IRQ keine Verzoegerung
//              vertraegt (am Geraet: Star Ocean, S-DD1 -- schon 16 Takte mehr
//              vor dem IRQ des Spiels bringen es zum Absturz). Nur fuer Spiele,
//              bei denen allein IRQs durch den Hook laufen.
enum class Takt { Nmi, Austastung, JederLauf, Nachlauf };

struct Programm {
    std::vector<uint8_t> sprung;   // ab kSprung
    std::vector<uint8_t> block;    // ab kBlock
    uint32_t kopf = 0;             // Adressen im Puffer
    uint32_t schwanz = 0;
    uint32_t stapel = 0;           // Stapelzeiger der Konsole in diesem Bild (16 Bit)
    uint32_t vektor = 0;           // zuletzt beim Lesen von $2A6C gelieferter Wert
    uint32_t laeufe = 0;           // Durchlaeufe der Routine (NMI und IRQ), 8 Bit
    uint32_t merker = 0;           // nur Takt::Austastung: Durchlauf ausserhalb seit der letzten Kopie
    Takt takt = Takt::Nmi;
    std::map<uint32_t, uint32_t> lage;   // RA-Adresse -> Adresse im Puffer
    std::vector<uint32_t> ohnePlatz;     // passten nicht mehr hinein
    int eintraege = 0;                   // gelesene 16-Bit-Woerter
    int takte = 0;                       // grobe Schaetzung je Bild
};

// Erzeugt die Routine fuer die RA-Adressen in der gegebenen Reihenfolge
// (wichtige zuerst). Adressen, die die Konsole nicht selbst liest, werden
// uebergangen.
Programm Erzeuge(const std::vector<uint32_t>& raAdressen, Takt takt = Takt::Nmi);

// Byte, die die Routine fuer genau diese Adressen braucht (Code, Zeiger,
// Daten, Zaehler), ohne Platzgrenze. Platz im Puffer: kBlockEnde - kBlock.
int Platzbedarf(const std::vector<uint32_t>& raAdressen, Takt takt);

// Adressen, die nur zusammen etwas nuetzen (z. B. alle eines Achievements).
struct Gruppe {
    int stufe = 0;                   // niedrige Stufen zuerst
    std::vector<uint32_t> adressen;
};
// Nimmt nur ganze Gruppen auf: je Stufe immer die, die am wenigsten Platz
// dazu braucht, bis keine mehr passt. `drin` sagt je Gruppe, ob sie dabei ist.
Programm ErzeugeGruppen(const std::vector<Gruppe>& gruppen, std::vector<bool>* drin, Takt takt = Takt::Nmi);

// Sonde: zaehlt nur, wie oft der Hook laeuft und wie oft davon ueber den NMI
// (Vektor-Byte $EA). Etwa 30 Takte im Hook -- kurz genug auch fuer Spiele, die
// kaum Verzoegerung vertragen (Star Ocean). Damit wird vor dem Einrichten der
// Takt gewaehlt: NMI -> Takt::Nmi, nur IRQ einmal je Bild -> Takt::Nachlauf.
constexpr uint32_t kSondeAlle = 0x2B60, kSondeNmi = 0x2B61;
std::vector<uint8_t> Sonde();   // ab kBlock

// Messroutine (nur Diagnose): haelt fuer die letzten 16 Durchlaeufe des
// Hooks Vektor-Byte, Bildschirmzeile und PPU-Status ($213F) fest. Liest
// dafuer $2137/$213F/$213D (Zaehler einrasten) -- deshalb nicht im Betrieb.
constexpr uint32_t kMessIndex = 0x2B5F;
constexpr uint32_t kMessRing = 0x2B60;      // 16 x {Vektor, Zeile lo, Zeile hi, Status}
constexpr int kMessEintraege = 16;
std::vector<uint8_t> Messroutine();          // ab kBlock

// Kleinste Routinen (nur Diagnose, Absturzsuche): zaehlen ihre
// Durchlaeufe bei kKleinZaehler und fuegen je eine Eigenschaft der
// Kopierroutine hinzu.
//  0: nur zaehlen (ca. 25 Takte, kein Stapel, nur A und P veraendert)
//  1: dazu 80 Takte Warteschleife
//  2: dazu 9 Byte auf dem Stapel
//  3: dazu 80 Takte Warteschleife mit Direktseite $2B00
//  4: dazu einmal $00:2A6C lesen (Vektor-Byte)
//  5: dazu X/Y auf 16 Bit und beide auf dem Stapel sichern
//  6: Anfang und Ende wie die Kopierroutine (DBR, D, X, Y sichern, D = $2B00)
//  7: wie 6, dazu Zugriffe ueber die Direktseite wie die Kopierroutine
//  8: wie 6, dazu $2A6C lesen und nach $2B3D schreiben (lang adressiert)
// 10-12: wie 6, dazu je einer der Direktseiten-Zugriffe der Kopierroutine:
//     10 sta <$3D, 11 inc <$3E, 12 lda <$3A / sta <$3F
// 13-14: wie 6, dazu $EE nach $2B30 bzw. $2B7D schreiben (lang adressiert)
// 15: wie 0, dazu $EE nach $2B3D schreiben (Direktseite unveraendert)
// 16: wie 0, misst ueber $4212, ob die Austastluecke waehrend der Routine
//     beginnt (kKleinUeberlapp) oder schon beim Start laeuft (kKleinInAustastung)
// 17: wie 0, legt die S-DD1-Bankregister $4804-$4807 bei kKleinBankregister ab
// 18: wie 0, dazu $EE nach `ziel` (Bank $00)
// 22-24: wie 6, dazu je zwei der drei Zugriffe aus 7: 22 sta <$3D + inc <$3E,
//     23 inc <$3E + lda <$3A / sta <$3F, 24 sta <$3D + lda <$3A / sta <$3F
// 25: wie 6, dazu `ziel` mal nop (reine Wartezeit, kein Speicherzugriff)
// 26: Aufschub -- lenkt nur den Ruecksprung des Spiels um und wartet erst
//     NACH dessen IRQ (`ziel` mal nop); zaehlt bei kAufschubZaehler
// 27: wie 26, aber Warteschleife mit `ziel` Runden zu 5 Takten (1-255)
// 28: Zaehler + Warteschleife IM Hook mit `ziel` Runden zu 5 Takten (0-255)
// A und P stellt der Hook der Firmware selbst wieder her (pla/plp).
constexpr uint32_t kKleinZaehler = 0x2B60;
constexpr uint32_t kKleinUeberlapp = 0x2B70, kKleinInAustastung = 0x2B71;
constexpr uint32_t kKleinBankregister = 0x2B64;   // 4 Byte: $4804-$4807 (Kleinroutine 17)
// Kleinroutine 26 (Aufschub): Ruecksprung PCL/PCH/PB, P, A, X, Y, Zaehler.
constexpr uint32_t kAufschubRueck = 0x2B90, kAufschubP = 0x2B93, kAufschubA = 0x2B94, kAufschubX = 0x2B96,
                   kAufschubY = 0x2B98, kAufschubZaehler = 0x2B9A;
constexpr uint32_t kNmiVektorAus = 0x2BFD;          // Schreiben beendet die Einblendung des Puffers
std::vector<uint8_t> Kleinroutine(int art, uint32_t ziel = 0);   // ab kBlock

struct Bild {
    bool vollstaendig = false;     // KOPF == SCHWANZ
    bool eingehaengt = false;      // Hook-Ende zeigt auf die Routine
    uint8_t zaehler = 0;
    // Stapelzeiger beim Lauf der Routine. Wartet das Spiel ruhig auf das
    // naechste Bild, steht er immer gleich; ein anderer Wert heisst, das Spiel
    // war beim Bildwechsel noch beschaeftigt (verspaetetes Bild, Ruckeln).
    uint16_t stapel = 0;
    uint8_t vektor = 0;            // $EA beim NMI, $EE beim IRQ (zuletzt gelesen)
    uint8_t laeufe = 0;
    std::map<uint32_t, uint8_t> werte;
};
// `gelesen`: kLeseLaenge Byte ab kLeseStart.
Bild Deute(const Programm& p, const std::vector<uint8_t>& gelesen);

}  // namespace nmikopie
