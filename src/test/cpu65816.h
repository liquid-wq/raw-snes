// Kleiner 65816-Interpreter fuer den Selbsttest.
//
// Kann genau die Befehle, die der NMI-Hook am Ende und die Kopierroutine
// (core/nmi_kopie) benutzen, im nativen Modus mit 8/16-Bit-Registern. Ein
// unbekannter Befehl beendet den Lauf mit Fehler -- so faellt jede Abweichung
// des Erzeugers sofort auf.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

class Cpu65816 {
public:
    std::function<uint8_t(uint32_t)> lies;            // 24-Bit-Adresse
    std::function<void(uint32_t, uint8_t)> schreibe;

    uint16_t a = 0, x = 0, y = 0, s = 0x1FF0, d = 0;
    uint8_t dbr = 0, pbr = 0, p = 0x34;   // p: N V M X D I Z C
    uint16_t pc = 0;

    // Laeuft ab pbr:pc, bis `jmp (ende)` erreicht ist. false bei unbekanntem
    // Befehl oder zu vielen Schritten; `fehler` sagt dann, warum.
    bool LaufeBis(uint16_t ende, int maxSchritte, std::string* fehler);
    int Schritte() const { return schritte_; }
    // Waren Direktseite oder Datenbank zwischendurch verstellt? Ein NMI, der
    // die Routine unterbricht, saehe dann fremde Werte.
    bool seitenGeaendert = false;
    // Haelt zusaetzlich an, sobald der naechste Befehl von dieser 24-Bit-Adresse kaeme.
    int32_t stopBei = -1;

private:
    bool M() const { return p & 0x20; }
    bool X() const { return p & 0x10; }
    uint8_t Hole() { return lies(((uint32_t)pbr << 16) | pc++); }
    uint16_t Hole16() { const uint8_t l = Hole(); return (uint16_t)(l | (Hole() << 8)); }
    void Push(uint8_t v) { schreibe(s, v); --s; }
    uint8_t Pull() { ++s; return lies(s); }
    void NZ(uint16_t v, bool acht);
    int schritte_ = 0;
};
