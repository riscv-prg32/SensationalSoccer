#!/usr/bin/env python3
"""Generate the Sensational Soccer AUDIO block source (assets/audio.json).

Everything here is original and fully deterministic:

* five short unsigned 8-bit PCM effects synthesized from first principles
  (leather kick, pea whistle, crowd roar, crowd "ooh", metal post clang),
* procedural SID-like instruments for the two tracker tunes and for the
  synthesized effects (bounce, net, menu blip),
* track 0, the title anthem, and track 1, the result fanfare.

Music is only ever started on title and result screens; match play triggers
event effects on instruments 8-15 (see game.c, "Audio").
"""
import json
import math
import pathlib
import random

ROOT = pathlib.Path(__file__).resolve().parents[1]
SFX_DIR = ROOT / "assets" / "sfx"


def sid(wave, pulse, cutoff, res):
    return 0x8000 | (res << 10) | (cutoff << 6) | (pulse << 2) | wave


TRI, SAW, PULSE, NOISE = 0, 1, 2, 3

# ---------------------------------------------------------------- PCM effects


def to_u8(samples):
    peak = max(1e-9, max(abs(s) for s in samples))
    return bytes(max(0, min(255, int(round(128 + 118 * s / peak)))) for s in samples)


def one_pole(x, a):
    y, out = 0.0, []
    for v in x:
        y += a * (v - y)
        out.append(y)
    return out


def kick(rate=11025):
    rng = random.Random(1)
    n, out, phase = int(rate * 0.11), [], 0.0
    for i in range(n):
        t = i / rate
        f = 60 + 150 * math.exp(-t / 0.018)
        phase += 2 * math.pi * f / rate
        body = math.sin(phase) * math.exp(-t / 0.032)
        click = rng.uniform(-1, 1) * math.exp(-t / 0.0025) * 0.8
        out.append(body + click)
    return out


def whistle(rate=11025):
    rng = random.Random(2)
    n, out, phase = int(rate * 0.42), [], 0.0
    for i in range(n):
        t = i / rate
        rattle = math.sin(2 * math.pi * 27 * t)
        f = 2850 + 190 * rattle
        phase += 2 * math.pi * f / rate
        env = min(1.0, t / 0.015) * min(1.0, (0.42 - t) / 0.05)
        amp = 0.72 + 0.28 * rattle
        out.append(env * (amp * math.sin(phase) + 0.08 * rng.uniform(-1, 1)))
    return out


def crowd(rate, seconds, seed, voices, base, glide, attack, vowel):
    rng = random.Random(seed)
    n = int(rate * seconds)
    noise = one_pole([rng.uniform(-1, 1) for _ in range(n)], 0.35)
    noise = [a - b for a, b in zip(noise, one_pole(noise, 0.04))]
    people = []
    for _ in range(voices):
        people.append((base * rng.uniform(0.75, 1.45), rng.uniform(0, 1), rng.uniform(0.3, 1.0),
                       rng.uniform(0.0, 0.25)))
    out = []
    for i in range(n):
        t = i / rate
        env = min(1.0, t / attack) * max(0.0, 1.0 - max(0.0, t - seconds * 0.45) / (seconds * 0.55))
        bend = 1.0 - glide * t / seconds
        tone = 0.0
        for f, ph, amp, late in people:
            if t < late:
                continue
            x = (f * bend * t + ph) % 1.0
            tone += amp * (2 * x - 1)
        out.append(env * (tone / voices * 0.9 + noise[i] * 1.4))
    return one_pole(out, vowel)


def clang(rate=11025):
    n, out = int(rate * 0.35), []
    partials = [(523, 0.11, 1.0), (1371, 0.07, 0.6), (2483, 0.05, 0.45), (3610, 0.03, 0.3)]
    for i in range(n):
        t = i / rate
        out.append(sum(a * math.sin(2 * math.pi * f * t) * math.exp(-t / d) for f, d, a in partials))
    return out


SAMPLES = [
    ("kick.raw", kick(), 72),                                                     # 11025 Hz
    ("whistle.raw", whistle(), 72),                                               # 11025 Hz
    ("roar.raw", crowd(5512.5, 1.6, 3, 28, 210, 0.05, 0.14, 0.55), 84),           # 5512 Hz
    ("ooh.raw", crowd(5512.5, 0.9, 4, 22, 240, 0.22, 0.10, 0.30), 84),            # 5512 Hz
    ("clang.raw", clang(), 72),                                                   # 11025 Hz
]

# ---------------------------------------------------------------- instruments


def inst(sample, vol, pan, a, d, s, r):
    return {"sample_id": sample, "default_volume": vol, "default_pan": pan,
            "attack": a, "decay": d, "sustain": s, "release": r}


