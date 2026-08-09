#!/usr/bin/env python3
"""Capture the ESP32-S3's logical display every few seconds over USB serial."""

from __future__ import annotations

import argparse
import math
import time
from datetime import datetime
from pathlib import Path

import serial
from PIL import Image, ImageDraw

FRAME_WIDTH = 640
FRAME_HEIGHT = 256
SCREEN_WIDTH = 448
SCREEN_HEIGHT = 368
VIEWPORT_HEIGHT = 336
PALETTE = tuple(
    (
        255 if index & 1 else 0,
        255 if index & 2 else 0,
        255 if index & 4 else 0,
    )
    for index in range(8)
)


def fnv1a(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def decode_rle(encoded: bytes) -> bytes:
    pixels = bytearray()
    for value in encoded:
        pixels.extend((value >> 5,) * ((value & 31) + 1))
    expected = FRAME_WIDTH * FRAME_HEIGHT
    if len(pixels) != expected:
        raise ValueError(f"decoded {len(pixels)} pixels; expected {expected}")
    return bytes(pixels)


def make_screen(indices: bytes) -> Image.Image:
    rgb = bytearray(len(indices) * 3)
    for offset, index in enumerate(indices):
        rgb[offset * 3 : offset * 3 + 3] = bytes(PALETTE[index & 7])
    source = Image.frombytes("RGB", (FRAME_WIDTH, FRAME_HEIGHT), bytes(rgb))
    viewport = source.resize((SCREEN_WIDTH, VIEWPORT_HEIGHT), Image.Resampling.NEAREST)
    screen = Image.new("RGB", (SCREEN_WIDTH, SCREEN_HEIGHT), "black")
    screen.paste(viewport, (0, (SCREEN_HEIGHT - VIEWPORT_HEIGHT) // 2))
    return screen


def read_exact(port: serial.Serial, size: int) -> bytes:
    result = bytearray()
    deadline = time.monotonic() + 20
    while len(result) < size and time.monotonic() < deadline:
        result.extend(port.read(size - len(result)))
    if len(result) != size:
        raise TimeoutError(f"received {len(result)} of {size} frame bytes")
    return bytes(result)


def wait_for_line(port: serial.Serial, prefix: bytes, timeout: float) -> bytes:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = port.readline()
        if line.startswith(prefix):
            return line.strip()
    raise TimeoutError(f"timed out waiting for {prefix.decode()}")


def start_capture(port: serial.Serial) -> None:
    deadline = time.monotonic() + 25
    while time.monotonic() < deadline:
        port.write(b"BC32_CAPTURE_START\n")
        try:
            wait_for_line(port, b"BC32_CAPTURE_READY", 1.0)
            return
        except TimeoutError:
            continue
    raise TimeoutError("board did not enable capture within 25 seconds")


def request_frame(port: serial.Serial) -> tuple[int, Image.Image]:
    port.write(b"BC32_SCREENSHOT\n")
    header = wait_for_line(port, b"BC32_FRAME ", 10).decode().split()
    if len(header) != 5:
        raise ValueError(f"invalid frame header: {' '.join(header)}")
    sequence = int(header[1])
    encoded_size = int(header[3])
    expected_hash = int(header[4], 16)
    encoded = read_exact(port, encoded_size)
    wait_for_line(port, b"BC32_END", 5)
    actual_hash = fnv1a(encoded)
    if actual_hash != expected_hash:
        raise ValueError(
            f"frame {sequence} hash mismatch: {actual_hash:08x} != {expected_hash:08x}"
        )
    return sequence, make_screen(decode_rle(encoded))


def make_contact_sheets(images: list[Path], output_dir: Path) -> None:
    columns = 4
    rows = 5
    per_sheet = columns * rows
    cell_width = 236
    cell_height = 220
    for page, first in enumerate(range(0, len(images), per_sheet), start=1):
        page_images = images[first : first + per_sheet]
        sheet = Image.new("RGB", (columns * cell_width, rows * cell_height), "#151922")
        draw = ImageDraw.Draw(sheet)
        for index, path in enumerate(page_images):
            image = Image.open(path).convert("RGB")
            image.thumbnail((224, 184), Image.Resampling.LANCZOS)
            x = (index % columns) * cell_width + 6
            y = (index // columns) * cell_height + 6
            sheet.paste(image, (x, y))
            draw.text((x, y + 188), path.name, fill="white")
        sheet.save(output_dir / f"contact-sheet-{page:03d}.jpg", quality=90)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="for example /dev/cu.usbmodem21101")
    parser.add_argument("--interval", type=float, default=1.0, help="seconds between requests")
    parser.add_argument("--duration", type=float, default=0, help="stop after N seconds; 0 waits for Ctrl-C")
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()

    started = datetime.now()
    output_dir = args.output_dir or Path("captures") / started.strftime("session-%Y%m%d-%H%M%S")
    output_dir.mkdir(parents=True, exist_ok=True)
    captured: list[Path] = []

    with serial.Serial(args.port, 115200, timeout=0.25, write_timeout=2) as port:
        start_capture(port)
        print(f"Capturing to {output_dir}; press Ctrl-C when you have visited every screen.")
        next_capture = time.monotonic()
        end_time = next_capture + args.duration if args.duration > 0 else math.inf
        try:
            while time.monotonic() < end_time:
                delay = next_capture - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
                sequence, image = request_frame(port)
                timestamp = datetime.now().strftime("%H%M%S-%f")[:-3]
                destination = output_dir / f"frame-{sequence:05d}-{timestamp}.png"
                image.save(destination, optimize=True)
                captured.append(destination)
                print(destination)
                next_capture = max(next_capture + args.interval, time.monotonic())
        except KeyboardInterrupt:
            print("\nStopping capture...")
        finally:
            port.write(b"BC32_CAPTURE_STOP\n")

    if captured:
        make_contact_sheets(captured, output_dir)
        print(f"Saved {len(captured)} PNGs and contact sheets in {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
