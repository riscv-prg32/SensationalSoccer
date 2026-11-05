# Reproducing Sensational Soccer

## Prerequisites

- A PRG32 checkout adjacent to this repository, or `PRG32_REPO` pointing to it.
- Python 3 and the toolchains reported by `python3 -m prg32 doctor`.
- ESP-IDF exported with `source "$HOME/esp-idf/export.sh"`.
- For hardware tests, one ESP32-C6 PRG32 board connected by native USB.

Record `git rev-parse HEAD` in this repository and in `PRG32_REPO` with every result. Generated files live in `build/`, `dist/`, and `store-bundle/`; the source of the audio block is `scripts/generate_audio.py`.

## Deterministic build

```bash
export PRG32_REPO="${PRG32_REPO:-../PRG32}"
source "$HOME/esp-idf/export.sh"
./build.sh
sha256sum dist/*.prg32 dist/*.zip
```

`build.sh` regenerates `assets/audio.json`, packs one AUDIO block, builds ESP32-C6 and QEMU portable cartridges with the audio and multiplayer feature requirements, attaches metadata, and produces the Store bundle.

## QEMU test

From `PRG32_REPO`, build or reuse the QEMU firmware, stage the cartridge, and run it:

```bash
python3 -m prg32 qemu build
python3 -m prg32 qemu upload ../SensationalSoccer/dist/sensational-soccer-qemu.prg32
python3 -m prg32 qemu run --no-audio
```

Verify that title music is audible, the attract match scrolls horizontally, the crowd animates, the ball visibly rises and bounces, and field markings remain stable while scrolling. Start play and check that music stops while movement, A shooting, B passing, possession changes, collisions, goals, and match effects continue.

## Physical board test

List devices, then upload to an explicit replaceable slot. This procedure writes `cart0`.

```bash
cd "$PRG32_REPO"
python3 -m prg32 esp32c6 ports
python3 -m prg32 esp32c6 upload ../SensationalSoccer/dist/sensational-soccer-esp32c6.prg32 --port /dev/cu.usbmodem1101 --slot cart0 --command run
```

Repeat the QEMU checklist on the LCD and joystick. Confirm that the frame rate remains responsive, sprites do not leave trails, title/result music is audible, and gameplay contains only match effects. Substitute the actual enumerated port; do not assume the example port on another computer.

## Multiplayer test

Run the PRG32 Multiplayer Server, configure both firmware instances to its WebSocket URL, and start the same cartridge on two devices or QEMU instances. Both must use the room signature `sensational-soccer-v2`. Confirm `NET` appears, controls from the second console move red, and disconnecting that peer restores red-team AI. Network multiplayer requires firmware Wi-Fi/network configuration; the gameplay remains complete offline.

## Acceptance record

For a reproducible report, save commit IDs, toolchain versions, hashes, board profile, serial port, cartridge slot, test date, and the result of every checklist item. A physical upload changes only the declared slot; back it up first if it contains user data.
