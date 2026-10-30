#include "rom_vergleich.h"

#include "fxpak_protokoll.h"

#include <algorithm>
#include <cstdio>

namespace romvergleich {

Ergebnis Vergleiche(FxPakPort& fx, Bytes datei, const std::function<void(uint32_t, long)>& fortschritt) {
    Ergebnis e;
    if (datei.size() % 1024 == 512) datei.erase(datei.begin(), datei.begin() + 512);
    constexpr uint32_t kHappen = 0x10000;
    for (uint32_t a = 0; a < datei.size(); a += kHappen) {
        const uint32_t n = std::min<uint32_t>(kHappen, (uint32_t)datei.size() - a);
        const Bytes r = fx.Lesen(fxpak::kRom + a, n);
        e.verglichen += n;
        if (r.size() != n) {
            ++e.kurz;
            continue;
        }
        if (!std::equal(r.begin(), r.end(), datei.begin() + a)) {
            // Abweichender Happen: ein zweites Mal lesen. Bleibt der Wert,
            // steht er wirklich so im Modul.
            const Bytes r2 = fx.Lesen(fxpak::kRom + a, n);
            for (uint32_t i = 0; i < n; ++i) {
                if (r[i] == datei[a + i]) continue;
                ++e.abweichend;
                const bool bleibt = r2.size() == n && r2[i] == r[i];
                e.bleibend += bleibt;
                if (e.beispiele.size() < 30) {
                    char z[96];
                    std::snprintf(z, sizeof z, "$%06X: Modul %02X, Datei %02X%s", a + i, r[i], datei[a + i],
                                  bleibt ? "" : " (beim zweiten Lesen anders)");
                    e.beispiele.push_back(z);
                }
            }
        }
        if (fortschritt) fortschritt(e.verglichen, e.abweichend);
    }
    return e;
}

}  // namespace romvergleich
