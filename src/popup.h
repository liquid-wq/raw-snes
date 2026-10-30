// Einblendung bei freigeschaltetem Achievement.
//
// Gestaltung und Zeitachse wie bei RAW-NES: die Karte
// schiebt sich unten rechts herein, federt nach, ein Glanz laeuft darueber,
// Funken stieben, die Punkteleiste laeuft voll, dann verschwindet sie wieder.
// Gezeichnet mit GDI+ in ein geschichtetes Win32-Fenster, das ueber allen
// Fenstern liegt und keine Eingaben annimmt -- dieselbe Umsetzung wie in
// raw-turn.
//
// Besonderheit: Kommt waehrend der Anzeige ein zweites
// Achievement, wird es danach gezeigt, statt das erste abzuschneiden.
#pragma once

#include <string>

namespace popup {

// badgePng darf leer sein -- dann wird ein Pokal gezeichnet.
void Zeigen(const std::string& titel, const std::string& beschreibung, int punkte,
            const std::string& badgePng, bool englisch, float skalierung);

// Beim Programmende aufrufen.
void Beenden();

}  // namespace popup
