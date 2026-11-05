# Reproducing Sensational Soccer

## Prerequisites

- A PRG32 checkout adjacent to this repository, or `PRG32_REPO` pointing to it.
- Python 3 and the toolchains reported by `python3 -m prg32 doctor`.
- ESP-IDF exported with `source "$HOME/esp-idf/export.sh"`. This provides `riscv32-esp-elf-gcc` and the Espressif `qemu-system-riscv32` with SDL; Homebrew QEMU has no SDL display.
- `ffmpeg`/`ffprobe` with libx264 and AAC, plus macOS `screencapture` and `swift`, for Store media capture.
- For hardware tests, one ESP32-C6 PRG32 board connected by native USB.

Record `git rev-parse HEAD` in this repository and in `PRG32_REPO` with every result. Generated files live in `build/`, `dist/` and `store-bundle/`. The source of the AUDIO block is `scripts/generate_audio.py`.

## Deterministic build

```bash
export PRG32_REPO="${PRG32_REPO:-../PRG32}"
source "$HOME/esp-idf/export.sh"
./build.sh
shasum -a 256 dist/*.prg32 dist/*.zip
```

`build.sh` does the following:

1. regenerates `assets/audio.json` and `assets/sfx/*.raw`;
2. packs one AUDIO block;
3. builds the ESP32-C6 and QEMU portable cartridges with the audio and multiplayer feature requirements;
4. attaches the metadata, colophon, icon and screenshot;
5. packs `dist/sensational-soccer-<version>.zip`, taking the version from `manifest.json`.

Validate the JSON afterwards:

```bash
for f in manifest.json metadata.json colophon.json assets/audio.json; do python3 -m json.tool "$f" >/dev/null && echo "ok $f"; done
git diff --check
```

## Host simulation (optional, for tuning)

`game.c` compiles on the host against a stub `prg32.h` whose drawing and audio calls do nothing. Rename the freestanding `memset`/`memcpy` before compiling. Release 1.1.0 was checked this way:

- **Deadlocks:** 2 hours of AI-vs-AI play reached no restart or possession deadlock.
- **Memory safety:** 120–200 four-minute sessions with random human input under AddressSanitizer and UBSan reported no hang and no undefined behaviour.
- **Scoring:** demo matches averaged about one goal and three shots per minute.

## QEMU test

From `PRG32_REPO`, build or reuse the QEMU firmware, stage the cartridge and run it:

```bash
python3 -m prg32 qemu build
python3 -m prg32 qemu upload ../SensationalSoccer/dist/sensational-soccer-qemu.prg32
python3 -m prg32 qemu run
```

The firmware boots to PRG32 SETUP; choose RUN CARTRIDGE → cart0. The QEMU UART keyboard maps:

| Key | Button |
|---|---|
| d / a / w / s | RIGHT / LEFT / UP / DOWN |
| h | A |
| j | B |
| Enter | SELECT |

Avoid `l` (D): in the PRG32 QEMU firmware it can freeze the emulator.

Checklist:

- [ ] The title music is audible and does not distort.
- [ ] The title, team and mode screens respond to input.
- [ ] The attract demo starts after 10 s and scrolls horizontally.
- [ ] The crowd animates, the ball rises and bounces, and markings stay stable while scrolling.
- [ ] In a match, the music is off and only effects play: kicks, touches, tackles, bounces, the whistle, the crowd, the post and the net.
- [ ] A shoots and slides, B passes and switches, and SELECT returns to the title.
- [ ] Goals, corners, goal kicks and throw-ins resolve.
- [ ] After full time the result screen plays the fanfare. The console log shows `PLAY_TRACK ... track 1` only there, and `track 0` only on the title.

## Store media capture

```bash
source "$HOME/esp-idf/export.sh"
./build.sh
python3 scripts/capture_media.py --keep /tmp/soccer-capture
./build.sh     # embed the new screenshot in the cartridges and bundle
```

`scripts/capture_media.py` boots a clean copy of the PRG32 QEMU flash with only the fresh QEMU cartridge in cart0 and launches it. It then:

- records the emulator's real 22,050 Hz UART PCM stream (paced at 20 ms credits);
- records the QEMU SDL window with `screencapture`, because Espressif QEMU has no QMP `screendump`;
- injects the scripted key sequence on the PCM clock: title, team selection (TOKYO vs RIO), single match, and 43 s of human dribbling, passing and shooting;
- retimes the video to the PCM clock and crops the 320 × 200 viewport (raw 2× window, viewport top 96 px);
- writes `assets/preview.mp4` (60 s, H.264 CRF 16 with AAC 128 kbit/s) and `assets/screenshot.png` (320 × 200, taken from the raw video at 30 s).

The script rejects and retries a take (`--attempts`, default 4) in either of two cases:

- the emulator stalls. The PRG32 QEMU firmware can intermittently freeze while its multiplayer client reconnects to an unreachable room server; the emulated NIC cannot be removed because the firmware aborts without it.
- the host mouse pointer crosses the QEMU window, because `screencapture` video always includes the pointer. The window opens in the bottom-right corner of the main display, and `--allow-pointer` is for verification runs only.

Captured media must never be edited, synthesized or combined from several runs.

## Physical board test

List the devices, then upload to an explicit slot. This procedure writes, and overwrites, `cart0`:

```bash
cd "$PRG32_REPO"
python3 -m prg32 esp32c6 ports
python3 -m prg32 esp32c6 upload ../SensationalSoccer/dist/sensational-soccer-esp32c6.prg32 --port /dev/cu.usbmodem1101 --slot cart0 --command run
python3 -m prg32 esp32c6 key --help        # drive the controls over the device protocol
```

Substitute the actual enumerated port. Repeat the QEMU checklist on the LCD and joystick, and also check:

- colours match QEMU (sprites, text and panels must not shift hue);
- the frame rate stays responsive;
- sprites leave no trails;
- the title and result music is audible, and gameplay contains only effects.

Report which slot was overwritten.

## Multiplayer test

1. Run the PRG32 Multiplayer Server and configure both firmware instances with its WebSocket URL.
2. Start the same cartridge on two devices or QEMU instances. Both join the room `sensational-soccer-v2`.
3. Confirm that `NET` appears and that the second console's controls move the away team.
4. Disconnect that peer and confirm that the away-team AI takes over again.

Network play needs the firmware's Wi-Fi or network configuration; the game is complete offline.

## Acceptance record

For a reproducible report, save:

- commit IDs and toolchain versions;
- hashes of the artifacts;
- the board profile, serial port and cartridge slot;
- the test date;
- the result of every checklist item.

A physical upload changes only the declared slot. Back the slot up first if it holds user data.
