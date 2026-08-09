# Roadmap

Each phase leaves a flashable or host-testable artifact. Later features do not
begin until the preceding acceptance checks are repeatable.

The current private hardware build has completed the core goals through Phase
7. Phase 6 now uses one persistent writable SSD copy per game in the onboard
wear-levelled FAT partition; explicit eject and interrupted-write recovery
remain appliance-hardening work.

## Phase 0 — scaffold (current)

- Pin Linux/SDL BeebEm and document provenance.
- Create a buildable ESP-IDF shell and board-specific defaults.
- Record the architecture, port boundary, risks, and test workflow.

Acceptance: clean ESP32-S3 build and a serial boot banner with memory totals.

## Phase 1 — portable core on the Mac

- Extract the 6502, memory map, VIA, CRTC/ULA, and keyboard matrix behind host
  callbacks.
- Add a headless clock and framebuffer/audio sinks.
- Run a 6502 functional test plus deterministic MOS boot traces.

Acceptance: repeatable CPU state and framebuffer CRCs on macOS with no SDL or
Windows headers in `bbc_core`.

## Phase 2 — board diagnostic firmware

- Bring across the proven V2 CO5300/CST820 detection and 40 MHz panel setup.
- Initialise QMI8658, TF card, buttons, AXP2101, ES8311, and speaker.
- Scan and pair one BLE HID keyboard with bonding in NVS.

Acceptance: landscape colour bars/status strip, live IMU values, SD listing,
audible tone, and BLE key-usage telemetry.

## Phase 3 — first BBC boot

- Build the host-tested CPU, memory, VIA, and video core for Xtensa.
- Load user-supplied OS 1.20 and BASIC II ROMs from TF.
- Present the MOS/BASIC prompt and inject a serial test command.

Acceptance: camera-visible BASIC prompt, stable 50 Hz emulated time, and a
known boot-screen CRC matching the host build.

## Phase 4 — complete video

- Implement direct scanline-to-RGB565 scaling for modes 0-6 and Mode 7.
- Add palette flashing, cursor, interlace handling, and frame skipping.
- Tune DMA band size and place hot tables in internal SRAM.

Acceptance: visual/golden tests for every video mode, no tearing, and at least
50 emulated frames per second with performance telemetry enabled.

## Phase 5 — keyboard, touch, and motion controls

- Map BLE HID usages to the complete BBC matrix with rollover and modifiers.
- Implement BREAK safely as a reset action.
- Add configurable tilt-to-ADC and tilt-to-key profiles with calibration,
dead zones, hysteresis, and a fire gesture/button.
- Add a touch menu/virtual-key fallback for pairing and disc selection.

Acceptance: a keyboard matrix test program sees every BBC key; motion control
profiles work without chatter or stuck inputs.

## Phase 6 — DFS and disc images

- Completed: mount `.ssd` images through the 8271, select them in the graphical
  launcher, and flush completed track writes to persistent FatFS storage.
- Completed: host verification saves a file, remounts the image, and verifies
  its DFS catalogue.
- Remaining: `.dsd`, explicit eject, dirty-cache recovery after unexpected
  power loss, and import/export of user media.

Acceptance: boot a DFS disc, load/run a program, save a file, remount the image
on the Mac, and verify the catalogue and contents.

The first acceptance image was prepared as
`components/assets/discs/chuckie_egg.ssd`: mount it as drive 0 and emulate
SHIFT+BREAK to run its `!BOOT` file. Chuckie Egg remains the visual,
control-mapping, golden-frame, and disposable write-path test.

## Phase 7 — sound and final clocking

- Convert BeebEm SN76489 output to fixed-rate signed PCM.
- Stream through I2S/ES8311 with bounded latency.
- Make audio occupancy the fine-grained pacing signal and quantify underruns.

Acceptance: stable-pitch test tones and game audio for 30 minutes with no
underruns, drift, or loss of keyboard responsiveness.

## Phase 8 — appliance behaviour

- Save states, per-game profiles, recent discs, clean power-down, brightness,
battery status, and crash diagnostics.
- Add release packaging that never produces a binary-only BeebEm distribution.

Acceptance: cold boot to last disc, suspend/resume, and a two-hour soak test.

## Phase 9 — extensions

Add tape/UEF, 1770/ADFS, Master 128, speech, serial, Econet, and Tube support in
separate milestones. Each extension retains the host/device golden-test model.
