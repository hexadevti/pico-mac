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
/picomac/snapshots/      saved snapshots (written by the app)
```

The ROM and System software are not included. A System 6.0.x or 7.0.x boot disk works best. The
disk image is read and written in place, so keep a copy.

## Home screen

- **Start the Mac** powers it on with the chosen boot disk. With no disk, the Mac shows the
  blinking "?" floppy.
- **Boot disk**, **ROM**: pick a file. Disk images: raw (.dsk .img .image .hfv .hfs) or
  DiskCopy 4.2 (.dc42 .diskcopy).
- **Second disk**: a second drive, mounted on the desktop once the Mac has booted. Use it for
  a game or program disk that has no System, while the boot disk holds the System.
- **RAM**: 512 KB, 1 MB, 2 MB or 4 MB.
- **Screen**:
  - *Fit*: the 512x342 screen scaled to 320x214 in grey.
  - *1:1 pan*: a 320x320 window that follows the pointer.
  - *Native*: the ROM is patched for a 320x320 Mac screen. Some programs expect 512x342.
- **CPU speed**: 1x (a real Plus) up to 4x.
- **Load snapshot**: start from a snapshot (see below).
- Volume, Show FPS.

## Snapshots

As in pico-386: the in-game menu (Ctrl+Esc) has **Save snapshot** and **Load snapshot** right
below Resume.
- Save: pick "New snapshot" or one to overwrite, then type a name.
- The list shows each snapshot's screen. Enter on one: Start from it, Delete or Back.
- A snapshot holds the whole Mac (CPU, RAM, devices) plus which disks, ROM, RAM size and screen
  mode it used. Starting from one brings those back as the settings.
- The Mac keeps parts of its disks in RAM. When a disk was written after the snapshot was
  taken, the list says so: starting from it may damage that disk.
- Files: /picomac/snapshots/<p4|desktop>/sN.snp, with sN.bmp as the picture. Each build reads
  only its own.

## Keys in the Mac

| PicoCalc      | Mac                                         |
|---------------|---------------------------------------------|
| Ctrl          | Command                                     |
| Sym           | Option                                      |
| Alt           | mouse button                                |
| arrows        | move the pointer (held: faster)             |
| both Shifts, Ctrl+Tab (or F1) | arrows: mouse, Mac arrow keys, or both |
| Ctrl+Caps Lock | zoom: Fit <-> 1:1 pan                      |
| Esc           | ` (the Plus keyboard has no Esc)            |
| Ctrl+Esc      | the menu (screen, speed, disks, Reset, Quit) |

Ctrl+Shift+Up returns to the loader, and Ctrl+Shift+F3 restarts.

## Building

- P4: the "P4: Build" VS Code task (arduino-cli, `esp32:esp32:esp32p4`).
- Desktop: the "desktop: build" task, or in an MSYS2 MinGW32 shell:
  `cmake -G "Unix Makefiles" -S . -B build && make -C build`. The desktop build uses ./sdcard
  as the card.
  - `MAC_DISK=<image>` boots that image directly (`MAC_DISK=` boots with no disk). `MAC_DISK2=<image>` is the second disk.
  - `MAC_SNAP=<sN.snp>` starts from that snapshot.
  - `MAC_KEYS`, `MAC_SHOT` and `MAC_SD` are the test hooks (see hal_sdl.cpp).

See THIRD_PARTY.md for the licences and the changes made to umac and Musashi.
