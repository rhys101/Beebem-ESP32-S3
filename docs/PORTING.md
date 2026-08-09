# BeebEm porting boundary

The port is derived from the pinned Linux/SDL source revision recorded in
`UPSTREAM.md`. ESP32 adaptations live in components and are connected through
an explicit host interface. The derived files retain their original copyright
headers so provenance stays visible.

## Initial Model B core

Start by extracting and compiling these BeebEm modules:

- `6502core` — CPU execution and cycle accounting.
- `beebmem` — address map, RAM, ROM paging, and memory-mapped devices.
- `via`, `sysvia`, `uservia` — timers, interrupts, keyboard, and joystick fire.
- `video` and `teletext` — CRTC/ULA and Mode 7.
- `beebsound` — SN76489 state and sample generation.
- `atodconv` — analogue joystick conversion timing.
- `disc8271` — Model B DFS controller and disc images.

The first extraction deliberately excludes desktop `main`, SDL, BeebWin,
configuration pages, AVI, debugger UI, Econet, SCSI/SASI, serial, speech, Tube,
Z80, and 80186 support.

## Host interface

Desktop dependencies currently leak into core modules through `mainWin`, SDL,
`windows.h`, `MessageBox`, file paths, and timing globals. Replace those calls
incrementally with a narrow interface:

```cpp
struct BbcHost {
    void (*video_span)(int y, int x, int count, uint8_t palette_index);
    void (*frame_complete)(uint64_t emulated_cycles);
    void (*audio_samples)(const int16_t *samples, size_t count);
    bool (*read_file)(const char *path, void *dst, size_t size);
    void (*report_error)(const char *message);
};
```

The actual interface can grow only when a reused core module demonstrates a
need. ESP-IDF, FreeRTOS, BLE, display, and board headers must never be included
from the portable core.

## Porting rules

1. Establish a host build and golden tests before moving a module to ESP-IDF.
2. Preserve BeebEm cycle semantics; optimise only after correctness tests pass.
3. Replace dynamic allocation in hot paths with fixed storage or startup-time
   allocation.
4. Do not perform FatFS, NVS, logging, or display I/O from the CPU instruction
   loop.
5. Keep original copyright notices on derived files and record each file's
   origin in the component manifest.
6. Changes useful to desktop BeebEm should remain separable from ESP32 glue.
