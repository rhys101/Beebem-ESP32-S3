#!/usr/bin/env python3
"""Convert launcher PNG/JPEG screenshots to the compact 3-bit RLE format."""

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SCREENSHOTS = ROOT / "components" / "assets" / "screenshots"
SOURCES = {
    "chuckie": "chuckie.png",
    "planetoid": "planetoid.png",
    "hopper": "hopper.png",
    "arcadians": "arcadians.png",
    "repton": "repton.png",
    "thrust": "thrust.png",
    "zalaga": "zalaga.jpg",
    "daredevil_dennis": "daredevil_dennis.png",
    "frak": "frak.png",
    "repton3": "repton3.jpg",
    "repton2": "repton2.jpg",
    "snapper": "snapper.png",
    "killer_gorilla": "killer_gorilla.png",
    "mr_ee": "mr_ee.png",
    "flappy_bird": "flappy_bird.png",
    "painter": "painter.png",
    "super_breakout": "super_breakout.png",
    "bbc_tetris": "bbc_tetris.png",
    "citadel": "citadel.png",
    "elite": "elite.png",
}
WIDTH = 640
HEIGHT = 256


def palette_index(pixel: tuple[int, int, int]) -> int:
    red, green, blue = pixel
    return (1 if red >= 128 else 0) | (2 if green >= 128 else 0) | (
        4 if blue >= 128 else 0
    )


def encode(name: str, source_name: str) -> None:
    source = Image.open(SCREENSHOTS / source_name).convert("RGB")
    image = source.resize((WIDTH, HEIGHT), Image.Resampling.NEAREST)
    pixels = [palette_index(pixel) for pixel in image.getdata()]

    encoded = bytearray()
    offset = 0
    while offset < len(pixels):
        colour = pixels[offset]
        run = 1
        while (
            run < 32
            and offset + run < len(pixels)
            and pixels[offset + run] == colour
        ):
            run += 1
        encoded.append((colour << 5) | (run - 1))
        offset += run

    destination = SCREENSHOTS / f"{name}.rle"
    destination.write_bytes(encoded)
    print(f"{name}: {source.width}x{source.height} -> {len(encoded)} bytes")


for screenshot_name, filename in SOURCES.items():
    encode(screenshot_name, filename)
