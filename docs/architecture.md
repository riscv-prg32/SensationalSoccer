# Sensational Soccer architecture

The cartridge is one portable PRG32 program, `game.c`, with the entry points `sensational_soccer_init`, `sensational_soccer_update` and `sensational_soccer_draw`. It builds with `-Os -ffreestanding -nostdlib -fno-jump-tables` for the 128 KiB cartridge profile: about 27 KiB of code, 50 KiB of memory and a 28 KiB AUDIO block. Everything is integer-only and deterministic. The only randomness is a seeded LCG, and per-pixel variation comes from an integer hash.

## Portable-code rule: no static pointer tables

Portable cartridges are **not relocated**. On QEMU the image runs about 0x40388000 but is linked at 0x40800000. Any initialized static pointer, such as `const char *const names[]` or a table of row pointers, therefore holds a wrong address and faults with a load access fault. All constant tables are therefore 2-D `char` arrays (sprite rows `[rows][17]`, ball frames `[4][6][7]`, ad strings `char t[16]`). Pointers are only ever formed at run time.

## World and simulation

- **Coordinates:** world positions are fixed point (`FP = 16`) on a 960 × 136 pixel pitch. The camera follows the ball, or the carrier with a velocity lead, and clamps to the stands and goals.
- **Players:** each player has position, velocity, stride phase, team, role, facing, complexion, action (none, kick, slide, dive, hold, cheer), timer, cooldown and stun. Movement is acceleration-limited steering. Players clamp to the pitch surround and push apart on contact.
- **Ball:** the ball has x/y/z, velocity, spin, owner, last touch, roll phase and a shot flag.
  - Flight: gravity and air drag apply in the air.
  - Ground: bounces are damped, and rolling friction applies.
  - Spin curls the ball sideways; aftertouch adds spin and dip for 14 frames after a human shot.
  - Possession: a carried ball inherits the carrier's velocity, so AI pursuit predicts carriers instead of chasing a stale pass velocity.
- **Phases:** `PH_KICKOFF`, `PH_PLAY`, `PH_GOAL`, `PH_RESTART` and `PH_FULLTIME`. During kick-off and restarts the ball is dead: no physics, and no pickups except by the restart taker. A 90-frame timeout hands the ball to the taker.
- **Rules:** `ball_rules` detects goals, posts and the crossbar, goal kicks, corners and throw-ins. It also detects near misses, which trigger the crowd "ooh".
- **AI:** the AI uses three roles per team, recomputed every frame: chaser, support and formation anchors. A human-controlled team never assigns its controlled player as chaser. The keeper AI handles positioning, shot prediction, dives, the hold timer and distribution.

## Screens and state

The screen machine is `SCREEN_TITLE → TEAM → MODE → MATCH → RESULT`. `go_screen` owns all music decisions:

- the title starts track 0;
- the result screen starts track 1;
- every other screen calls `music_off()`, which stops the tracker and all voices and restores full channel volume;
- `SCREEN_MATCH` also calls `prg32_audio_stop_track()` on every update.

`prg32_audio_stop_track()` alone stops only the sequencer and leaves held notes sounding, so stopping all voices is required.

## Colour model

QEMU uses a true RGB565 framebuffer. The ESP32-C6 LCD uses an 8-bit indexed framebuffer, whose RGB565 draws (text8, sprites) map through the 6×6×6 cube `16 + 36r + 6g + b` with floor quantisation. To look identical on both:

- **Sprites, text and the sprite palettes** use only exact cube colours. `c565(ci)` converts a cube index (16–231) back to the RGB565 value that maps to that index, so no colour shifts on hardware.
- **Backgrounds** (sky, stands, grass shades, lines, net, HUD panels and logo gradient) use custom palette slots 8–15 and 232–255. The RGB565 mapper never produces these slots, so reprogramming them cannot corrupt sprite or text colours.
- **Text backgrounds** use cube colours (for example `CI(0,0,1)` for HUD panels), so text cells match the panel they sit on.

## Graphics

- **Players:** a sprite is composed from head, upper-body and lower-body character maps into eight 16 × 24 frames: stand, four run phases, kick, cheer and slide (keepers dive in the arms-up pose, raised and tilted). Frames are mirrored into left- and right-facing 4-bit buffers. Each kit × complexion pair has its own 16-colour RGB565 palette.
- **Crowd:** a 320 × 28 tile with three animation frames, scrolled at parallax speed and tinted with both teams' colours.
- **Ad boards:** a 1056 × 8 strip that scrolls with the camera.
- **Run-length drawing:** the custom 5 × 7 font, the logo and the pitch markings are drawn as runs of rectangles, which keeps the draw call count low on the ESP32-C6.

## Audio design

`scripts/generate_audio.py` deterministically synthesizes every sample and writes `assets/audio.json` and `assets/sfx/*.raw`. `build.sh` packs them into the AUDIO block.

- **PCM samples** (unsigned 8-bit mono):
  - kick (11025 Hz);
  - pea whistle with a trill (11025 Hz);
  - crowd roar (5512 Hz);
  - crowd "ooh" (5512 Hz);
  - post clang (11025 Hz).

  Pitch comes from note vs. base note: base 72 for 11025 Hz samples and base 84 for 5512 Hz samples, both played at note 60.
- **Instruments:** 16 in total. Instruments 0–7 are music synths (lead, counter line, two panned pads, bass, arpeggio, snare and drum). Instruments 8–12 are the PCM effects. Instrument 13 is the ball bounce (triangle), 14 the net swish (noise) and 15 the menu blip (pulse).
- **Tracks:** track 0 is the 132 bpm title anthem (16 bars), track 1 the 120 bpm result fanfare (8 bars). One tracker tick is a sixteenth note.
- **Mix:** tracker notes play at the *channel* volume, and the mixer applies that volume twice (voice gain × channel gain).
  - Each track therefore starts with `SET_VOLUME` events of `255·√(MUSIC_GAIN·mix/255)` per channel.
  - `MUSIC_GAIN` was tuned against captured QEMU PCM. At master volume 160 the title music peaks at about 22,000 of 32,767 with no clipping.
  - `music_off()` restores channel volume 255, so match effects play at their own levels.
- **Effect channels:** ball 0, bounce 1, whistle 2, post 3, crowd left/right 4/5, net 6 and UI 7. Effects are panned by world x. Whistle blasts are queued and spaced in `sfx_update`.

## Build and Store

Both builds are portable ABI-table cartridges that require audio and multiplayer: ESP32-C6 hardware and ESP32-C3 QEMU. They receive the same metadata, colophon, icon and screenshot. `scripts/store_manifest.py` generates `manifest.json` from `metadata.json` and `colophon.json`. The result is the full metadata document (`abi: prg32-metadata-1.0`) plus `architectures`, `assets` (`icon`, `splash` = screenshot, `preview`) and the inline `colophon`. Cartridge Store intake requires exactly this shape and rebuilds each architecture's cartridge from it. `build.sh` packs the manifest with both cartridges, `colophon.json`, the icon, `assets/screenshot.png` and `assets/preview.mp4` into `dist/sensational-soccer-<version>.zip`, taking the version from the manifest. Network play uses the public PRG32 state-sharing API and room signature `sensational-soccer-v2`. The first remote peer's input drives the away team, with the AI as offline fallback.
