#!/usr/bin/env python3
"""Capture real Store media for Sensational Soccer from the PRG32 QEMU target.

The script boots a clean copy of the PRG32 QEMU flash with only the freshly
built QEMU cartridge in cart0, then:

* paces and records the emulator's real 22050 Hz mono UART PCM stream,
* records the QEMU SDL window with macOS ``screencapture`` (Espressif QEMU
  has no QMP ``screendump``) and crops the cartridge viewport by scale,
* injects documented UART keyboard bytes on an emulator-time schedule,
* crops the 320x200 cartridge viewport out of the firmware display and
  muxes an H.264/AAC MP4 plus a PNG screenshot.

Key events are scheduled on the PCM clock (samples received / 22050). Run it
after ``source $HOME/esp-idf/export.sh`` so the Espressif QEMU with SDL is used.
"""

from __future__ import annotations

import argparse
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import wave
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get("PRG32_REPO", HERE.parent / "PRG32")).resolve()
RATE = 22_050
AUDIO_PORT = 4421
CONSOLE_PORT = 4422

# (seconds after recording starts, UART key bytes). Keys follow the PRG32
# QEMU keyboard mapper: d=RIGHT a=LEFT w=UP s=DOWN h=A j=B Enter=SELECT.
# Timeline: title music, team select, mode select, a played match with
# dribbling, passes and shots (effects only), then the attract demo.
SCRIPT: list[tuple[float, str]] = [
    (6.0, "\r"),           # SELECT: title -> team selection (repeated:
    (6.7, "\r"),           # the team screen ignores SELECT)
    (8.0, "d"),            # home team -> TOKYO (LONDON is taken by away)
    (9.0, "s"),            # away team -> RIO
    (11.0, "h"),           # -> mode selection
    (13.0, "h"),           # single match, kick-off
]
for _i in range(43):     # 43 s of human play on the right-hand attack
    _t = 16.0 + _i
    SCRIPT.append((_t, "d"))
    SCRIPT.append((_t + 0.25, "d"))
    SCRIPT.append((_t + 0.5, "dw"[_i % 2] if _i % 3 else "d"))
    if _i % 4 == 1:
        SCRIPT.append((_t + 0.75, "j"))   # pass
    if _i % 4 == 3:
        SCRIPT.append((_t + 0.75, "h"))   # shoot
SCRIPT.sort()


class Stall(RuntimeError):
    """The emulator stopped producing audio/video before the capture finished."""


def connect(port: int, proc: subprocess.Popen, timeout: float = 20) -> socket.socket:
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if proc.poll() is not None:
            raise RuntimeError(f"QEMU exited early ({proc.returncode})")
        s = socket.socket()
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        try:
            s.connect(("127.0.0.1", port))
            return s
        except ConnectionRefusedError:
            s.close()
            time.sleep(0.1)
    raise TimeoutError(f"port {port}")


POINTER_WATCH = """import Foundation
import CoreGraphics
setvbuf(stdout, nil, _IONBF, 0)
while true {
  let p = CGEvent(source: nil)!.location
  var inside = 0
  let ws = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID)! as NSArray
  for case let w as NSDictionary in ws {
    if w[kCGWindowNumber] as? Int == QEMU_WINDOW, let b = w[kCGWindowBounds] as? NSDictionary,
       let r = CGRect(dictionaryRepresentation: b), r.insetBy(dx: -24, dy: -24).contains(p) { inside = 1 }
  }
  print(inside)
  usleep(50000)
}"""


def corner_position() -> str:
    """Bottom-right corner of the main display, away from where people work."""
    src = "import CoreGraphics\nlet b = CGDisplayBounds(CGMainDisplayID())\nprint(Int(b.width), Int(b.height))"
    try:
        w, h = (int(v) for v in subprocess.run(["/usr/bin/swift", "-e", src], capture_output=True,
                                                text=True, check=True).stdout.split())
        return f"{w - 340},{h - 300}"
    except (subprocess.CalledProcessError, ValueError):
        return "0,0"


