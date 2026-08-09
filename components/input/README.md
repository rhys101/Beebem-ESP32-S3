# input

BLE HID keyboard, QMI8658 motion profiles, touch, and physical-button events.
This layer emits neutral BBC key/joystick events and does not access emulator
state directly.

The PWR button uses the AXP2101's latched short-press event so it works from
battery as well as USB power. EXIO4 remains a fallback if the PMU is unavailable;
BOOT is read directly from GPIO0.
