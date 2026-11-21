# pico-mac

A Macintosh Plus for the ClockworkPi PicoCalc with a Waveshare ESP32-P4 module. It is built on
[umac](https://github.com/evansm7/umac) and Musashi (68000), with the frontend from pico-386 /
pico-amiga. There is also an SDL2 desktop build for debugging.

## SD card

```
/picomac/macplus.rom     Mac Plus ROM v3 (128 KB, checksum 4D1F8172) -- not included
/picomac/*.dsk           boot disk images (raw .dsk/.img/.image/.hfv, or DiskCopy 4.2)
/picomac/picomac.cfg     settings (written by the app)
/picomac/pram.bin        the Mac's PRAM and clock (written by the app)
```

The ROM and System software are not included. A System 6.0.x or 7.0.x boot disk works best. The
disk image is read and written in place, so keep a copy.

## Home screen

- **Start the Mac** powers it on with the chosen boot disk. With no disk, the Mac shows the
  blinking "?" floppy.
- **Boot disk**, **ROM**: pick a file.
- **RAM**: 512 KB, 1 MB, 2 MB or 4 MB.
- **Screen**:
  - *Fit*: the 512x342 screen scaled to 320x214 in grey.
  - *1:1 pan*: a 320x320 window that follows the pointer.
  - *Native*: the ROM is patched for a 320x320 Mac screen. Some programs expect 512x342.
- **CPU speed**: 1x (a real Plus) up to 4x.
- Volume, Show FPS.

## Keys in the Mac

| PicoCalc      | Mac                                         |
|---------------|---------------------------------------------|
| Ctrl          | Command                                     |
| Sym           | Option                                      |
| Alt           | mouse button                                |
| arrows        | move the pointer (held: faster)             |
| F1            | arrows switch between mouse and Mac arrow keys |
| Esc           | ` (the Plus keyboard has no Esc)            |
| Ctrl+Esc      | the menu (screen, speed, boot disk, Reset, Quit) |

Ctrl+Shift+Up returns to the loader, and Ctrl+Shift+F3 restarts.

## Building

- P4: the "P4: Build" VS Code task (arduino-cli, `esp32:esp32:esp32p4`).
- Desktop: the "desktop: build" task, or in an MSYS2 MinGW32 shell:
  `cmake -G "Unix Makefiles" -S . -B build && make -C build`. The desktop build uses ./sdcard
  as the card.
  - `MAC_DISK=<image>` boots that image directly (`MAC_DISK=` boots with no disk).
  - `MAC_KEYS`, `MAC_SHOT` and `MAC_SD` are the test hooks (see hal_sdl.cpp).

See THIRD_PARTY.md for the licences and the changes made to umac and Musashi.
