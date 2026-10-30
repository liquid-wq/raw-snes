#include "cpu65816.h"

#include <cstdio>

void Cpu65816::NZ(uint16_t v, bool acht) {
    const bool null = acht ? (v & 0xFF) == 0 : v == 0;
    const bool neg = acht ? (v & 0x80) != 0 : (v & 0x8000) != 0;
    p = (uint8_t)((p & ~0x82) | (null ? 0x02 : 0) | (neg ? 0x80 : 0));
}

bool Cpu65816::LaufeBis(uint16_t ende, int maxSchritte, std::string* fehler) {
    auto fail = [&](const char* was, uint8_t op) {
        char t[96];
        std::snprintf(t, sizeof(t), "%s bei %02X:%04X (Befehl %02X)", was, pbr, (unsigned)(pc - 1), op);
        if (fehler) *fehler = t;
        return false;
    };
    const uint16_t d0 = d;
    const uint8_t dbr0 = dbr;
    seitenGeaendert = false;
    for (schritte_ = 0; schritte_ < maxSchritte; ++schritte_) {
        if (stopBei >= 0 && (((uint32_t)pbr << 16) | pc) == (uint32_t)stopBei) return true;
        const uint8_t op = Hole();
        switch (op) {
            case 0x78: p |= 0x04; break;                                 // SEI
            case 0x40:                                                   // RTI (nativ)
                p = Pull();
                if (X()) { x &= 0xFF; y &= 0xFF; }
                { const uint8_t l = Pull(); pc = (uint16_t)(l | (Pull() << 8)); }
                pbr = Pull();
                break;
            case 0xA3: {                                                 // LDA sr,S
                const uint16_t ad = (uint16_t)(s + Hole());
                if (M()) { a = (uint16_t)((a & 0xFF00) | lies(ad)); NZ(a, true); }
                else { a = (uint16_t)(lies(ad) | (lies((uint16_t)(ad + 1)) << 8)); NZ(a, false); }
                break;
            }
            case 0x83: {                                                 // STA sr,S
                const uint16_t ad = (uint16_t)(s + Hole());
                schreibe(ad, (uint8_t)a);
                if (!M()) schreibe((uint16_t)(ad + 1), (uint8_t)(a >> 8));
                break;
            }
            case 0x98:                                                   // TYA
                if (M()) { a = (uint16_t)((a & 0xFF00) | (y & 0xFF)); NZ(a, true); }
                else { a = y; NZ(a, false); }
                break;
            case 0xA8:                                                   // TAY
                y = X() ? (uint16_t)(a & 0xFF) : a;
                NZ(y, X());
                break;
            case 0x08: Push(p); break;                                   // PHP
            case 0x28: p = Pull(); if (X()) { x &= 0xFF; y &= 0xFF; } break;  // PLP
            case 0x48:                                                   // PHA
                if (!M()) Push((uint8_t)(a >> 8));
                Push((uint8_t)a);
                break;
            case 0x68: {                                                 // PLA
                const uint8_t l = Pull();
                if (M()) { a = (uint16_t)((a & 0xFF00) | l); NZ(a, true); }
                else { a = (uint16_t)(l | (Pull() << 8)); NZ(a, false); }
                break;
            }
            case 0x8B: Push(dbr); break;                                 // PHB
            case 0xAB: dbr = Pull(); NZ(dbr, true); break;              // PLB
            case 0x0B: Push((uint8_t)(d >> 8)); Push((uint8_t)d); break;  // PHD
            case 0x2B: { const uint8_t l = Pull(); d = (uint16_t)(l | (Pull() << 8)); NZ(d, false); break; }  // PLD
            case 0xDA:                                                   // PHX
                if (!X()) Push((uint8_t)(x >> 8));
                Push((uint8_t)x);
                break;
            case 0xFA: {                                                 // PLX
                const uint8_t l = Pull();
                if (X()) { x = l; NZ(x, true); }
                else { x = (uint16_t)(l | (Pull() << 8)); NZ(x, false); }
                break;
            }
            case 0x3B: a = s; NZ(a, false); break;                       // TSC
            case 0x7B: a = d; NZ(a, false); break;                       // TDC
            case 0x5B: d = a; NZ(d, false); break;                       // TCD
            case 0x3A:                                                   // DEC A
                if (M()) { a = (uint16_t)((a & 0xFF00) | ((a - 1) & 0xFF)); NZ(a, true); }
                else { --a; NZ(a, false); }
                break;
            case 0xA6: {                                                 // LDX dp
                const uint32_t ad = (uint16_t)(d + Hole());
                if (X()) { x = lies(ad); NZ(x, true); }
                else { x = (uint16_t)(lies(ad) | (lies((uint16_t)(ad + 1)) << 8)); NZ(x, false); }
                break;
            }
            case 0x95: {                                                 // STA dp,X
                const uint32_t ad = (uint16_t)(d + Hole() + x);
                schreibe(ad, (uint8_t)a);
                if (!M()) schreibe((uint16_t)(ad + 1), (uint8_t)(a >> 8));
                break;
            }
            case 0x29:                                                   // AND #
                if (M()) { a = (uint16_t)(a & (0xFF00 | Hole())); NZ(a, true); }
                else { a = (uint16_t)(a & Hole16()); NZ(a, false); }
                break;
            case 0x8A:                                                   // TXA
                if (M()) { a = (uint16_t)((a & 0xFF00) | (x & 0xFF)); NZ(a, true); }
                else { a = x; NZ(a, false); }
                break;
            case 0x18: p &= (uint8_t)~0x01; break;                       // CLC
            case 0x30: {                                                 // BMI
                const int8_t rel = (int8_t)Hole();
                if (p & 0x80) pc = (uint16_t)(pc + rel);
                break;
            }
            case 0x10: {                                                 // BPL
                const int8_t rel = (int8_t)Hole();
                if (!(p & 0x80)) pc = (uint16_t)(pc + rel);
                break;
            }
            case 0x64: {                                                 // STZ dp
                const uint32_t ad = (uint16_t)(d + Hole());
                schreibe(ad, 0);
                if (!M()) schreibe((uint16_t)(ad + 1), 0);
                break;
            }
            case 0xEB: a = (uint16_t)((a >> 8) | (a << 8)); NZ(a, true); break;  // XBA
            case 0x90: {                                                 // BCC
                const int8_t rel = (int8_t)Hole();
                if (!(p & 0x01)) pc = (uint16_t)(pc + rel);
                break;
            }
            case 0xC5: {                                                 // CMP dp
                const uint32_t ad = (uint16_t)(d + Hole());
                const uint16_t w = M() ? lies(ad) : (uint16_t)(lies(ad) | (lies((uint16_t)(ad + 1)) << 8));
                const uint16_t v = M() ? (uint16_t)(a & 0xFF) : a;
                NZ((uint16_t)(v - w), M());
                p = (uint8_t)((p & ~0x01) | (v >= w ? 0x01 : 0));
                break;
            }
            case 0x69: {                                                 // ADC # (binaer)
                const uint16_t w = M() ? Hole() : Hole16();
                const uint32_t v = (M() ? (a & 0xFFu) : a) + (uint32_t)w + (p & 1);
                if (M()) { a = (uint16_t)((a & 0xFF00) | (v & 0xFF)); NZ(a, true); p = (uint8_t)((p & ~1) | (v > 0xFF)); }
                else { a = (uint16_t)v; NZ(a, false); p = (uint8_t)((p & ~1) | (v > 0xFFFF)); }
                break;
            }
            case 0xE6: {                                                 // INC dp
                const uint32_t ad = (uint16_t)(d + Hole());
                if (M()) { const uint8_t v = (uint8_t)(lies(ad) + 1); schreibe(ad, v); NZ(v, true); }
                else {
                    const uint16_t v = (uint16_t)((lies(ad) | (lies((uint16_t)(ad + 1)) << 8)) + 1);
                    schreibe(ad, (uint8_t)v);
                    schreibe((uint16_t)(ad + 1), (uint8_t)(v >> 8));
                    NZ(v, false);
                }
                break;
            }
            case 0xAF: {                                                 // LDA long
                const uint16_t l = Hole16();
                const uint32_t ad = ((uint32_t)Hole() << 16) | l;
                if (M()) { a = (uint16_t)((a & 0xFF00) | lies(ad)); NZ(a, true); }
                else { a = (uint16_t)(lies(ad) | (lies((ad + 1) & 0xFFFFFF) << 8)); NZ(a, false); }
                break;
            }
            case 0xC9: {                                                 // CMP #
                const uint16_t w = M() ? Hole() : Hole16();
                const uint16_t v = M() ? (uint16_t)(a & 0xFF) : a;
                NZ((uint16_t)(v - w), M());
                p = (uint8_t)((p & ~0x01) | (v >= w ? 0x01 : 0));
                break;
            }
            case 0xF0: {                                                 // BEQ
                const int8_t rel = (int8_t)Hole();
                if (p & 0x02) pc = (uint16_t)(pc + rel);
                break;
            }
            case 0x5A:                                                   // PHY
                if (!X()) Push((uint8_t)(y >> 8));
                Push((uint8_t)y);
                break;
            case 0x7A: {                                                 // PLY
                const uint8_t l = Pull();
                if (X()) { y = l; NZ(y, true); }
                else { y = (uint16_t)(l | (Pull() << 8)); NZ(y, false); }
                break;
            }
            case 0xA0:                                                   // LDY #
                if (X()) { y = Hole(); NZ(y, true); }
                else { y = Hole16(); NZ(y, false); }
                break;
            case 0x54: {                                                 // MVN ziel,quelle
                const uint8_t zielBank = Hole(), quellBank = Hole();
                dbr = zielBank;
                // Kopiert A+1 Byte (A immer 16 Bit), X und Y laufen mit.
                do {
                    schreibe(((uint32_t)zielBank << 16) | y, lies(((uint32_t)quellBank << 16) | x));
                    x = X() ? (uint16_t)((x + 1) & 0xFF) : (uint16_t)(x + 1);
                    y = X() ? (uint16_t)((y + 1) & 0xFF) : (uint16_t)(y + 1);
                    --a;
                } while (a != 0xFFFF);
                break;
            }
            case 0xF4: { const uint16_t w = Hole16(); Push((uint8_t)(w >> 8)); Push((uint8_t)w); break; }  // PEA
            case 0xC2: p = (uint8_t)(p & ~Hole()); break;                // REP
            case 0xE2:                                                   // SEP
                p = (uint8_t)(p | Hole());
                if (X()) { x &= 0xFF; y &= 0xFF; }
                break;
            case 0xA9:                                                   // LDA #
                if (M()) { a = (uint16_t)((a & 0xFF00) | Hole()); NZ(a, true); }
                else { a = Hole16(); NZ(a, false); }
                break;
            case 0xA2:                                                   // LDX #
                if (X()) { x = Hole(); NZ(x, true); }
                else { x = Hole16(); NZ(x, false); }
                break;
            case 0xA5: {                                                 // LDA dp
                const uint32_t ad = (uint16_t)(d + Hole());
                if (M()) { a = (uint16_t)((a & 0xFF00) | lies(ad)); NZ(a, true); }
                else { a = (uint16_t)(lies(ad) | (lies((uint16_t)(ad + 1)) << 8)); NZ(a, false); }
                break;
            }
            case 0x85: {                                                 // STA dp
                const uint32_t ad = (uint16_t)(d + Hole());
                schreibe(ad, (uint8_t)a);
                if (!M()) schreibe((uint16_t)(ad + 1), (uint8_t)(a >> 8));
                break;
            }
            case 0x1A:                                                   // INC A
                if (M()) { a = (uint16_t)((a & 0xFF00) | ((a + 1) & 0xFF)); NZ(a, true); }
                else { ++a; NZ(a, false); }
                break;
            case 0x49:                                                   // EOR #
                if (M()) { a = (uint16_t)(a ^ Hole()); NZ(a, true); }
                else { a = (uint16_t)(a ^ Hole16()); NZ(a, false); }
                break;
            case 0xA1: {                                                 // LDA (dp,X)
                const uint16_t zeigerAd = (uint16_t)(d + Hole() + x);
                const uint16_t z = (uint16_t)(lies(zeigerAd) | (lies((uint16_t)(zeigerAd + 1)) << 8));
                const uint32_t ad = ((uint32_t)dbr << 16) + z;
                if (M()) { a = (uint16_t)((a & 0xFF00) | lies(ad)); NZ(a, true); }
                else { a = (uint16_t)(lies(ad) | (lies((ad + 1) & 0xFFFFFF) << 8)); NZ(a, false); }
                break;
            }
            case 0x9F: {                                                 // STA long,X
                const uint16_t l = Hole16();
                const uint32_t ad = ((((uint32_t)Hole() << 16) | l) + x) & 0xFFFFFF;
                schreibe(ad, (uint8_t)a);
                if (!M()) schreibe((ad + 1) & 0xFFFFFF, (uint8_t)(a >> 8));
                break;
            }
            case 0x8F: {                                                 // STA long
                const uint16_t l = Hole16();
                const uint32_t ad = ((uint32_t)Hole() << 16) | l;
                schreibe(ad, (uint8_t)a);
                if (!M()) schreibe((ad + 1) & 0xFFFFFF, (uint8_t)(a >> 8));
                break;
            }
            case 0xBF: {                                                 // LDA long,X
                const uint16_t l = Hole16();
                const uint32_t ad = ((((uint32_t)Hole() << 16) | l) + x) & 0xFFFFFF;
                if (M()) { a = (uint16_t)((a & 0xFF00) | lies(ad)); NZ(a, true); }
                else { a = (uint16_t)(lies(ad) | (lies((ad + 1) & 0xFFFFFF) << 8)); NZ(a, false); }
                break;
            }
            case 0xBD: {                                                 // LDA abs,X
                const uint32_t ad = (((uint32_t)dbr << 16) + Hole16() + x) & 0xFFFFFF;
                if (M()) { a = (uint16_t)((a & 0xFF00) | lies(ad)); NZ(a, true); }
                else { a = (uint16_t)(lies(ad) | (lies((ad + 1) & 0xFFFFFF) << 8)); NZ(a, false); }
                break;
            }
            case 0xFB: break;                                            // XCE (laeuft immer nativ)
            case 0x38: p |= 0x01; break;                                 // SEC
            case 0x88:                                                   // DEY
                y = X() ? (uint16_t)((y - 1) & 0xFF) : (uint16_t)(y - 1);
                NZ(y, X());
                break;
            case 0x5C: { const uint16_t w = Hole16(); pbr = Hole(); pc = w; break; }  // JML long
            case 0xAD: {                                                 // LDA abs
                const uint32_t ad = ((uint32_t)dbr << 16) | Hole16();
                if (M()) { a = (uint16_t)((a & 0xFF00) | lies(ad)); NZ(a, true); }
                else { a = (uint16_t)(lies(ad) | (lies((ad + 1) & 0xFFFFFF) << 8)); NZ(a, false); }
                break;
            }
            case 0xCD: {                                                 // CMP abs
                const uint32_t ad = ((uint32_t)dbr << 16) | Hole16();
                const uint16_t w = M() ? lies(ad) : (uint16_t)(lies(ad) | (lies((ad + 1) & 0xFFFFFF) << 8));
                const uint16_t v = M() ? (uint16_t)(a & 0xFF) : a;
                NZ((uint16_t)(v - w), M());
                p = (uint8_t)((p & ~0x01) | (v >= w ? 0x01 : 0));
                break;
            }
            case 0x8D: {                                                 // STA abs
                const uint32_t ad = ((uint32_t)dbr << 16) | Hole16();
                schreibe(ad, (uint8_t)a);
                if (!M()) schreibe((ad + 1) & 0xFFFFFF, (uint8_t)(a >> 8));
                break;
            }
            case 0x86: {                                                 // STX dp
                const uint32_t ad = (uint16_t)(d + Hole());
                schreibe(ad, (uint8_t)x);
                if (!X()) schreibe((uint16_t)(ad + 1), (uint8_t)(x >> 8));
                break;
            }
            case 0xB0: {                                                 // BCS
                const int8_t rel = (int8_t)Hole();
                if (p & 0x01) pc = (uint16_t)(pc + rel);
                break;
            }
            case 0x4B: Push(pbr); break;                                 // PHK
            case 0xEA: break;                                            // NOP
            case 0xAA:                                                   // TAX
                x = X() ? (uint16_t)(a & 0xFF) : a;
                NZ(x, X());
                break;
            case 0x9B:                                                   // TYX
                x = y;
                NZ(x, X());
                break;
            case 0xCA:                                                   // DEX
                x = X() ? (uint16_t)((x - 1) & 0xFF) : (uint16_t)(x - 1);
                NZ(x, X());
                break;
            case 0xC8:                                                   // INY
                y = X() ? (uint16_t)((y + 1) & 0xFF) : (uint16_t)(y + 1);
                NZ(y, X());
                break;
            case 0xC0: {                                                 // CPY #
                const uint16_t w = X() ? Hole() : Hole16();
                NZ((uint16_t)(y - w), X());
                p = (uint8_t)((p & ~0x01) | (y >= w ? 0x01 : 0));
                break;
            }
            case 0xE8:                                                   // INX
                x = X() ? (uint16_t)((x + 1) & 0xFF) : (uint16_t)(x + 1);
                NZ(x, X());
                break;
            case 0xE0: {                                                 // CPX #
                const uint16_t w = X() ? Hole() : Hole16();
                const uint16_t r = (uint16_t)(x - w);
                NZ(r, X());
                p = (uint8_t)((p & ~0x01) | (x >= w ? 0x01 : 0));
                break;
            }
            case 0xD0: {                                                 // BNE
                const int8_t rel = (int8_t)Hole();
                if (!(p & 0x02)) pc = (uint16_t)(pc + rel);
                break;
            }
            case 0x80: { const int8_t rel = (int8_t)Hole(); pc = (uint16_t)(pc + rel); break; }  // BRA
            case 0x4C: pc = Hole16(); break;                             // JMP abs
            case 0x6C: {                                                 // JMP (abs)
                const uint16_t w = Hole16();
                if (w == ende) return true;
                pc = (uint16_t)(lies(w) | (lies((uint16_t)(w + 1)) << 8));
                break;
            }
            default:
                return fail("Unbekannter Befehl", op);
        }
        if (d != d0 || dbr != dbr0) seitenGeaendert = true;
    }
    if (fehler) *fehler = "zu viele Schritte";
    return false;
}
