# Capturing README screenshots

The firmware exposes an opt-in USB capture protocol. It is inactive during
normal use and sends the latest logical 640×256 framebuffer only after the host
starts a capture session. The host reproduces the firmware's nearest-neighbour
4:3 scaling and centred 448×368 AMOLED canvas in PNG files.

Build and flash the current source, then start an automatic one-second session:

```sh
.venv/bin/python tools/capture_screenshots.py \
  --port /dev/cu.usbmodem21101 --interval 1
```

On Linux, use the appropriate `/dev/ttyACM*` port. Opening the port may reset
the board; the tool waits up to 25 seconds for the launcher and capture service.
Navigate through the launcher, calibration, pairing, gameplay, and shake key
selector normally. Press `Ctrl-C` when finished.

Each session is placed under `captures/session-YYYYMMDD-HHMMSS/`. It contains
timestamped lossless PNGs and contact sheets containing 20 frames each. The
whole `captures/` directory is ignored by Git. After reviewing the sheets, copy
only selected frames into `docs/images/`, give them descriptive names, and
reference those relative paths from `README.md`.

For a short unattended test, add `--duration 10`. If a complex game frame takes
longer than one second to compress and transmit at 115200 baud, the next request
starts as soon as the previous frame finishes rather than building a backlog.