INSTRUMENTS = [
    inst(sid(PULSE, 5, 12, 1), 50, -8, 2, 60, 150, 40),    # 0 lead
    inst(sid(TRI, 8, 15, 0), 38, 28, 4, 50, 170, 50),      # 1 counter line
    inst(sid(SAW, 8, 7, 1), 22, -46, 40, 80, 150, 70),      # 2 pad left
    inst(sid(SAW, 8, 7, 1), 22, 46, 40, 80, 150, 70),       # 3 pad right
    inst(sid(PULSE, 8, 6, 2), 54, 0, 1, 40, 120, 30),      # 4 bass
    inst(sid(PULSE, 3, 11, 0), 20, 34, 1, 30, 60, 25),      # 5 arpeggio
    inst(sid(NOISE, 8, 12, 0), 38, -18, 0, 26, 0, 20),      # 6 snare
    inst(sid(TRI, 8, 10, 0), 62, 0, 0, 22, 0, 16),         # 7 drum
    inst(0, 230, 0, 0, 0, 255, 0),                          # 8 kick PCM
    inst(1, 210, 0, 0, 0, 255, 0),                          # 9 whistle PCM
    inst(2, 220, 0, 0, 0, 255, 0),                          # 10 crowd roar PCM
    inst(3, 200, 0, 0, 0, 255, 0),                          # 11 crowd ooh PCM
    inst(4, 210, 0, 0, 0, 255, 0),                          # 12 post clang PCM
    inst(sid(TRI, 8, 9, 0), 170, 0, 0, 18, 0, 12),          # 13 ball bounce
    inst(sid(NOISE, 8, 15, 0), 150, 0, 0, 60, 0, 60),       # 14 net swish
    inst(sid(PULSE, 4, 14, 0), 120, 0, 0, 30, 0, 20),       # 15 menu blip
]

# --------------------------------------------------------------------- tracks
MUSIC_GAIN = 3.3   # headroom-tuned against captured QEMU PCM (no clipping at volume 160)

# One tracker tick is a sixteenth note. Events are built on an absolute tick
# grid and converted to deltas. Instrument == channel for tracker notes.

NOTE = {n: i for i, n in enumerate(["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"])}


def midi(name):
    return 12 * (int(name[-1]) + 1) + NOTE[name[:-1]]


