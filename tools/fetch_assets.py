#!/usr/bin/env python3
"""Fetch reproducible local-build assets from their original archive hosts."""

from __future__ import annotations

import hashlib
import re
import subprocess
import sys
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import urljoin

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "components" / "assets"
UA = "Beebem-ESP32-S3 asset fetcher (+https://github.com/rhys101/Beebem-ESP32-S3)"


@dataclass(frozen=True)
class Game:
    page_id: int
    archive_name: str
    disc_name: str
    screenshot_name: str
    sha256: str


GAMES = (
    Game(25, "Disc002-ChuckieEgg.ssd", "chuckie_egg.ssd", "chuckie.png", "da34e9b7b2fda70ede334f2224887130fa4ca4fb14f854141cede380f5958597"),
    Game(11, "Disc001-PlanetoidAKADefender.ssd", "planetoid.ssd", "planetoid.png", "062d4ffc8966dcf23ceae6232a50cda75ced0e0425417ae0f68cdcb9229d388f"),
    Game(2449, "DiscA05-HopperSAM6.ssd", "hopper.ssd", "hopper.png", "8398a7f5bde485884b4e2079ce3608c05dbc0c9366f341e36cb8ea5ccc1fc211"),
    Game(20, "Disc002-Arcadians.ssd", "arcadians.ssd", "arcadians.png", "14908ca21fb99ad67982e07a1ce08e576536f95a947b9d4539ff753d2978b112"),
    Game(266, "Disc015-ReptonP.ssd", "repton.ssd", "repton.png", "cdd9e3d02f8565c9173251fdac16950f6e37a5963962b2999059d49c572a6845"),
    Game(432, "Disc024-Thrust.ssd", "thrust.ssd", "thrust.png", "d85b0ae2a814d632846e924de01839bd190dba316353e417af4a6b4e3cbd3d2a"),
    Game(2460, "Disc999-Zalaga-Master.ssd", "zalaga.ssd", "zalaga.jpg", "67b9bdd6b762818e67184e262bd758327c92e85c6e289c072614e8fa653af6c4"),
    Game(79, "Disc005-DareDevilDenis.ssd", "daredevil_dennis.ssd", "daredevil_dennis.png", "fb1b03e593b0db72af6bdb5de2eeb338fbd0a69c77a47a1cdccf3940eec4c217"),
    Game(96, "Disc006-Frak.ssd", "frak.ssd", "frak.png", "792ba289c77747d56d88f42003bbc7a204b763a209422c5af9353e934523d899"),
    Game(425, "Disc024-Repton3P.ssd", "repton3.ssd", "repton3.jpg", "fd139c27139e9616a32b91fbc0e81c4cbad86f7926805e834d3f5f1bc8dc8ab7"),
    Game(298, "Disc017-Repton2.ssd", "repton2.ssd", "repton2.jpg", "fbdca7d8f36329d554285f109f15cb70c6aec86ea64b8a274b2cf4f8e675b3b5"),
    Game(14, "Disc001-SnapperV2.ssd", "snapper.ssd", "snapper.png", "2f64daf772af3e562b0f2db9f13f71a13f776b24d42c2c8401845316c1488369"),
    Game(7, "Disc001-KillerGorilla.ssd", "killer_gorilla.ssd", "killer_gorilla.png", "53b451c3e4f35ee83d94f45b45ab7b4e29538450cc23c1dde7aa86129ae34a49"),
    Game(85, "Disc005-MrEe.ssd", "mr_ee.ssd", "mr_ee.png", "e7eb3b8b73a58cf8a54fb9d9792290dfdae557ca5f6482f2f6b01f7164a601ce"),
    Game(4462, "Disc181-FlappyBird.ssd", "flappy_bird.ssd", "flappy_bird.png", "eb8fa7188f31b5900816ca2cc2adb2353dd1c6aa89fe0f45f42a057c10d17d0f"),
    Game(86, "Disc005-PainterAF.ssd", "painter.ssd", "painter.png", "49b58e41539d7b6ade6ae40868880f962c87035127beea86418679e1bbc3266e"),
    Game(3535, "Disc158-SuperBreakoutJ.ssd", "super_breakout.ssd", "super_breakout.png", "ab5c52256441a6ce468fe28987343fb09c34e32830954b26d6ae9434127d5fe7"),
    Game(2558, "Disc115-BBCTetris.ssd", "bbc_tetris.ssd", "bbc_tetris.png", "1e7b8917eb1294e761b8ddc9d61919451dced357754754b2f7dce7c305c48798"),
    Game(290, "Disc017-Citadel.ssd", "citadel.ssd", "citadel.png", "7f752b5522846cb085c064f9c7dea86130d83fcfcb7552f6baf994d09d63fdd9"),
    Game(366, "Disc021-EliteD.ssd", "elite.ssd", "elite.png", "e9dbb0287c0cd00b6a289159697f27e3f81456629238c94a0c83e051d9915c1e"),
)

UPSTREAM_COMMIT = "aa2ffc1fd7fe87f2de9114caa13779d280e76a75"
UPSTREAM = "https://raw.githubusercontent.com/sjnewbury/beebem-1/" + UPSTREAM_COMMIT
UPSTREAM_ASSETS = (
    ("data/roms/bbc/os12.rom", "roms/os12.rom", "2d9fea69017864f6962704481829f95fee08446c8c3a13826d5d4e44000ac9de"),
    ("data/roms/bbc/basic2.rom", "roms/basic2.rom", "45bd55dc0f6f0f8f1fe9e2481de7def206565eec8f600ba3068b849ca4132079"),
    ("data/roms/bbc/dnfs.rom", "roms/dnfs.rom", "e745e34895225a6650b712c1dd0656cb0b0b15f072a8ae6d9ea8d1ac257eb3d6"),
    ("data/resources/teletext.fnt", "fonts/teletext.fnt", "20ef3badf213cdebfb1b4b0fcaebb4fc9e4bd3e0b96b3e1e9e7c1285b3e91f87"),
)


def get(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(request, timeout=45) as response:
        return response.read()


def checked_write(relative: str, data: bytes, expected: str) -> None:
    actual = hashlib.sha256(data).hexdigest()
    if actual != expected:
        raise RuntimeError(f"hash mismatch for {relative}: expected {expected}, got {actual}")
    destination = ASSETS / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)
    print(f"{relative}: {len(data):,} bytes, SHA-256 verified")


def main() -> int:
    print("Downloading directly from the original hosts for a local build.")
    print("Availability is not a grant to redistribute the resulting firmware.\n")
    for source, destination, digest in UPSTREAM_ASSETS:
        checked_write(destination, get(f"{UPSTREAM}/{source}"), digest)

    for game in GAMES:
        page_url = f"https://bbcmicro.co.uk/game.php?id={game.page_id}"
        page = get(page_url).decode("utf-8", "replace")
        disc_url = f"https://bbcmicro.co.uk/gameimg/discs/{game.page_id}/{game.archive_name}"
        checked_write(f"discs/{game.disc_name}", get(disc_url), game.sha256)

        match = re.search(r'["\']([^"\']*gameimg/screenshots/[^"\']+)["\']', page)
        if not match:
            raise RuntimeError(f"no screenshot link found on {page_url}")
        artwork = get(urljoin(page_url, match.group(1)))
        destination = ASSETS / "screenshots" / game.screenshot_name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(artwork)
        print(f"screenshots/{game.screenshot_name}: {len(artwork):,} bytes")

    subprocess.run([sys.executable, str(ROOT / "tools" / "prepare_screenshots.py")], check=True)
    print("\nAssets are ready for scripts/idf.sh build")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
