# Local test assets

These inputs are examples for private development and are not copied into the
public repository or a release without separate redistribution permission.

## BBC ROMs

The pinned Linux/SDL BeebEm tree supplies the initial Model B ROM set:

| File | SHA-256 |
| --- | --- |
| `data/roms/bbc/os12.rom` | `2d9fea69017864f6962704481829f95fee08446c8c3a13826d5d4e44000ac9de` |
| `data/roms/bbc/basic2.rom` | `45bd55dc0f6f0f8f1fe9e2481de7def206565eec8f600ba3068b849ca4132079` |
| `data/roms/bbc/dnfs.rom` | `e745e34895225a6650b712c1dd0656cb0b0b15f072a8ae6d9ea8d1ac257eb3d6` |

The first firmware may embed these in a private development build to remove SD
setup as a variable. The release design still loads user-provided ROMs from TF.

## Chuckie Egg SSD example

- Prepared filename: `components/assets/discs/chuckie_egg.ssd`
- Size: 204,800 bytes (single-sided 80-track DFS image).
- SHA-256: `da34e9b7b2fda70ede334f2224887130fa4ca4fb14f854141cede380f5958597`
- Catalogue title: `CHUCKIE`
- Files visible in the catalogue: `CH_EGG` and `!BOOT`.
- Boot option: 3, so SHIFT+BREAK should execute `!BOOT`.

This is the first end-to-end visual test: mount drive 0 read-only, perform
SHIFT+BREAK, observe the Chuckie Egg title/game screen through Desk View, then
exercise left/right/up/down/fire through both BLE keys and an IMU profile.
