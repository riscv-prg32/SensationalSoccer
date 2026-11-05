# Sensational Soccer contributor guide

This repository contains one portable PRG32 cartridge. Keep gameplay deterministic, integer-only, original, and compatible with both QEMU and ESP32-C6. Do not copy names, code, graphics, audio, maps, or presentation from commercial football games.

Before changing gameplay, read `README.md`, `docs/architecture.md`, and `docs/reproducibility.md`. Preserve the 320×200 viewport, 128 KiB cartridge profile, indexed palette, offline AI fallback, and multiplayer room compatibility unless a versioned migration is documented.

Every behavior change requires documentation parity. Build with `./build.sh`, run `git diff --check`, validate JSON metadata, test QEMU with audio and input, and upload the final ESP32-C6 artifact to an explicitly named slot. Report any overwritten slot. Store media must be real QEMU output: a minimum 320×200 screenshot and a 60-second H.264/AAC preview with captured emulator PCM. Never fabricate test results.

Audio policy: music is allowed only on title, result, game-over, and winning screens. Match play uses event effects only. The regular publication volume is 160. Builds or demonstrations run during Central European night hours must temporarily set master volume to zero and restore the publication value afterward.

Generated publication artifacts are `dist/*.prg32`, `dist/*.zip`, `assets/screenshot.png`, and `assets/preview.mp4`. Keep metadata, colophon, manifest, documentation, and packaged artifacts synchronized. Commit focused changes and push only after both target tests pass.