def build_track(tempo, bars, melody, counter, drums=True, arp=True):
    ev = []

    def add(tick, cmd, a=None, b=None, order=1):
        ev.append((tick, order, cmd, a, b))

    def play(ch, start, length, note):
        add(start, "NOTE_ON", ch, note)
        add(start + length - 1, "NOTE_OFF", ch, order=0)

    t = 0
    for (chord, mel) in zip(bars, melody):
        root, third, fifth = chord
        play(2, t, 16, third)
        play(3, t, 16, fifth)
        for step, note in enumerate((root - 24, root - 24, root - 17, root - 12)):
            play(4, t + step * 4, 3, note)
        if arp:
            for step in range(8):
                play(5, t + step * 2, 1, (root, third, fifth, root + 12)[step % 4] + 12)
        if drums:
            play(7, t, 2, 36)
            play(7, t + 8, 2, 36)
            play(6, t + 4, 2, 60)
            play(6, t + 12, 2, 60)
            if (t // 16) % 4 == 3:
                play(6, t + 14, 1, 64)
        pos = t
        for name, length in mel:
            if name != "-":
                play(0, pos, length, midi(name))
            pos += length
        assert pos - t == 16, (pos - t, mel)
        t += 16
    pos = 0
    for name, length in counter:
        if name != "-":
            play(1, pos, length, midi(name))
        pos += length
    assert pos == t, (pos, t)
    ev.sort(key=lambda e: (e[0], e[1]))
    out = [{"delta": 0, "command": "SET_TEMPO", "arg0": tempo}]
    # The tracker plays notes at the channel volume and the mixer applies that
    # volume twice (voice gain x channel gain), so the effective gain is
    # (v/255)^2. Set v = 255*sqrt(MUSIC_GAIN*mix/255) per channel; the
    # cartridge restores full channel volume when music stops.
    for ch in range(8):
        gain = min(1.0, MUSIC_GAIN * INSTRUMENTS[ch]["default_volume"] / 255)
        out.append({"delta": 0, "command": "SET_VOLUME", "arg0": ch, "arg1": round(255 * gain ** 0.5)})
    last = 0
    for tick, _, cmd, a, b in ev:
        out[-1]["delta"] += tick - last
        last = tick
        x = {"delta": 0, "command": cmd, "arg0": a}
        if b is not None:
            x["arg1"] = b
        out.append(x)
    out[-1]["delta"] += t - last
    for ch in range(8):
        out.append({"delta": 0, "command": "NOTE_OFF", "arg0": ch})
    out[-1]["delta"] = 4
    out.append({"delta": 0, "command": "JUMP", "arg0": 0, "arg1": 0})
    return {"events": out}


C, Am, F, G, Em, Dm = (60, 64, 67), (57, 60, 64), (53, 57, 60), (55, 59, 62), (52, 55, 59), (50, 53, 57)

TITLE_BARS = [C, Am, F, G, C, Em, F, G, Am, F, C, G, Am, F, G, C]
TITLE_MELODY = [
    [("E5", 4), ("G5", 2), ("E5", 2), ("C5", 4), ("G4", 4)],
    [("A4", 2), ("C5", 2), ("E5", 4), ("D5", 2), ("C5", 2), ("A4", 4)],
    [("F4", 2), ("A4", 2), ("C5", 4), ("F5", 4), ("E5", 2), ("D5", 2)],
    [("D5", 6), ("B4", 2), ("G4", 8)],
    [("E5", 2), ("G5", 2), ("C6", 4), ("B5", 2), ("A5", 2), ("G5", 4)],
    [("G5", 2), ("E5", 2), ("B4", 4), ("E5", 4), ("G5", 4)],
    [("A5", 4), ("G5", 2), ("F5", 2), ("E5", 4), ("D5", 4)],
    [("C5", 12), ("-", 4)],
    [("A5", 4), ("E5", 2), ("A5", 2), ("C6", 4), ("B5", 4)],
    [("A5", 2), ("G5", 2), ("F5", 4), ("C5", 4), ("F5", 4)],
    [("G5", 6), ("E5", 2), ("C5", 4), ("E5", 4)],
    [("D5", 2), ("E5", 2), ("F5", 2), ("G5", 2), ("A5", 4), ("B5", 4)],
    [("C6", 6), ("B5", 2), ("A5", 4), ("E5", 4)],
    [("F5", 2), ("G5", 2), ("A5", 4), ("G5", 4), ("F5", 4)],
    [("E5", 2), ("F5", 2), ("G5", 4), ("D5", 4), ("B4", 4)],
    [("C5", 8), ("G4", 4), ("C5", 4)],
]
TITLE_COUNTER = [("C4", 8), ("E4", 8), ("E4", 8), ("C4", 8), ("A3", 8), ("C4", 8), ("B3", 8), ("D4", 8),
                 ("G4", 8), ("E4", 8), ("G3", 8), ("B3", 8), ("A3", 8), ("C4", 8), ("B3", 8), ("D4", 8),
                 ("E4", 8), ("C4", 8), ("A3", 8), ("C4", 8), ("G3", 8), ("C4", 8), ("B3", 8), ("D4", 8),
                 ("E4", 8), ("A3", 8), ("C4", 8), ("F4", 8), ("D4", 8), ("B3", 8), ("C4", 8), ("E4", 8)]

RESULT_BARS = [C, F, G, C, Am, F, G, C]
RESULT_MELODY = [
    [("C5", 2), ("E5", 2), ("G5", 4), ("E5", 2), ("G5", 6)],
    [("A5", 4), ("G5", 2), ("F5", 2), ("A5", 8)],
    [("B5", 4), ("A5", 2), ("G5", 2), ("D5", 4), ("G5", 4)],
    [("C6", 12), ("-", 4)],
    [("A5", 4), ("E5", 4), ("A5", 4), ("C6", 4)],
    [("F5", 4), ("A5", 4), ("C6", 6), ("A5", 2)],
    [("G5", 2), ("A5", 2), ("B5", 4), ("D6", 4), ("B5", 4)],
    [("C6", 8), ("G5", 4), ("C6", 4)],
]
RESULT_COUNTER = [("E4", 16), ("F4", 16), ("D4", 16), ("E4", 16), ("C4", 16), ("C4", 16), ("D4", 16), ("E4", 16)]


def main():
    SFX_DIR.mkdir(parents=True, exist_ok=True)
    samples = []
    for name, data, base in SAMPLES:
        (SFX_DIR / name).write_bytes(to_u8(data))
        samples.append({"file": f"sfx/{name}", "base_note": base})
    tracks = [build_track(132, TITLE_BARS, TITLE_MELODY, TITLE_COUNTER),
              build_track(120, RESULT_BARS, RESULT_MELODY, RESULT_COUNTER, drums=True, arp=False)]
    doc = {"samples": samples, "instruments": INSTRUMENTS, "tracks": tracks}
    (ROOT / "assets" / "audio.json").write_text(json.dumps(doc, indent=1) + "\n")
    total = sum((SFX_DIR / s["file"][4:]).stat().st_size for s in samples)
    print(f"audio.json: {len(samples)} samples ({total} bytes PCM), {len(INSTRUMENTS)} instruments, "
          f"{len(tracks)} tracks ({', '.join(str(len(t['events'])) for t in tracks)} events)")


if __name__ == "__main__":
    main()
