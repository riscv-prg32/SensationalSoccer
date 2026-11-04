#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
sdk="${PRG32_REPO:-$here/../PRG32}"
mkdir -p "$here/build" "$here/dist" "$here/store-bundle"
python3 "$here/scripts/generate_audio.py"
python3 "$sdk/tools/prg32audio_pack.py" "$here/assets/audio.json" --out "$here/build/soccer.audio"
for arch in esp32c6 qemu; do
  (cd "$sdk" && python3 -m prg32 cartridge build "$here/game.c" --portable --entry-prefix sensational_soccer --name "Sensational Soccer" --architecture "$arch" --cart-ram-kib 128 --required-feature audio --build-dir "$here/build/$arch" --out "$here/build/sensational-soccer-$arch-core.prg32" --audio-block "$here/build/soccer.audio")
  (cd "$sdk" && python3 -m prg32 store attach-metadata "$here/build/sensational-soccer-$arch-core.prg32" --metadata "$here/metadata.json" --colophon "$here/colophon.json" --architecture "$arch" --icon "$here/assets/icon.png" --screenshot "$here/assets/screenshot.png" --out "$here/dist/sensational-soccer-$arch.prg32")
  cp "$here/dist/sensational-soccer-$arch.prg32" "$here/store-bundle/"
done
cp "$here/manifest.json" "$here/colophon.json" "$here/assets/icon.png" "$here/assets/screenshot.png" "$here/assets/preview.mp4" "$here/store-bundle/"
(cd "$sdk" && python3 -m prg32 store pack-bundle --manifest "$here/store-bundle/manifest.json" --out "$here/dist/sensational-soccer-1.0.0.zip")
