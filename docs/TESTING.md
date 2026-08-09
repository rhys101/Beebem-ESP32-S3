# Validation workflow

## Host first

The Mac build is the correctness oracle. Tests should cover:

- 6502 instruction/flag behaviour and exact cycle counts.
- VIA timers, IRQ/NMI edges, and keyboard matrix transitions.
- MOS boot checkpoints and memory/register traces.
- Framebuffer CRCs for modes 0-7, cursor, and palette flash.
- SN76489 sample CRCs and frequency measurements.
- 8271 reads/writes against disposable disc images.
- Persistent DFS writes: issue a BBC `*SAVE`, remount the resulting SSD, and
  verify its catalogue without modifying the canonical embedded asset.
- HID usage and IMU profile mapping, especially key release paths.

## Device telemetry

Every firmware milestone prints one compact line per second containing:

- emulated cycles/s and frames/s;
- rendered/presented/dropped frames;
- audio fill, underruns, and overruns;
- display DMA time;
- BLE connection/input counts;
- internal SRAM and PSRAM minimum free sizes.

Assertions and watchdogs should report the last emulated PC, cycle count, and
active disc operation before restarting.

## Flash and camera loop

For each camera-visible milestone:

1. Build and run host golden tests.
2. Build the firmware and save its SHA-256.
3. Stop any serial monitor holding the selected `PORT`.
4. Flash and capture at least ten seconds of boot/runtime logs.
5. Use MacBook Pro Desk View to inspect the AMOLED after reset.
6. Compare visible state with the expected milestone image and serial CRC.
7. Reflash the last known-good image if the new build cannot boot reliably.

Desk View is evidence for panel state, orientation, tearing, animation, and
interaction. It does not replace CRCs or telemetry for emulator correctness.

## Long-running checks

- 30-minute Mode 7 and high-resolution graphics tests.
- Two-hour game/demo soak with BLE and audio active.
- Repeated pair/unpair, SD eject/error, reset, and low-memory tests.
- Power interruption during a dirty disc-cache test using a disposable image.