def find_window(proc: subprocess.Popen, timeout: float = 20) -> str:
    """CoreGraphics window number of the QEMU SDL window owned by proc."""
    src = f"""import Foundation
import CoreGraphics
let ws = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID)! as NSArray
for case let w as NSDictionary in ws {{
  if w[kCGWindowOwnerPID] as? Int == {proc.pid}, let n = w[kCGWindowNumber] as? Int {{ print(n); break }}
}}"""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        out = subprocess.run(["/usr/bin/swift", "-e", src], capture_output=True, text=True).stdout.strip()
        if out:
            return out
        time.sleep(0.2)
    raise TimeoutError("QEMU SDL window not found")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cart", type=Path, default=HERE / "dist/sensational-soccer-qemu.prg32")
    ap.add_argument("--duration", type=float, default=60.0)
    ap.add_argument("--warmup", type=float, default=8.0)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--screenshot-at", type=float, default=30.0)
    ap.add_argument("--preview", type=Path, default=HERE / "assets/preview.mp4")
    ap.add_argument("--screenshot", type=Path, default=HERE / "assets/screenshot.png")
    ap.add_argument("--keep", type=Path, help="keep raw video, audio and console log here")
    ap.add_argument("--attempts", type=int, default=4,
                    help="retry a run whose emulator stalls (QEMU firmware can freeze while its "
                         "multiplayer client reconnects to an unreachable room server)")
    ap.add_argument("--allow-pointer", action="store_true",
                    help="accept takes with the host pointer over the window (verification runs only, "
                         "never for Store media)")
    ap.add_argument("--keys", help="override the input script, e.g. '6:h,8:d,9.5:l'")
    args = ap.parse_args()
    if args.keys:
        SCRIPT[:] = sorted((float(t), k) for t, k in (item.split(":") for item in args.keys.split(",")))
    for attempt in range(1, args.attempts + 1):
        try:
            capture(args)
            return
        except Stall as exc:
            print(f"attempt {attempt}/{args.attempts}: {exc}", file=sys.stderr)
    raise SystemExit("capture failed: emulator stalled on every attempt")


