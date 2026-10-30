# RAW-SNES

RetroAchievements auf einem echten Super Nintendo. Kein Emulator, kein
ROM-Patch.

[English version](README.md)

RAW-SNES verbindet ein echtes Super Nintendo, ein SD2SNES / FXPak Pro und
RetroAchievements. Das Modul hält während des Spiels eine Kopie des
Arbeitsspeichers der Konsole. Das Programm auf dem PC liest sie über USB,
prüft die Achievements und schaltet sie auf deinem RetroAchievements-Konto
frei. Das Spiel selbst bleibt unverändert.

Wird ein Achievement freigeschaltet, erscheint eine Karte am Bildschirmrand.

## Was du brauchst

- Ein FXPak Pro oder SD2SNES. RAW-SNES wird auf einem FXPak Pro (Rev. Z) mit
  Firmware 1.11.2 entwickelt und getestet. Das SD2SNES nutzt dieselbe Firmware
  und sollte funktionieren, ist aber ungetestet.
- Ein originales Super Nintendo oder Super Famicom
- Ein USB-Kabel zwischen Modul und PC
- Windows
- Ein kostenloses RetroAchievements-Konto

## Los geht's

1. Die neueste Fassung des Programms herunterladen und starten.
2. Das Modul per USB mit dem PC verbinden. Das Programm findet es selbst.
3. Mit dem RetroAchievements-Konto anmelden.
4. Ein Spiel starten. Das Programm erkennt es selbst und beginnt, die
   Achievements zu prüfen.

## Spiele mit Zusatzchip

Bei Spielen mit SA-1-, Super-FX-, Cx4- oder S-DD1-Chip hält das Modul keine
Kopie des Arbeitsspeichers. Die Werte, die RAW-SNES braucht, kopiert dann die
Konsole selbst, einmal je Bild, über den In-Game-Hook des Moduls. Dafür den
In-Game-Hook in den Einstellungen des Moduls einschalten. Am Gerät getestet mit
SA-1, Super FX, Cx4 und S-DD1 (Star Ocean); OBC1 ist ungetestet.

Wegen einer Grenze der Originalfirmware des Moduls werden manche dieser Spiele
nur teilweise unterstützt: Nicht alle Achievements werden gleichzeitig
überwacht. Die übrigen kommen erst dazu, wenn durch Freischaltungen Platz frei
wird; was du vorher bei ihnen erreichst, zählt nicht. Das Programm zeigt das
deutlich in Rot an und markiert sie in der Liste als „nicht überwacht“. Volle
Unterstützung gibt es in der Master-Version (siehe unten).

Spiele mit DSP-Chip (Super Mario Kart, Top Gear 3000 und andere) laufen wie
Spiele ohne Chip. Das Modul selbst braucht für diese Spiele die
DSP-Firmware-Dateien in seinem Ordner `sd2snes`; ohne sie bleibt das Spiel
hängen, mit oder ohne RAW-SNES.

## Hardcore-Modus

Hardcore ist geplant, aber noch nicht aktiv. RetroAchievements wertet
Hardcore-Freischaltungen nur von Programmen, die es freigegeben hat, und ein
Programm muss dafür mindestens sechs Monate öffentlich verfügbar sein. Dafür ist
RAW-SNES noch zu neu, außerdem muss es eine Reihe von Prüfungen durch
RetroAchievements bestehen. Bis dahin wird jede Freischaltung als Softcore gebucht.

## Die Master-Version

Die Master-Version ist fertig und läuft auf echter Hardware. Sie bringt
Speicherstände mit Ton und ein Menü direkt an der Konsole, ohne PC, sowie
volle Unterstützung für alle Spiele mit Zusatzchip. Ungeklärte Lizenzfragen
verhindern derzeit eine Veröffentlichung. Fortsetzung folgt.

## Aufbau des Repositorys

In `src/` liegt das Programm. In `assets/` liegen das Symbol und die
Intro-Bilder, in `docs/` die Projektseite.

## Fehler und Rückmeldungen

Fehler bitte über [GitHub Issues](https://github.com/liquid-wq/raw-snes/issues)
melden, mit Spiel, Hardware und dem Protokoll aus dem Programmfenster. Nur
dort verfolge ich sie.

## Lizenz

RAW-SNES ist kostenlos nutzbar. Der Quelltext ist öffentlich zum Lesen und
Prüfen, aber nicht Open Source — siehe [LICENSE](LICENSE). Fremdkomponenten
stehen in [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).

Es gibt auch eine Fassung für das Mega Drive, [MEGA-RAW](https://github.com/liquid-wq/mega-raw),
eine für das NES, [RAW-NES](https://github.com/liquid-wq/raw-nes), und eine
für die Saturn, [RAW-TURN](https://github.com/liquid-wq/raw-turn).

Liqui
