#include "snes_speicher.h"

#include <algorithm>

namespace snes {

bool RaGueltig(uint32_t ra) { return ra < kRaIramEnde; }

bool RaLesbar(uint32_t ra) { return ra < kRaSramEnde; }

std::optional<uint32_t> FxPakAdresse(uint32_t ra) {
    if (ra < kRaWramEnde) return fxpak::kWram + ra;
    if (ra < kRaSramEnde) return fxpak::kSram + (ra - kRaWramEnde);
    return std::nullopt;
}

namespace {
// Grenze zwischen WRAM und SRAM: auf beiden Seiten liegen die Daten im FXPak
// an voellig anderen Stellen, ein Bereich darf sie nicht ueberspannen.
bool GleicherSpeicher(uint32_t a, uint32_t b) { return (a < kRaWramEnde) == (b < kRaWramEnde); }
}  // namespace

Leseplan::Leseplan(const std::vector<uint32_t>& raAdressen, uint32_t luecke) {
    for (uint32_t a : raAdressen)
        if (RaLesbar(a)) adressen_.push_back(a);
    std::sort(adressen_.begin(), adressen_.end());
    adressen_.erase(std::unique(adressen_.begin(), adressen_.end()), adressen_.end());

    // 1. Zusammenhaengende Laeufe bilden.
    std::vector<Stueck> laeufe;
    for (uint32_t a : adressen_) {
        if (!laeufe.empty()) {
            Stueck& l = laeufe.back();
            const uint32_t ende = l.ra + l.laenge;  // erstes Byte danach
            if (a < ende + luecke + 1 && GleicherSpeicher(l.ra, a)) {
                l.laenge = a - l.ra + 1;
                continue;
            }
        }
        laeufe.push_back({a, 1});
    }

    // 2. In Stuecke zu hoechstens 255 Byte teilen, je acht in eine Lesung.
    for (const Stueck& l : laeufe) {
        for (uint32_t o = 0; o < l.laenge; o += fxpak::kMaxBereichLaenge) {
            const uint32_t n = std::min<uint32_t>(fxpak::kMaxBereichLaenge, l.laenge - o);
            if (lesungen_.empty() || lesungen_.back().bereiche.size() == (size_t)fxpak::kMaxBereiche) {
                lesungen_.emplace_back();
                stuecke_.emplace_back();
            }
            const uint32_t ra = l.ra + o;
            lesungen_.back().bereiche.push_back({*FxPakAdresse(ra), (uint8_t)n});
            lesungen_.back().bytes += n;
            stuecke_.back().push_back({ra, n});
        }
    }
}

std::map<uint32_t, uint8_t> Leseplan::Verteile(const std::vector<std::vector<uint8_t>>& antworten) const {
    // Erst alle gelesenen Bytes ablegen, dann nur die verlangten behalten.
    std::map<uint32_t, uint8_t> alle;
    for (size_t i = 0; i < stuecke_.size() && i < antworten.size(); ++i) {
        size_t pos = 0;
        for (const Stueck& s : stuecke_[i]) {
            for (uint32_t k = 0; k < s.laenge && pos < antworten[i].size(); ++k, ++pos)
                alle[s.ra + k] = antworten[i][pos];
        }
    }
    std::map<uint32_t, uint8_t> aus;
    for (uint32_t a : adressen_) {
        auto it = alle.find(a);
        if (it != alle.end()) aus[a] = it->second;
    }
    return aus;
}

}  // namespace snes
