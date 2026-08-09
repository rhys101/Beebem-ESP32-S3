# Host core test

`bc32_host` runs the same portable CPU, memory, VIA, video and 8271 code used
on the ESP32. It writes the final 640x256 palette framebuffer as a PPM image.

With the embedded-development ROM set and Chuckie Egg SSD, 1,000 fields must
complete without AddressSanitizer findings and produce:

```text
7407186b928d994c1f59133498e6e59c05ed2d157ba9a83950e89a4181b1feea
```

See the root README for the complete configure and invocation commands.

The Chuckie Egg control timing can also be exercised with
`BC32_TEST_SPACE_AT`, `BC32_TEST_START_AT`,
`BC32_TEST_START_HOLD_FIELDS`, `BC32_TEST_PLAYER_DELAY_FIELDS`, and the
`BC32_TEST_MOVE_*` variables. These verify the appliance's start timing and
the disk's configured movement keys.
