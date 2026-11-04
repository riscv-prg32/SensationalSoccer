# Sensational Soccer architecture

The cartridge is a portable 128 KiB PRG32 program. Its elevated sideline camera maps a fixed-point world onto a trapezoidal pitch: the entire playing area remains readable, while depth-dependent sprite scale, shadows, crowd animation, and converging field lines provide the broadcast spectacle.

All playfield fills use the indexed framebuffer and a cartridge palette. The simulation uses fixed-point integer arithmetic and RV32M multiplication/division for projection, pursuit, damping, and AI steering. Ten players run deterministic formation and possession logic; in attract mode both teams use that AI, and any input starts a fresh player-controlled match.

The AUDIO block defines eight panned SID-like instruments. Its looping stadium arrangement activates channels 0–7, while gameplay uses the percussion voices for kicks and whistles. Mono hardware receives the firmware's supported fold-down.

Builds are portable ABI-table cartridges for both ESP32-C6 hardware and ESP32-C3 QEMU, then receive identical Store metadata and are packed into one flat multi-architecture bundle.
