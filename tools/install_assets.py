#!/usr/bin/env python3
"""Install legally obtained BeebEm-ESP32-S3 build assets from a prepared tree."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "components" / "assets"
REQUIRED = {
    "roms": ("os12.rom", "basic2.rom", "dnfs.rom"),
    "fonts": ("teletext.fnt",),
    "discs": (
        "chuckie_egg.ssd", "planetoid.ssd", "hopper.ssd", "arcadians.ssd",
        "repton.ssd", "thrust.ssd", "zalaga.ssd", "daredevil_dennis.ssd",
        "frak.ssd", "repton3.ssd", "repton2.ssd", "snapper.ssd",
        "killer_gorilla.ssd", "mr_ee.ssd", "flappy_bird.ssd", "painter.ssd",
        "super_breakout.ssd", "bbc_tetris.ssd", "citadel.ssd", "elite.ssd",
    ),
    "screenshots": tuple(
        f"{name}.rle" for name in (
            "chuckie", "planetoid", "hopper", "arcadians", "repton", "thrust",
            "zalaga", "daredevil_dennis", "frak", "repton3", "repton2",
            "snapper", "killer_gorilla", "mr_ee", "flappy_bird", "painter",
            "super_breakout", "bbc_tetris", "citadel", "elite",
        )
    ),
}


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Copy a prepared assets/{roms,fonts,discs,screenshots} tree."
    )
    parser.add_argument("--from", dest="source", required=True, type=Path)
    parser.add_argument("--check", action="store_true", help="validate without copying")
    args = parser.parse_args()

    missing: list[Path] = []
    for directory, names in REQUIRED.items():
        for name in names:
            source = args.source / directory / name
            if not source.is_file():
                missing.append(source)
    if missing:
        print("Missing required prepared assets:")
        for path in missing:
            print(f"  {path}")
        return 1

    if not args.check:
        for directory, names in REQUIRED.items():
            destination = ASSETS / directory
            destination.mkdir(parents=True, exist_ok=True)
            for name in names:
                shutil.copy2(args.source / directory / name, destination / name)
        print(f"Installed {sum(map(len, REQUIRED.values()))} assets into {ASSETS}")
    else:
        print("Asset tree is complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