def capture(args: argparse.Namespace) -> None:
    ffmpeg = shutil.which("ffmpeg") or sys.exit("ffmpeg is required")

    with tempfile.TemporaryDirectory(prefix="soccer-capture-") as tmp_name:
        tmp = Path(args.keep) if args.keep else Path(tmp_name)
        tmp.mkdir(parents=True, exist_ok=True)
        flash, efuse = tmp / "flash.bin", tmp / "efuse.bin"
        shutil.copy2(SDK / "build-qemu/qemu_flash.bin", flash)
        shutil.copy2(SDK / "build-qemu/qemu_efuse.bin", efuse)
        subprocess.run([sys.executable, "-m", "prg32", "qemu", "upload", str(args.cart.resolve()),
                        "--flash", str(flash)], cwd=SDK, check=True)
        cmd = ["qemu-system-riscv32", "-M", "esp32c3", "-m", "4M",
               "-drive", f"file={flash},if=mtd,format=raw",
               "-drive", f"file={efuse},if=none,format=raw,id=efuse",
               "-global", "driver=nvram.esp32c3.efuse,property=drive,value=efuse",
               "-global", "driver=timer.esp32c3.timg,property=wdt_disable,value=true",
               "-nic", "user,model=open_eth",   # required: the firmware aborts without its NIC
               "-display", "sdl,show-cursor=off", "-monitor", "none",
               "-serial", f"tcp::{CONSOLE_PORT},server=on,wait=off,nodelay=on",
               "-serial", f"tcp::{AUDIO_PORT},server=on,wait=on,nodelay=on"]
        env = dict(os.environ, SDL_VIDEO_WINDOW_POS=os.environ.get("SOCCER_WINDOW_POS") or corner_position())
        proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=(tmp / "qemu.log").open("wb"), env=env)
        pcm = bytearray()
        total = round(args.duration * RATE) * 2
        recording, done = threading.Event(), threading.Event()

        def audio() -> None:
            s = connect(AUDIO_PORT, proc)
            s.sendall(b"K")
            nxt = time.monotonic() + 0.02
            while proc.poll() is None and len(pcm) < total:
                chunk = bytearray()
                while len(chunk) < 882:
                    part = s.recv(882 - len(chunk))
                    if not part:
                        done.set()
                        return
                    chunk.extend(part)
                if recording.is_set():
                    pcm.extend(chunk[:total - len(pcm)])
                delay = nxt - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
                nxt += 0.02
                s.sendall(b"K")
            done.set()

        th = threading.Thread(target=audio, daemon=True)
        th.start()
        console = connect(CONSOLE_PORT, proc)
        log = bytearray()

        def reader() -> None:
            console.settimeout(0.2)
            while proc.poll() is None:
                try:
                    data = console.recv(4096)
                except (TimeoutError, OSError):
                    continue
                if not data:
                    return
                log.extend(data)

        threading.Thread(target=reader, daemon=True).start()
        raw = tmp / "window.mov"
        raw.unlink(missing_ok=True)   # screencapture refuses to overwrite
        pointer = None
        try:
            window = find_window(proc)
            # Firmware boots to PRG32 SETUP: RUN CARTRIDGE -> cart0.
            # Wait for the PRG32 SETUP menu (boot time varies with host load),
            # then RUN CARTRIDGE -> cart0 until the cartridge reports loading.
            end = time.monotonic() + 60
            while b"QEMU keyboard input enabled" not in log:
                if time.monotonic() > end:
                    raise Stall("firmware did not reach the setup menu")
                time.sleep(0.1)
            time.sleep(args.warmup / 2)
            for _ in range(4):
                for key in "hh":   # menu waits for release between presses
                    for _ in range(3):
                        console.sendall(key.encode())
                        time.sleep(0.03)
                    time.sleep(2.0)
                if b"loaded cartridge" in log:
                    break
            else:
                raise Stall("cartridge did not start")
            time.sleep(args.warmup / 2)
            # screencapture video always includes the host pointer: watch it and
            # reject (retry) a take where it crosses the QEMU window.
            pointer = subprocess.Popen(["/usr/bin/swift", "-e", POINTER_WATCH.replace("QEMU_WINDOW", window)],
                                       stdout=subprocess.PIPE, text=True)
            samples: list[str] = []
            threading.Thread(target=lambda: samples.extend(pointer.stdout), daemon=True).start()
            recording.set()
            # Record until the emulator has produced `duration` seconds of PCM.
            # QEMU may run slightly slower than wall time while the window is
            # recorded, so the video is later retimed onto the PCM clock.
            started = time.monotonic()
            video = subprocess.Popen(["/usr/sbin/screencapture", "-v", f"-V{args.duration * 1.12 + 3:g}",
                                      "-x", "-o", f"-l{window}", str(raw)])
            event, wall = 0, None
            while video.poll() is None:
                if wall is None and done.is_set():
                    wall = time.monotonic() - started
                t = len(pcm) / 2 / RATE
                while event < len(SCRIPT) and SCRIPT[event][0] <= t:
                    for _ in range(3):  # ~90 ms pulse crosses an update boundary
                        console.sendall(SCRIPT[event][1].encode())
                        time.sleep(0.03)
                    event += 1
                time.sleep(0.01)
            if wall is None:
                wall = time.monotonic() - started
            pointer.terminate()
            if video.returncode != 0 or not raw.exists():
                raise SystemExit(f"screencapture failed ({video.returncode})")
            if len(samples) < args.duration * 5:
                raise SystemExit("pointer watcher did not run (swift CoreGraphics script failed)")
            if not args.allow_pointer and any(s.startswith("1") for s in samples):
                raise Stall("host pointer crossed the QEMU window during recording")
        finally:
            if pointer:
                pointer.terminate()
            console.close()
            proc.terminate()
            proc.wait(timeout=10)
            (tmp / "console.log").write_bytes(bytes(log))
        if len(pcm) < total * 0.98:
            raise Stall(f"PCM capture short: {len(pcm) / 2 / RATE:.2f}s (emulator stalled)")
        wav = tmp / "audio.wav"
        with wave.open(str(wav), "wb") as w:
            w.setparams((1, 2, RATE, len(pcm) // 2, "NONE", "not compressed"))
            w.writeframes(bytes(pcm))
        # The SDL window is the macOS title bar above the 320x240 firmware
        # panel, whose 20-line top and bottom status bands surround the
        # 320x200 cartridge viewport. Window width == 320 * scale.
        probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                                "stream=width,height", "-of", "csv=p=0", str(raw)],
                               capture_output=True, text=True, check=True).stdout.strip()
        width, height = (int(v) for v in probe.split(","))
        scale = width // 320
        if width % 320 or scale < 2:
            raise Stall(f"QEMU window is {width}x{height}, expected a 2x or larger 320-wide panel")
        top = int(os.environ.get("SOCCER_VIEWPORT_TOP", "-1"))
        if top < 0:
            top = height - scale * 240 + scale * 20
        ratio = (len(pcm) / 2 / RATE) / wall
        crop = (f"setpts=PTS*{ratio:.6f},crop={scale * 320}:{scale * 200}:0:{top},"
                "scale=320:200:flags=neighbor")
        print(f"raw {width}x{height} scale {scale} viewport top {top}; emulator/wall speed {ratio:.4f}")
        subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-i", str(raw), "-i", str(wav),
                        "-t", str(args.duration), "-vf", crop, "-r", str(args.fps), "-af", "apad",
                        "-c:v", "libx264", "-preset", "slow", "-crf", "16", "-pix_fmt", "yuv420p",
                        "-c:a", "aac", "-b:a", "128k", "-movflags", "+faststart", str(args.preview)],
                       check=True)
        still = f"crop={scale * 320}:{scale * 200}:0:{top},scale=320:200:flags=neighbor"
        subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-ss", f"{args.screenshot_at / ratio:.3f}",
                        "-i", str(raw), "-frames:v", "1", "-vf", still, str(args.screenshot)], check=True)
        print(f"preview {args.preview} ({len(pcm) / 2 / RATE:.2f}s PCM), screenshot {args.screenshot}")


if __name__ == "__main__":
    main()
