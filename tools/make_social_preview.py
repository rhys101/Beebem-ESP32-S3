#!/usr/bin/env python3
"""Build the 640x320 transparent hardware social-preview image."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageEnhance


CANVAS_SIZE = (640, 320)
# Display opening in the unrotated 800x800 Waveshare product photograph.
PORTRAIT_DISPLAY_BOX = (268, 250, 532, 573)


def silhouette_alpha(photo: Image.Image) -> Image.Image:
    """Extract the dark enclosure from the near-white catalogue background."""
    difference = ImageChops.difference(photo, Image.new("RGB", photo.size, "white"))
    difference = difference.convert("L")
    # Retain antialiased product edges while removing JPEG noise in the white.
    return difference.point(lambda value: max(0, min(255, (value - 3) * 18)))


def rounded_mask(size: tuple[int, int], radius: int) -> Image.Image:
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size[0] - 1, size[1] - 1), radius, fill=255)
    return mask


def build_preview(photo_path: Path, screen_path: Path) -> Image.Image:
    photo_rgb = Image.open(photo_path).convert("RGB")
    if photo_rgb.size != (800, 800):
        raise ValueError(f"expected an 800x800 product photo, got {photo_rgb.size}")

    photo = photo_rgb.convert("RGBA")
    photo.putalpha(silhouette_alpha(photo_rgb))
    rotated = photo.transpose(Image.Transpose.ROTATE_270)

    left, top, right, bottom = PORTRAIT_DISPLAY_BOX
    display_box = (photo.height - bottom, left, photo.height - top, right)
    display_size = (display_box[2] - display_box[0], display_box[3] - display_box[1])

    screen = Image.open(screen_path).convert("RGB")
    screen = ImageEnhance.Brightness(screen).enhance(0.92)
    screen = screen.resize(display_size, Image.Resampling.LANCZOS).convert("RGBA")
    display_mask = rounded_mask(display_size, radius=38)
    screen.putalpha(display_mask)
    rotated.alpha_composite(screen, (display_box[0], display_box[1]))

    # Reapply only the bright glass highlights from the real photograph. This
    # keeps its photographed material without obscuring the framebuffer.
    glass = photo_rgb.transpose(Image.Transpose.ROTATE_270).crop(display_box).convert("RGBA")
    luminance = glass.convert("L")
    highlight = luminance.point(lambda value: max(0, min(105, (value - 35) * 2)))
    highlight = ImageChops.multiply(highlight, display_mask)
    glass.putalpha(highlight)
    rotated.alpha_composite(glass, (display_box[0], display_box[1]))

    bounds = rotated.getchannel("A").point(lambda value: 255 if value > 8 else 0).getbbox()
    if bounds is None:
        raise ValueError("product silhouette is empty")
    device = rotated.crop(bounds)

    max_size = (CANVAS_SIZE[0] - 32, CANVAS_SIZE[1] - 20)
    scale = min(max_size[0] / device.width, max_size[1] / device.height)
    device = device.resize(
        (round(device.width * scale), round(device.height * scale)),
        Image.Resampling.LANCZOS,
    )

    result = Image.new("RGBA", CANVAS_SIZE, (0, 0, 0, 0))
    result.alpha_composite(
        device,
        ((CANVAS_SIZE[0] - device.width) // 2, (CANVAS_SIZE[1] - device.height) // 2),
    )
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--photo", type=Path, required=True)
    parser.add_argument("--screen", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    build_preview(args.photo, args.screen).save(args.output, optimize=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
