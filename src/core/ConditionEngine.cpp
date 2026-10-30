#include "ConditionEngine.h"
#include "snes_speicher.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <regex>
#include <set>

namespace rawsnes {
namespace {

// Groessen-Tabelle: Groessenzeichen (klein) -> (nbytes, special, big_endian)
struct SizeInfo { int nbytes; std::string special; bool be; };

const std::map<std::string, SizeInfo>& sizeTable() {
    static const std::map<std::string, SizeInfo> t = {
        {"",  {2, "", false}}, {" ", {2, "", false}},
        {"h", {1, "", false}},
        {"x", {4, "", false}},
        {"w", {3, "", false}},
        {"i", {2, "", true}},
        {"j", {3, "", true}},
        {"g", {4, "", true}},
        {"m", {1, "bit0", false}}, {"n", {1, "bit1", false}},
        {"o", {1, "bit2", false}}, {"p", {1, "bit3", false}},
        {"q", {1, "bit4", false}}, {"r", {1, "bit5", false}},
        {"s", {1, "bit6", false}}, {"t", {1, "bit7", false}},
        {"l", {1, "low4", false}}, {"u", {1, "up4", false}},
        {"k", {1, "cnt", false}},
    };
    return t;
}

std::string toLowerStr(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return std::tolower(c); });
    return r;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Operand-Muster, Gruppen:
//  1=mod([dpb]?) 2=size_c 3=addr_hex 4=const_hex(nach 'h') 5=const_dec
//  6=const_f(nach 'f') 7=mod_op 8=mod_hex 9=mod_f('f' oder leer) 10=mod_dec
const std::regex& opRegex() {
    static const std::regex re(
        R"(^([dpb]?)(?:0x([hHxXwWiIjJgGmMnNoOpPqQrRsStTlLuUkK ]?)([0-9a-fA-F]+)|h([0-9a-fA-F]+)|(\d+(?:\.\d+)?)|f([0-9]+(?:\.[0-9]+)?))(?:([*/&^%])(?:0x([0-9a-fA-F]+)|(f?)([0-9]+(?:\.[0-9]+)?)))?$)");
    return re;
}

// Vergleichsoperatoren -- Reihenfolge der Alternativen ist wichtig (!=/<=/>=
// vor </>/=, sonst wuerde z.B. ">=" faelschlich als "=" nach ">" erkannt).
const std::regex& cmpRegex() {
    static const std::regex re(R"(!=|<=|>=|<|>|=)");
    return re;
}

Operand parseOperand(const std::string& tokenIn) {
    Operand out;
    std::string token = trim(tokenIn);
    if (token.empty()) return out; // valid=false -> "kein Operand"

    std::smatch m;
    if (!std::regex_match(token, m, opRegex())) return out; // valid=false

    std::string mod = m[1].str();
    std::string sizeC = m[2].str();
    std::string addrHex = m[3].str();
    std::string constHex = m[4].str();
    std::string constDec = m[5].str();
    std::string constF = m[6].str();
    std::string modOpS = m[7].str();
    std::string modHex = m[8].str();
    std::string modF = m[9].str();
    std::string modDec = m[10].str();

    char modOp = 0;
    double modVal = 1;
    bool modFloat = false;
    if (!modHex.empty()) {
        modVal = static_cast<double>(std::stoll(modHex, nullptr, 16));
        modOp = modOpS[0];
    } else if (!modDec.empty()) {
        modVal = std::stod(modDec);
        modOp = modOpS[0];
        modFloat = !modF.empty() || modDec.find('.') != std::string::npos;
    } else {
        modOp = 0;
        modVal = 1;
    }

    out.valid = true;
    out.mod = mod.empty() ? 0 : mod[0];
    out.modOp = modOp;
    out.modVal = modVal;
    out.modFloat = modFloat;

    if (!constHex.empty()) {
        out.isConst = true;
        out.constValue = static_cast<double>(std::stoll(constHex, nullptr, 16));
        return out;
    }
    if (!constDec.empty()) {
        out.isConst = true;
        out.constValue = std::stod(constDec);
        return out;
    }
    if (!constF.empty()) {
        out.isConst = true;
        out.isFloatConst = true;
        out.constValue = std::stod(constF);
        return out;
    }

    // Speicherzugriff
    uint32_t addr = static_cast<uint32_t>(std::stoul(addrHex, nullptr, 16));
    std::string sizeKey = (!sizeC.empty() && sizeC != " ") ? toLowerStr(sizeC) : sizeC;
    auto it = sizeTable().find(sizeKey);
    SizeInfo si = (it != sizeTable().end()) ? it->second : SizeInfo{1, "", false};

    out.ra = addr;
    out.nbytes = si.nbytes;
    out.special = si.special;
    out.bigEndian = si.be;
    // Alle Bytes des Zugriffs muessen im RA-Adressraum des SNES liegen. Ob sie
    // auch lesbar sind, haengt vom Lesemodus ab und prueft der Worker.
    if (!snes::RaGueltig(addr) || !snes::RaGueltig(addr + (uint32_t)si.nbytes - 1)) out.unsupported = true;
    return out;
}

Condition parseConditionLine(const std::string& condIn) {
    Condition c;
    std::string cond = condIn;

    // Flag: ^([A-Za-z]):(.*)$  -- Gross-/Kleinschreibung wird NICHT
    // normalisiert (bewusst -- ein kleines "a:" ist
    // NICHT dasselbe wie "A:" und macht die Bedingung unsupported).
    if (cond.size() >= 2 && std::isalpha(static_cast<unsigned char>(cond[0])) &&
        cond[1] == ':') {
        char f = cond[0];
        cond = cond.substr(2);
        c.flag = (f == 'Q') ? 0 : f; // Q = MeasuredIf, im Trigger-Pfad wie normale Bedingung
    }

    // Hit-Ziel: \.(\d+)\.$  -- strikt mit Schlusspunkt
    static const std::regex targetRe(R"(\.(\d+)\.$)");
    std::smatch tm;
    if (std::regex_search(cond, tm, targetRe)) {
        c.target = std::stoi(tm[1].str());
        cond = cond.substr(0, tm.position(0));
    }

    std::smatch cm;
    if (std::regex_search(cond, cm, cmpRegex())) {
        c.op = cm.str(0);
        c.left = parseOperand(cond.substr(0, cm.position(0)));
        c.right = parseOperand(cond.substr(cm.position(0) + cm.length(0)));
        c.hasRight = true;
    } else {
        c.op.clear();
        c.left = parseOperand(cond);
        c.hasRight = false;
    }
    return c;
}

// Teilt MemAddr in Alt-Gruppen. std::regex kennt kein
// Lookbehind, daher manueller Scan: an jedem 'S' trennen, das NICHT die
// zwei Zeichen "0x" direkt davor hat.
std::vector<std::string> splitAltGroups(const std::string& memaddr) {
    std::vector<std::string> parts;
    std::string cur;
    for (size_t i = 0; i < memaddr.size(); ++i) {
        char ch = memaddr[i];
        if (ch == 'S') {
            bool precededBy0x = (i >= 2 && memaddr[i - 2] == '0' && memaddr[i - 1] == 'x');
            if (!precededBy0x) {
                parts.push_back(cur);
                cur.clear();
                continue;
            }
        }
        cur.push_back(ch);
    }
    parts.push_back(cur);
    return parts;
}

std::vector<std::string> splitChar(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

const std::set<char>& supportedFlags() {
    // 0 steht fuer "kein Flag".
    // I = AddAddress (Zeiger): verschiebt die Adressen der naechsten Bedingung.
    static const std::set<char> s = {0, 'A', 'B', 'N', 'O', 'Z', 'P', 'R', 'T', 'M', 'C', 'D', 'I'};
    return s;
}

double applyMod(double v, const Operand& op) {
    if (op.modOp == 0) return v; // ohne modOp ist mult immer 1
    switch (op.modOp) {
        case '*': return v * op.modVal;
        // Ganzzahlige Operanden teilen ganzzahlig, wie bei RetroAchievements;
        // mit einer Kommazahl wird mit Nachkommastellen gerechnet.
        case '/':
            if (op.modVal == 0) return 0;
            if (op.modFloat || op.isFloatConst) return v / op.modVal;
            return static_cast<double>(static_cast<int64_t>(v) / static_cast<int64_t>(op.modVal));
        case '&': return static_cast<double>(static_cast<int64_t>(v) & static_cast<int64_t>(op.modVal));
        case '^': return static_cast<double>(static_cast<int64_t>(v) ^ static_cast<int64_t>(op.modVal));
        case '%': return op.modVal != 0
                      ? static_cast<double>(static_cast<int64_t>(v) % static_cast<int64_t>(op.modVal))
                      : 0;
        default: return v;
    }
}

double bcd(int64_t v, int nbytes) {
    int64_t total = 0;
    for (int shift = (nbytes * 2 - 1) * 4; shift >= 0; shift -= 4) {
        total = total * 10 + ((v >> shift) & 0xF);
    }
    return static_cast<double>(total);
}

// `versatz`: Wert der vorangehenden AddAddress-Bedingung (0 ohne Zeiger).
double readOperand(const Operand& op, const RamMap& ram, const RamMap* prev,
                   const RamMap& prior, int64_t versatz = 0) {
    if (op.isConst) return applyMod(op.constValue, op);

    const RamMap* src = &ram;
    if (op.mod == 'd') src = prev;
    else if (op.mod == 'p') src = &prior; // prior wird immer mitgefuehrt

    const uint32_t basis = (uint32_t)((int64_t)op.ra + versatz) & 0xFFFFFFu;
    int64_t v = 0;
    if (op.bigEndian) {
        for (int i = 0; i < op.nbytes; ++i) {
            auto it = src->find(raByteToPhys(basis + i));
            uint8_t b = (it != src->end()) ? it->second : 0;
            v = (v << 8) | b;
        }
    } else {
        for (int i = 0; i < op.nbytes; ++i) {
            auto it = src->find(raByteToPhys(basis + i));
            uint8_t b = (it != src->end()) ? it->second : 0;
            v |= static_cast<int64_t>(b) << (8 * i);
        }
    }

    if (!op.special.empty()) {
        if (op.special.rfind("bit", 0) == 0) {
            int bitNo = op.special[3] - '0';
            v = (v >> bitNo) & 1;
        } else if (op.special == "low4") {
            v = v & 0xF;
        } else if (op.special == "up4") {
            v = (v >> 4) & 0xF;
        } else if (op.special == "cnt") {
            v = static_cast<int64_t>(__builtin_popcount(static_cast<unsigned>(v & 0xFF)));
        }
    }
    double vd = static_cast<double>(v);
    if (op.mod == 'b') vd = bcd(v, op.nbytes);
    return applyMod(vd, op);
}

bool compareVals(double a, const std::string& op, double b) {
    if (op == "=")  return a == b;
    if (op == "!=") return a != b;
    if (op == ">")  return a > b;
    if (op == ">=") return a >= b;
    if (op == "<")  return a < b;
    if (op == "<=") return a <= b;
    return false;
}

} // namespace

std::vector<ConditionGroup> parseMemAddr(const std::string& memaddr) {
    std::vector<ConditionGroup> groups;
    for (const auto& grp : splitAltGroups(memaddr)) {
        ConditionGroup conds;
        for (const auto& piece : splitChar(grp, '_')) {
            std::string c = trim(piece);
            if (!c.empty()) conds.push_back(parseConditionLine(c));
        }
        if (!conds.empty()) groups.push_back(std::move(conds));
    }
    return groups;
}

bool isUnsupported(const std::vector<ConditionGroup>& groups) {
    if (groups.empty()) return true;
    for (auto& g : groups) {
        for (auto& c : g) {
            if (supportedFlags().find(c.flag) == supportedFlags().end()) return true;
            if (!c.left.valid) return true;
            if (!c.op.empty() && !c.right.valid) return true;
            if (c.left.valid && c.left.unsupported) return true;
            if (c.right.valid && c.right.unsupported) return true;
        }
    }
    return false;
}

std::vector<uint32_t> collectAddresses(
    const std::vector<std::pair<std::string, std::string>>& idAndMem) {
    std::set<uint32_t> addrs;
    for (auto& [id, memIn] : idAndMem) {
        (void)id;
        // Leaderboard-Werte trennen Alternativen mit '$'; fuer die Adressen
        // genuegt es, sie wie einzelne Bedingungen zu lesen.
        std::string mem = memIn;
        std::replace(mem.begin(), mem.end(), '$', '_');
        for (auto& grp : parseMemAddr(mem)) {
            for (auto& c : grp) {
                for (const Operand* side : {&c.left, &c.right}) {
                    if (side->valid && !side->isConst && !side->unsupported) {
                        for (int i = 0; i < side->nbytes; ++i) {
                            addrs.insert(raByteToPhys(side->ra + i));
                        }
                    }
                }
            }
        }
    }
    return {addrs.begin(), addrs.end()};
}

AchievementRuntime::AchievementRuntime(const std::string& memaddr) {
    groups_ = parseMemAddr(memaddr);
    unsupported_ = isUnsupported(groups_);
}

void AchievementRuntime::reset() {
    for (auto& g : groups_) for (auto& c : g) c.hits = 0;
}

void AchievementRuntime::trackPrior(const RamMap& ram) {
    for (auto& [addr, val] : ram) {
        auto it = last_.find(addr);
        if (it != last_.end() && it->second != val) prior_[addr] = it->second;
        last_[addr] = val;
    }
}

bool AchievementRuntime::walk(ConditionGroup& group, const RamMap& ram, const RamMap* prev,
                              bool pausePass, bool* resetOut) {
    double accum = 0;
    double hitPool = 0;
    std::optional<bool> chainVal;
    std::string chainOp; // "and" / "or" / ""
    bool resetNext = false;
    bool paused = false;
    bool satisfied = true;
    bool anyCountable = false;
    bool reset = false;
    int64_t versatz = 0;   // aus AddAddress, gilt fuer genau die naechste Bedingung

    for (auto& c : group) {
        char flag = c.flag;
        double rawVal = readOperand(c.left, ram, prev, prior_, versatz);

        // AddAddress: der Wert (samt Modifikator) verschiebt die naechste
        // Bedingung; mehrere hintereinander ergeben einen Zeiger auf Zeiger.
        if (flag == 'I') { versatz = (int64_t)rawVal; continue; }
        const int64_t vers = versatz;
        versatz = 0;

        if (flag == 'A') { accum += rawVal; continue; }
        if (flag == 'B') { accum -= rawVal; continue; }

        double lval = rawVal + accum;
        bool raw;
        if (!c.op.empty()) {
            raw = compareVals(lval, c.op, readOperand(c.right, ram, prev, prior_, vers));
        } else {
            raw = true;
        }

        bool combined;
        if (!chainVal.has_value()) combined = raw;
        else if (chainOp == "or") combined = *chainVal || raw;
        else combined = *chainVal && raw;

        if (flag == 'N') { chainVal = combined; chainOp = "and"; accum = 0; continue; }
        if (flag == 'O') { chainVal = combined; chainOp = "or"; accum = 0; continue; }
        if (flag == 'Z') {
            if (combined) resetNext = true;
            chainVal.reset(); chainOp.clear(); accum = 0;
            continue;
        }
        if (flag == 'C' || flag == 'D') {
            if (!pausePass) {
                if (resetNext) c.hits = 0;
                if (combined && (c.target == 0 || c.hits < c.target)) c.hits += 1;
            }
            hitPool += (flag == 'C') ? c.hits : -c.hits;
            resetNext = false;
            chainVal.reset(); chainOp.clear(); accum = 0;
            continue;
        }

        // reale Bedingung (kein Flag / P / R / T / M)
        accum = 0;
        chainVal.reset(); chainOp.clear();

        bool isPause = (flag == 'P');
        bool doHits = (pausePass && isPause) || (!pausePass && !isPause);

        bool sat;
        if (doHits) {
            if (resetNext) c.hits = 0;
            if (c.target > 0) {
                if (combined && c.hits < c.target) c.hits += 1;
                sat = (c.hits + hitPool) >= c.target;
            } else {
                sat = combined;
            }
        } else {
            if (c.target > 0) sat = (c.hits + hitPool) >= c.target;
            else sat = combined;
        }
        hitPool = 0;
        resetNext = false;

        if (isPause) {
            if (pausePass && sat) paused = true;
            continue;
        }
        if (pausePass) continue;

        if (flag == 'R') {
            if (sat) reset = true;
            continue;
        }

        anyCountable = true;
        if (!sat) satisfied = false;
    }

    if (pausePass) return paused;
    if (resetOut) *resetOut = reset;
    return anyCountable ? satisfied : true;
}

void AchievementRuntime::zielAdressen(const RamMap& ram, std::set<uint32_t>& aus) const {
    if (unsupported_) return;
    for (const auto& g : groups_) {
        int64_t versatz = 0;
        for (const auto& c : g) {
            const int64_t vers = versatz;
            versatz = 0;
            if (vers != 0) {
                for (const Operand* side : {&c.left, &c.right}) {
                    if (!side->valid || side->isConst) continue;
                    const uint32_t basis = (uint32_t)((int64_t)side->ra + vers) & 0xFFFFFFu;
                    for (int i = 0; i < side->nbytes; ++i) aus.insert(basis + (uint32_t)i);
                }
            }
            if (c.flag == 'I') versatz = (int64_t)readOperand(c.left, ram, &ram, prior_, vers);
        }
    }
}

bool AchievementRuntime::update(const RamMap& ram, const RamMap* prev) {
    if (unsupported_ || prev == nullptr) {
        trackPrior(ram);
        return false;
    }
    trackPrior(ram);

    std::vector<bool> paused;
    paused.reserve(groups_.size());
    for (auto& g : groups_) paused.push_back(walk(g, ram, prev, true, nullptr));

    std::vector<bool> results;
    results.reserve(groups_.size());
    bool resetAll = false;
    for (size_t i = 0; i < groups_.size(); ++i) {
        if (paused[i]) { results.push_back(false); continue; }
        bool rst = false;
        bool sat = walk(groups_[i], ram, prev, false, &rst);
        results.push_back(sat);
        if (rst) resetAll = true;
    }

    if (resetAll) { reset(); return false; }

    bool core = results.empty() ? true : results[0];
    bool anyAlt = false;
    for (size_t i = 1; i < results.size(); ++i) if (results[i]) { anyAlt = true; break; }
    bool hasAlts = results.size() > 1;
    return core && (!hasAlts || anyAlt);
}

AchievementRuntime::ProgressInfo AchievementRuntime::progress() const {
    ProgressInfo info;
    if (groups_.empty() || unsupported_) return info;
    const ConditionGroup& core = groups_[0];

    // Nur eine 'M'-Bedingung (Measured) mit Ziel > 1 -- wie bei
    // RetroAchievements. Bewusst kein Ersatz ueber "das hoechste Trefferziel":
    // Haltezeiten in Nebenbedingungen (z. B. 20 Bilder) sind kein Fortschritt
    // des Spielers.
    for (const auto& c : core) {
        if (c.flag == 'M' && c.target > 1) {
            info.current = std::min(c.hits, c.target);
            info.target = c.target;
            info.hasProgress = true;
            return info;
        }
    }
    return info;
}

// ================= Leaderboard-Wert =================
//
// Wertformat von RetroAchievements: Alternativen sind durch '$' getrennt, der
// groesste Wert zaehlt. Jede Alternative ist entweder die alte Summenform
// (Operanden mit '_' getrennt, jeder mit eigenem Faktor) oder eine
// Bedingungskette: A:/B: addieren bzw. ziehen ab, die Measured-Bedingung (M:)
// am Ende liefert den Wert -- ohne Vergleich ihren Betrag samt Kette, mit
// Vergleich die Zahl der Bilder seit dem Start, in denen er zutraf.

ValueRuntime::ValueRuntime(const std::string& expr) {
    const std::string t = trim(expr);
    if (t.empty()) { unsupported_ = true; return; }
    for (const auto& altText : splitChar(t, '$')) {
        Alternative alt;
        bool mitFlags = false;
        for (const auto& piece : splitChar(altText, '_')) {
            const std::string p = trim(piece);
            if (p.empty()) continue;
            Condition c = parseConditionLine(p);
            if (c.flag) mitFlags = true;
            alt.conds.push_back(c);
        }
        if (alt.conds.empty()) { unsupported_ = true; return; }
        if (!mitFlags) {
            // Alte Summenform: alles addieren, kein Vergleich erlaubt.
            for (auto& c : alt.conds) {
                if (c.hasRight) { unsupported_ = true; return; }
                c.flag = 'A';
            }
            alt.conds.back().flag = 'M';
        }
        int measured = 0;
        for (size_t i = 0; i < alt.conds.size(); ++i) {
            const Condition& c = alt.conds[i];
            if (c.flag != 'A' && c.flag != 'B' && c.flag != 'M') { unsupported_ = true; return; }
            if (!c.left.valid || c.left.unsupported || c.left.mod == 'p') { unsupported_ = true; return; }
            if (c.hasRight && (!c.right.valid || c.right.unsupported || c.right.mod == 'p')) { unsupported_ = true; return; }
            if (c.flag != 'M' && c.hasRight) { unsupported_ = true; return; }
            if (c.target) { unsupported_ = true; return; }
            if (c.flag == 'M') {
                ++measured;
                if (i + 1 != alt.conds.size()) { unsupported_ = true; return; }
            }
        }
        if (measured != 1) { unsupported_ = true; return; }
        alts_.push_back(std::move(alt));
    }
}

void ValueRuntime::reset() {
    for (auto& a : alts_) { a.hits = 0; a.wert = 0; }
}

void ValueRuntime::update(const RamMap& ram, const RamMap* prev) {
    if (unsupported_) return;
    const RamMap& vorher = prev ? *prev : ram;
    const RamMap leer;
    for (auto& a : alts_) {
        double accum = 0;
        for (const auto& c : a.conds) {
            const double v = readOperand(c.left, ram, &vorher, leer);
            if (c.flag == 'A') { accum += v; continue; }
            if (c.flag == 'B') { accum -= v; continue; }
            if (c.hasRight) {
                if (compareVals(accum + v, c.op, readOperand(c.right, ram, &vorher, leer))) ++a.hits;
                a.wert = a.hits;
            } else {
                a.wert = accum + v;
            }
        }
    }
}

double ValueRuntime::value() const {
    double best = 0;
    for (size_t i = 0; i < alts_.size(); ++i)
        if (i == 0 || alts_[i].wert > best) best = alts_[i].wert;
    return best;
}

// ================= LeaderboardRuntime =================

LeaderboardRuntime::LeaderboardRuntime(long long id, std::string title, std::string format,
                                       const std::string& startMem, const std::string& cancelMem,
                                       const std::string& submitMem, const std::string& valueMem)
    : id_(id), title_(std::move(title)), format_(std::move(format)) {
    startRt_ = std::make_unique<AchievementRuntime>(startMem);
    cancelRt_ = std::make_unique<AchievementRuntime>(cancelMem);
    submitRt_ = std::make_unique<AchievementRuntime>(submitMem);
    valueRt_ = std::make_unique<ValueRuntime>(valueMem);
    unsupported_ = startRt_->unsupported() || cancelRt_->unsupported() || submitRt_->unsupported() ||
                   valueRt_->unsupported();
}

void LeaderboardRuntime::reset() {
    started_ = false;
    submitted_ = false;
    startRt_->reset();
    cancelRt_->reset();
    submitRt_->reset();
    valueRt_->reset();
}

LeaderboardRuntime::Event LeaderboardRuntime::update(const RamMap& ram, const RamMap* prev,
                                                      double* outValue) {
    if (unsupported_ || submitted_ || prev == nullptr) return Event::None;

    if (!started_) {
        if (startRt_->update(ram, prev)) {
            started_ = true;
            valueRt_->reset();
            valueRt_->update(ram, prev);
            return Event::Start;
        }
        return Event::None;
    }

    valueRt_->update(ram, prev);
    if (cancelRt_->update(ram, prev)) {
        started_ = false;
        return Event::Cancel;
    }

    if (submitRt_->update(ram, prev)) {
        submitted_ = true;
        if (outValue) *outValue = valueRt_->value();
        return Event::Submit;
    }
    return Event::None;
}

} // namespace rawsnes