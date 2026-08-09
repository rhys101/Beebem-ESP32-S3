# Architecture

## Product target

The first complete target is an accurate BBC Micro Model B:

- 2 MHz 6502A, 32 KiB RAM, MOS 1.20 and sideways ROMs.
- 6845 CRTC and Video ULA, including bitmap modes 0-6 and Mode 7 teletext.
- System and User 6522 VIAs, keyboard matrix, ADC joystick, and fire button.
- SN76489 sound, 8271 DFS, and `.ssd`/`.dsd` images on the TF card.
- Full BLE HID keyboard, configurable IMU controls, touch overlay, and buttons.

Master 128, 1770/ADFS, Tube processors, Econet, serial, speech, and tape are
extensions after the Model B meets its accuracy and frame-pacing targets.

## Display mapping

The panel is 368x448 portrait. The emulator rotates it to 448x368 landscape
and aspect-fits a centred 448x336 4:3 BBC viewport, leaving equal 16-pixel
letterbox bars. This preserves the intended CRT geometry: the 640x256 backing
pixels are non-square and must not be fitted as a raw 5:2 image.

`bbc_core` renders palette indices into two complete 640x256 buffers in PSRAM.
The app snapshots the completed buffer and converts it directly into 28-line
RGB565 DMA bands, keeping source reads contiguous. Mode 7's 500-source-line
raster is half-sampled into 250 stored rows, so it also fits without a second,
larger framebuffer. Bitmap modes discard BeebEm's 32-line vertical-blank
interval before storage, matching its 640x512 desktop presentation without
storing scan-doubled rows. The panel stays at Waveshare's reliable 40 MHz QSPI
clock.

## Software layers

```text
app
 |-- bbc_core: 6502, memory, CRTC/ULA, VIA, ADC, sound, DFS
 |       `-- host callbacks: video, audio, files, clock, log
 |-- input: BLE HID + IMU + touch/buttons -> BBC keyboard/joystick state
 |-- board: CO5300, CST820, QMI8658, TF, ES8311, AXP2101
 `-- platform: ESP-IDF tasks, queues, timers, NVS and FatFS
```

The emulator core owns all BBC machine state. Other tasks submit timestamped
input events or consume immutable audio/video buffers; they never mutate VIA,
memory, or CPU state directly.

## Task and core allocation

- Core 1, high priority: emulator task. Runs cycle-budgeted chunks and is the
  sole owner of machine state.
- Core 0: NimBLE HID host, storage requests, touch/IMU sampling, and system work.
- Display DMA: consumes 8-16-line RGB565 bands through a small queue.
- Audio task on Core 0: synthesises SN76489 blocks and streams signed 16-bit
  mono through I2S/ES8311.

Pacing uses a fixed epoch at 50 BBC fields/s. A PAL field is 40,000 CPU cycles.
The display presents alternate completed fields when necessary, while the CPU,
peripherals and audio continue without skipping emulated time.

## Memory budget

Internal SRAM is reserved for latency-sensitive state and DMA:

- BBC RAM, CPU/VIA/CRTC state, and hot lookup tables.
- Two display DMA bands (approximately 14-28 KiB total).
- Audio ring and Bluetooth/FreeRTOS working memory.

PSRAM holds larger, colder allocations:

- Two 640x256 8-bit BBC framebuffers plus one stable display snapshot (160 KiB
  each), ROM images, teletext data and the current read-only SSD image.

The initial renderer should be banded and need no full framebuffer. A full
PSRAM frame is allowed for diagnostics and UI composition, but not required by
the emulation loop.

The 16 MB flash uses a 6 MB factory application partition, leaving roughly
9.9 MB as a local FAT data partition. TF remains the normal ROM/disc store; the
internal data partition is available for configuration, recovery assets, or a
small private development image without constraining the application binary.

## Input routing

BLE keyboard reports are decoded by usage code, not host character, and mapped
to the BBC keyboard matrix. The map includes BBC-specific SHIFT, CTRL, CAPS
LOCK, SHIFT LOCK, COPY, BREAK, arrows, and function keys. Pairing and bonds are
stored in NVS. ESP32-S3 supports BLE only, so the keyboard must support BLE
HID/HOGP; a Bluetooth Classic-only keyboard will not connect.

QMI8658 samples are filtered and calibrated at startup. Per-game profiles can
route pitch/roll to either:

- continuous 16-bit ADC joystick X/Y values, plus a fire gesture/button; or
- digital BBC keys with configurable dead zone, hysteresis, and repeat policy.

Profiles are currently compiled into the game table and may assign tilt,
touch, PWR, and BOOT inputs independently. All motion-driven key releases are
explicit so no key can remain stuck when a profile changes.

## Storage and ROMs

Build-time asset layout:

```text
components/assets/roms/{os12,basic2,dnfs}.rom
components/assets/discs/*.ssd
components/assets/screenshots/*.rle
components/assets/fonts/teletext.fnt
```

The private development image embeds these user-supplied files. On first game
launch the selected SSD is copied to a writable image in the 10 MiB
wear-levelled FAT partition. Disc writes use a bounded cache and completed
track writes are flushed to that copy. Installed inputs and the output image
are Git-ignored and must not be distributed without the relevant permissions.
