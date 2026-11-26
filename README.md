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
/picomac/startups/       the startups, startN.cfg (written by the app)
```

The ROM and System software are not included. A System 6.0.x or 7.0.x boot disk works best. The
disk image is read and written in place, so keep a copy.

## Home screen

As in pico-386, the home screen lists the **startups**. A startup is a named Mac: the machine,
its two disks, RAM, screen and speed, plus its own snapshots. Make one for each System or game
you run. The first time, your earlier settings become one startup per machine that had a disk.

- **Create new startup** asks for a name, then opens the new startup's screen.
- **Settings**: Volume, Show FPS.

**A startup's screen:**

| Row | What it does |
|---|---|
| **Start the Mac** | Powers it on. With no disk, the Mac shows the blinking "?" floppy. |
| **Snapshots** | Start from one of this startup's snapshots, or delete one (see below). |
| **Machine** | Mac Plus, Mac II (or Mac IIci on the desktop and web). The disks stay. |
| **Boot disk** / **Hard disk** | The disk the Mac starts from. Choose a disk image, or eject it. |
| **Second disk** / **Floppy** | A second drive, mounted on the desktop once the Mac has booted. Use it for a game or program disk that has no System. |
| **ROM** | The machine's ROM. It is shared by all startups that use this machine. |
| **RAM** | Plus: 512 KB to 4 MB. Mac II: 1 to 8 MB. IIci: 4 to 32 MB. |
| **Screen** | *Fit*: the 512x342 screen scaled to 320x214 in grey. *1:1 pan*: a 320x320 window that follows the pointer. *Native* (Plus only): the ROM is patched for a 320x320 Mac screen; some programs expect 512x342. |
| **CPU speed** | 1x (the real machine) up to 4x. The Mac II and IIci also have **Max** (the default: a startup switched to a Mac II, or saved at 1x before Max existed, gets it): the 68020 runs as fast as the computer or phone allows, up to 16x, adjusted every frame. Only the CPU speeds up; the clock, timers and sound stay real time. Action games may run too fast: use 1x for them. Max keeps the processor busy, which costs battery on a phone. On the P4 the Mac II and IIci run at 1x. |
| **Rename**, **Delete** | Delete removes only the startup; the disk images stay. |

Disk images are raw (.dsk .img .image .hfv .hfs) or DiskCopy 4.2 (.dc42 .diskcopy). Every change
is saved to the startup at once, including drive changes made in the Mac. The startups live in
/picomac/startups/startN.cfg.

**In the Mac**, the in-game menu (Ctrl+Esc) has a **Drives** submenu:

- Each drive shows its image. Enter offers **Choose a disk image** and **Eject**.
- On the Plus, a change restarts the Mac.
- On the Mac II, a change to the hard disk restarts the Mac. The floppy goes in at once if the
  drive is empty. If the Mac has a disk mounted there, drag it to the Trash first.

## Snapshots

As in pico-386: the in-game menu (Ctrl+Esc) has **Save snapshot** and **Load snapshot** right
below Resume.
- Save: pick "New snapshot" or one to overwrite, then type a name.
- The list shows each snapshot's screen. Enter on one: Start from it, Delete or Back.
- A snapshot holds the whole Mac (CPU, RAM, devices) plus which disks, ROM, RAM size and screen
  mode it used. Starting from one brings those back as the settings.
- The Mac keeps parts of its disks in RAM. When a disk was written after the snapshot was
  taken, the list says so: starting from it may damage that disk.
- Each startup has its own snapshots. Files: /picomac/snapshots/startN/sN.snp, with sN.bmp as
  the picture (a Mac II's in startN-macii). The startups made from the old settings keep
  /picomac/snapshots/<p4|desktop|web>, which each build reads alone.

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
| Ctrl+Esc      | the menu (snapshots, Drives, screen, Reset, Quit) |

Ctrl+Shift+Up returns to the loader, and Ctrl+Shift+F3 restarts.

## Building

The **Mac IIci** (68030 with its MMU, 25 MHz, 4-32 MB, ROM 368CADFE, 512 KB) is on the desktop and
web builds only; the P4 has the Plus and the Mac II.

- P4: the "P4: Build" VS Code task (arduino-cli, `esp32:esp32:esp32p4`).
- Desktop: the "desktop: build" task, or in an MSYS2 MinGW32 shell:
  `cmake -G "Unix Makefiles" -S . -B build && make -C build`. The desktop build uses ./sdcard
  as the card.
  - `MAC_DISK=<image>` boots that image directly (`MAC_DISK=` boots with no disk). `MAC_DISK2=<image>` is the second disk.
  - `MAC_SNAP=<sN.snp>` starts from that snapshot.
  - `MAC_MACHINE=ii` or `iici` picks the machine; `MAC_ROM2` / `MAC_ROM3` are their ROMs (relative to the card).
  - `MAC_KEYS`, `MAC_SHOT` and `MAC_SD` are the test hooks (see hal_sdl.cpp).

## Web (phones)

`web/` is the same emulator in the browser, made for phones. It has the same home menu (Mac Plus,
Mac II or Mac IIci), a Mac keyboard (⌘, ⌥, ⌃ Control for the Mac II, Caps, arrows), the game keys and sound.
- **Touch on the screen** (the default): the pointer goes where the finger touches. Tap to click,
  tap twice to double-click, and hold, then slide, to drag or to pull down a menu. 📁 → More turns
  it off, and the screen becomes a trackpad: slide to move, tap to click, tap twice and slide to drag.
- While the trackpad (🖱, or the one at the side of a sideways screen) shows, it is the mouse: a touch
  on the screen clicks nothing. Two fingers pinch to zoom, and one finger or two move the zoomed view.
- **Game keys** (🎮): a tap on the ⇄ key (next to Esc) turns the arrow pad into the movement
  letters (↑ W, ← A, ↓ S, → D, the corners two letters at once) and back. Holding it for 2 seconds switches the letters: WASD, ESDF, IJKL, ZQSD (AZERTY)
  or QAOP. The page remembers both. Ctrl is the Mac II's Control key.
- Upright, the screen always takes the whole width; the keys or the trackpad share the rest.
- Sideways, the Mac's picture is stretched to fill its whole area, at the phone's full resolution.
  With 🎮 the screen is in the middle, with a simpler set of small keys at its sides: Esc, ⇄, Shift,
  Ctrl, ⌘, Enter, Click and Space at the left; the toolbar and, below it, the 3x3 pad at the right.
  Otherwise the screen takes 3/4 of the width at the left, the toolbar, the mouse button and the
  trackpad the quarter at its right. The Mac keyboard starts hidden there: ⌨ lays it over the
  bottom of the screen (⌨ again hides it).
- **Files** (📁) are kept in the browser's private storage (OPFS). Upload sends ROMs to
  picomac/roms and disk images to picomac/disks. **Receive from a PC** takes files sent from the
  `/send` page on a computer: the phone shows a code, and the computer types it in.
- Bring your own ROM (Plus: 128 KB, Mac II: 256 KB, Mac IIci: 512 KB) and System disk; none come with it.

Building and testing:
- `python web/build.py` (Emscripten SDK in `EMSDK` or `~/emsdk`) writes `web/dist`.
- `python web/serve.py 8767 --only=v3.ROM,System7_0_1` serves it on localhost. The page copies the
  named files from ./sdcard into its storage at startup (add `9779D2C4,System755` for the Mac II, `368CADFE,System755` for the IIci).
- Publishing: `npx wrangler deploy` from `web/`. The Worker (`web/worker/relay.js`) serves dist and
  relays the Receive from a PC rooms. It keeps no files.

See THIRD_PARTY.md for the licences and the changes made to umac and Musashi.
