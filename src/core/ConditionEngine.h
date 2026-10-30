// ConditionEngine.h -- Auswertung der RetroAchievements-Bedingungen
// (rcheevos-Semantik: AddSource/SubSource, AddAddress, AndNext/OrNext,
// ResetNextIf, PauseIf, ResetIf, Trigger, Measured, Hit-Counts,
// Alt-Gruppen, Delta/Prior/BCD, Multiplikator/Bitmaske, Bit-Groessen).
//
// Diese Auswertung entscheidet, ob ein Achievement ausloest -- Fehler hier
// sind nicht tolerierbar. Geprueft im Selbsttest (src/selbsttest.cpp).
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace rawsnes {

// Ein geparster Operand (Speicherzugriff oder Konstante).
struct Operand {
    bool isConst = false;
    bool isFloatConst = false;   // 'f'-Konstante (Kommazahl)
    double constValue = 0;

    uint32_t ra = 0;             // RA-Byte-Adresse (nur wenn !isConst)
    int nbytes = 1;
    // special: "" | "bit0".."bit7" | "low4" | "up4" | "cnt"
    std::string special;
    bool bigEndian = false;

    char mod = 0;                 // 'd' (delta) / 'p' (prior) / 'b' (BCD) / 0
    char modOp = 0;                // '*' '/' '&' '^' '%' / 0
    double modVal = 1;
    bool modFloat = false;         // Faktor als Kommazahl ('f' oder mit Punkt)

    bool unsupported = false;     // am Geraet nicht lesbar o.ae.
    bool valid = false;           // false = Parse-Fehler (Token nicht erkannt)
};

// Eine geparste Bedingung (eine Zeile zwischen '_' in MemAddr).
struct Condition {
    char flag = 0;           // 0 = keine, sonst 'A','B','I','N','O','Z','P','R','T','M','C','D'
    Operand left;
    std::string op;           // "" "=" "!=" "<" ">" "<=" ">="
    Operand right;
    bool hasRight = false;
    int target = 0;           // Hit-Ziel (.N.), 0 = kein Ziel
    mutable int hits = 0;      // veraenderlich waehrend der Auswertung
};

using ConditionGroup = std::vector<Condition>;

// Gruppe 0 = Core, Rest = Alt-Gruppen (S-getrennt).
std::vector<ConditionGroup> parseMemAddr(const std::string& memaddr);

bool isUnsupported(const std::vector<ConditionGroup>& groups);

// Schluessel der RamMap ist die RA-Adresse selbst (SNES: kein Byteswap).
// Die Umrechnung auf Geraeteadressen macht snes_speicher.h.
inline uint32_t raByteToPhys(uint32_t raAddr) { return raAddr; }

// Alle physischen Adressen, die die Achievements dieses Spiels brauchen.
std::vector<uint32_t> collectAddresses(
    const std::vector<std::pair<std::string, std::string>>& idAndMem);

using RamMap = std::map<uint32_t, uint8_t>;

// Stateful-Auswertung eines einzelnen Achievements ueber Frames hinweg
// (Hit-Counter, Delta/Prior-Tracking).
class AchievementRuntime {
public:
    explicit AchievementRuntime(const std::string& memaddr);

    bool unsupported() const { return unsupported_; }
    void reset();

    // Pro Frame aufrufen. true = Achievement feuert jetzt.
    bool update(const RamMap& ram, const RamMap* prev);

    // Adressen, auf die AddAddress-Zeiger (I:) mit den Werten in `ram`
    // gerade zeigen. Sie sind nicht fest und muessen je Bild zusaetzlich
    // gelesen werden.
    void zielAdressen(const RamMap& ram, std::set<uint32_t>& aus) const;

    // Fuer den Fortschrittsbalken: Hit-Stand und Ziel der Bedingung mit
    // Flag 'M' (Measured) in der Core-Gruppe, wie bei RetroAchievements.
    // hasProgress=false ohne Measured-Bedingung mit Ziel > 1.
    struct ProgressInfo { int current = 0; int target = 0; bool hasProgress = false; };
    ProgressInfo progress() const;

private:
    bool walk(ConditionGroup& group, const RamMap& ram, const RamMap* prev,
             bool pausePass, bool* resetOut);
    void trackPrior(const RamMap& ram);

    std::vector<ConditionGroup> groups_;
    bool unsupported_ = false;
    RamMap prior_;   // phys-Adresse -> Wert vor der letzten Aenderung
    RamMap last_;    // phys-Adresse -> letzter gesehener Wert
};

// Leaderboard-Wert (VAL:-Ausdruck) im Wertformat von RetroAchievements:
// '$'-Alternativen (der groesste zaehlt), je Alternative die alte Summenform
// oder eine A:/B:-Kette mit abschliessender Measured-Bedingung (M:). Mit
// Vergleich zaehlt M: die Bilder seit reset(). Andere Formen gelten als
// nicht unterstuetzt -- dann wird nichts eingereicht.
class ValueRuntime {
public:
    explicit ValueRuntime(const std::string& expr);
    bool unsupported() const { return unsupported_; }
    void reset();
    void update(const RamMap& ram, const RamMap* prev);
    double value() const;

private:
    struct Alternative {
        std::vector<Condition> conds;
        int hits = 0;
        double wert = 0;
    };
    std::vector<Alternative> alts_;
    bool unsupported_ = false;
};

// Ein einzelner Leaderboard-Eintrag: Start-/Abbruch-/Einreich-Bedingung
// (wiederverwendet AchievementRuntime -- gleiche Trigger-Syntax) plus
// der Wert (ValueRuntime).
class LeaderboardRuntime {
public:
    LeaderboardRuntime(long long id, std::string title, std::string format,
                       const std::string& startMem, const std::string& cancelMem,
                       const std::string& submitMem, const std::string& valueMem);

    long long id() const { return id_; }
    const std::string& title() const { return title_; }
    const std::string& format() const { return format_; }
    bool unsupported() const { return unsupported_; }
    bool started() const { return started_; }
    bool submitted() const { return submitted_; }
    void reset();

    enum class Event { None, Start, Cancel, Submit };
    // Pro Frame aufrufen. Bei Event::Submit steht der Wert in outValue.
    Event update(const RamMap& ram, const RamMap* prev, double* outValue);

private:
    long long id_;
    std::string title_;
    std::string format_;
    bool unsupported_ = false;
    bool started_ = false;
    bool submitted_ = false;
    std::unique_ptr<AchievementRuntime> startRt_;
    std::unique_ptr<AchievementRuntime> cancelRt_;
    std::unique_ptr<AchievementRuntime> submitRt_;
    std::unique_ptr<ValueRuntime> valueRt_;
};

} // namespace rawsnes
