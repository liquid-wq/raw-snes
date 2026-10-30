# RAW-SNES

RetroAchievements on a real Super Nintendo. No emulator, no ROM patch.

[Deutsche Fassung](README.de.md)

RAW-SNES connects a real Super Nintendo, an SD2SNES / FXPak Pro and
RetroAchievements. The cartridge keeps a copy of the console's work RAM while
the game runs. The program on the PC reads it over USB, checks the
achievements and unlocks them on your RetroAchievements account. The game
itself stays untouched.

When an achievement unlocks, a card appears at the edge of your screen.

## What you need

- An FXPak Pro or SD2SNES. RAW-SNES is developed and tested on an FXPak Pro
  (Rev. Z) with firmware 1.11.2. The SD2SNES uses the same firmware and should
  work, but is untested.
- An original Super Nintendo or Super Famicom
- A USB cable between the cartridge and the PC
- Windows
- A free RetroAchievements account

## Getting started

1. Download the latest release of the program and start it.
2. Connect the cartridge to the PC over USB. The program finds it by itself.
3. Log in with your RetroAchievements account.
4. Start a game. The program recognises it by itself and starts checking the
   achievements.

## Games with enhancement chips

For games with an SA-1, Super FX, Cx4 or S-DD1 chip the cartridge does not
keep a copy of the work RAM. The console copies the values RAW-SNES needs
itself, once per frame, through the in-game hook of the cartridge. Turn the
in-game hook on in the cartridge's settings for this. Tested on hardware with
SA-1, Super FX, Cx4 and S-DD1 (Star Ocean); OBC1 is untested.

Because of a limit of the cartridge's original firmware, some of these games
are only partly supported: not every achievement is monitored at
once. The others are only monitored once unlocks free up room, and anything you
achieve for them before that does not count. The program shows this clearly in
red and marks them as "not monitored" in the list. Full support is in the master
version (see below).

Games with a DSP chip (Super Mario Kart, Top Gear 3000 and others) work like
games without a chip. The cartridge itself needs the DSP firmware files in its
`sd2snes` folder for these games; without them the game hangs, with or without
RAW-SNES.

## Hardcore mode

Hardcore is planned, but not active yet. RetroAchievements only counts Hardcore
unlocks from clients it has approved, and a client has to be publicly available
for at least six months before it can be considered. RAW-SNES is still too new
for that, and it also has to pass a set of checks by RetroAchievements. Until
then, every unlock is recorded as Softcore.

## The master version

The master version is finished and runs on real hardware. It adds save
states with sound and a menu right on the console, without a PC, and full
support for every enhancement chip game. Unresolved licensing questions
currently keep it from being released. To be continued.

## Repository layout

`src/` holds the program. `assets/` holds the icon and the intro images,
`docs/` the project page.

## Bugs and feedback

Please report bugs through [GitHub Issues](https://github.com/liquid-wq/raw-snes/issues),
with the game, your hardware and the log from the program window. That is the
only place I track them.

## License

RAW-SNES is free to use. The source is public to read and audit, but it is
not open source — see [LICENSE](LICENSE). Third-party components are listed
in [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).

There is also a Mega Drive version, [MEGA-RAW](https://github.com/liquid-wq/mega-raw),
a NES version, [RAW-NES](https://github.com/liquid-wq/raw-nes), and a Saturn
version, [RAW-TURN](https://github.com/liquid-wq/raw-turn).

Liqui
