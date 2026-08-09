# Supplying ROMs, discs, and launcher images

The source repository intentionally contains no BBC ROM, game disc, saved-game,
or third-party screenshot files. You must obtain these files lawfully and may
only build or distribute an image when you have the necessary rights.

For a local build, the verified fetcher downloads the selected game discs and
screenshots directly from BBCMicro.co.uk and the ROM/font files from the pinned
BeebEm source revision:

```sh
.venv/bin/python tools/fetch_assets.py
```

BBCMicro.co.uk says its restored games are provided for play in emulators and
on real BBC Micro hardware. It does not publish a blanket third-party mirroring
licence; direct local download therefore does not imply permission to upload a
media-containing firmware image elsewhere.

Alternatively, prepare a directory with this layout:

```text
my-assets/
├── roms/          os12.rom, basic2.rom, dnfs.rom
├── fonts/         teletext.fnt
├── discs/         chuckie_egg.ssd, planetoid.ssd, ... elite.ssd
└── screenshots/   chuckie.rle, planetoid.rle, ... elite.rle
```

The complete normalized filename list is authoritative in
`tools/install_assets.py`. Install and validate it with:

```sh
python3 tools/install_assets.py --from /path/to/my-assets
python3 tools/install_assets.py --from /path/to/my-assets --check
```

`tools/prepare_screenshots.py` converts source PNG/JPEG artwork into the compact
3-bit RLE format expected by the launcher. Run `python3
tools/prepare_screenshots.py --help` for its input and output options.

These installed files are ignored by Git. Never use `git add -f` on them unless
you have separately verified that redistribution is permitted.
