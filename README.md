# Sensational Soccer

An original, horizontally scrolling six-a-side football cartridge for the PRG32 RISC-V console (version 1.1.0).

A touchline camera follows play across a 960-pixel pitch below a floodlit, parallax grandstand with an animated crowd and scrolling advertising boards. Players are mirrored 4-bit indexed sprites with idle, four-phase run, kick, slide-tackle and celebration frames. Keepers position themselves, predict shots, dive, hold and distribute the ball. The fixed-point simulation covers ball height, bounce, spin and aftertouch, posts and crossbar, corners, goal kicks and throw-ins. It is inspired by the readability of early home-computer football games, not by their art, code, names or content.

![Gameplay captured from PRG32 QEMU](assets/screenshot.png)

The title screen leads to team selection (Napoli, London, Tokyo, Rio), then to a single 90-second match or a three-round world tour against new rivals. Left idle for ten seconds, the title starts a 60-second AI demo match.

**Audio policy:** music plays only on the title and result screens. During matches the soundtrack is off and you hear only synthesized event effects: kicks, dribble touches, tackles, bounces, the referee's whistle, crowd roar and "ooh", post clang and net swish.

## Controls

| Control | With the ball | Without the ball | Menus |
|---|---|---|---|
| Stick | Dribble; aftertouch (curl and dip) right after a shot | Run | Choose |
| A | Shoot | Slide tackle | Confirm |
| B | Pass to the best-placed team-mate | Switch to the player nearest the ball | Back |
| SELECT | Return to the title | Return to the title | Title → team selection |

Control switches automatically to the team-mate best placed to reach the ball. A marker above the player shows who you control. When two consoles join the `sensational-soccer-v2` multiplayer room, the first remote peer controls the away team, and the HUD shows `NET`. With no peer, the away team stays AI-controlled.

## Build

Set `PRG32_REPO` if the PRG32 checkout is not the adjacent `../PRG32`, export ESP-IDF, then run `./build.sh`. The build produces:

- `dist/sensational-soccer-esp32c6.prg32` and `dist/sensational-soccer-qemu.prg32`;
- the ready-to-publish Store bundle `dist/sensational-soccer-1.1.0.zip`, containing both cartridges, the manifest, colophon, icon, screenshot and 60-second preview.

Regenerate the Store media from a real QEMU run with `python3 scripts/capture_media.py`.

- [docs/gameplay.md](docs/gameplay.md): rules, AI and presentation.
- [docs/architecture.md](docs/architecture.md): implementation, colour model and audio design.
- [docs/reproducibility.md](docs/reproducibility.md): exact build, QEMU, media, board and multiplayer procedures.

The publication build uses master volume 160. Before Central European night-time testing, set it to zero (see `AGENTS.md`).
