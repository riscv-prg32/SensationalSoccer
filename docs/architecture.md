# Sensational Soccer architecture

The cartridge is a portable 128 KiB PRG32 program. Its touchline camera follows the ball or carrier across a 960 by 136 pixel fixed-point world. Rendering clips world-space grass stripes, markings, goals, players, ball, and a slower parallax grandstand into the 320 by 200 viewport. All player and spectator artwork is original procedural indexed graphics.

The fixed-point simulation models acceleration, drag, player-to-player separation, ball height, gravity, damped bounces, wall rebounds, rolling friction, and lateral spin. Twelve players use formation, pursuit, passing, possession, and shooting logic. In attract mode both teams use AI. During play the local controller owns blue; the first multiplayer peer owns red, with an AI fallback whenever no peer is visible.

The screen state machine separates title music from silent-transition menus and match effects. Player action state drives close-control, kick, walk, and keeper-dive poses without bitmap assets or copied artwork. Team selection changes palette colors and labels while leaving collision and simulation rules identical.

The AUDIO block defines eight panned SID-like instruments. Its looping stadium arrangement activates channels 0–7, while gameplay uses separately panned, power-sensitive kicks and a whistle voice. Mono hardware receives the firmware's supported fold-down. The regular release initializes master volume to 160; night tests temporarily use zero.

Builds are portable ABI-table cartridges requiring audio and multiplayer for both ESP32-C6 hardware and ESP32-C3 QEMU. They receive identical Store metadata and are packed into one flat multi-architecture bundle. Network play uses the public PRG32 state-sharing API and room signature `sensational-soccer-v2`.
