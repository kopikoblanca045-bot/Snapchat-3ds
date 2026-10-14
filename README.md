# FilterCam for New 3DS XL

Camera app with 8 filters, face effects (dog, cat, bunny), 3 stickers (heart, star, sunglasses) and a skin-aware beauty/smoothing mode.
Photos save as BMP to `sdmc:/FilterCam/`.

## Controls
| Button | Action |
|---|---|
| X | next filter |
| A / B | next sticker / sticker on-off |
| L / R | beauty down / up (0-5) |
| ZL / ZR | sticker smaller / bigger |
| D-Pad or touch | move sticker |
| Y | switch inner/outer camera |
| SELECT | save photo |
| Circle Pad L/R | face effect: Off / Dog / Cat / Bunny |
| START | exit |

## Face effects: how they work
The 3DS has no face-detection API, so the app finds the biggest skin-coloured region and
places ears/nose/tongue relative to it. For best results: face the camera, use good even light,
and avoid skin-toned or red/wood backgrounds. The effect hides when no face is found.
Tuning knobs are in `trackFace()` (skin threshold `cnt >= 9`, head ratio `fw * 1.3f`).

## Build
1. Install devkitPro with `3ds-dev` (`dkp-pacman -S 3ds-dev`).
2. Install `makerom` (Project_CTR) and `bannertool` and put them in PATH.
3. Run `./build_cia.sh`
4. Copy `camera.cia` to the SD card and install with FBI.

This was written without access to a compiler or a console, so it is untested.
If something fails to compile or the camera doesn't start, send me the error text.
