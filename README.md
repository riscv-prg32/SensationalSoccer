# Sensational Soccer

An original, cinematic 5-a-side soccer cartridge for the PRG32 RISC-V console.

The elevated broadcast camera combines whole-pitch playability with perspective-scaled players, animated stands, stereo stadium music, and fast arcade action. The cartridge starts in an autonomous demo match; press any control to play.

## Controls

| Control | Action |
|---|---|
| Stick | Move the selected blue player |
| A | Shoot |
| B | Pass forward |
| SELECT | Toggle demo mode |

## Build

Set `PRG32_REPO` if the PRG32 checkout is not the adjacent `../PRG32`, then run `./build.sh`. The script produces ESP32-C6 and QEMU cartridges plus a ready-to-publish Store bundle in `dist/`.

See [docs/architecture.md](docs/architecture.md) for implementation details.
