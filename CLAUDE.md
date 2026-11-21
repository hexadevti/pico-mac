# pico-mac -- notes for working on it

Mac Plus emulator (umac + Musashi) for PicoCalc + ESP32-P4. The frontend and HAL come from
pico-386 (c:\Users\lucia\repos\pico-386) and pico-amiga. Read README.md and THIRD_PARTY.md
first.

## Layout
- picomac/picomac.ino: Arduino entry (app_setup/app_loop). picomac/core.h: the EmuCore interface.
- picomac/src/frontend: home menu, in-game menu, browser, settings, ui (from pico-386).
- picomac/src/hal: hal.h, desktop (SDL2), picocalc (P4).
- picomac/src/mac/mac_core.cpp: the core (`kMacPlus`). It handles video modes, sound, key
  mapping, disk file ops and PRAM persistence.
- picomac/src/mac/umac: the patched umac + Musashi. Every change must be listed in
  THIRD_PARTY.md.

## Builds
- Desktop (MSYS2 MinGW32 + SDL2):
  `MSYSTEM=MINGW32 /c/msys64/usr/bin/bash.exe -lc "cd /c/Users/lucia/repos/pico-mac && cmake -G 'Unix Makefiles' -S . -B build -DCMAKE_BUILD_TYPE=Debug && make -C build -j8"`
  - umac is always built at -O2.
  - Test hooks: MAC_SD, MAC_DISK, MAC_KEYS ("w1500,shot,quit"), MAC_SHOT.
- P4: `ARDUINO_DIRECTORIES_DATA=~/.emu6502-p4/data ARDUINO_DIRECTORIES_USER=~/.emu6502-p4/user arduino-cli compile --fqbn esp32:esp32:esp32p4:PSRAM=enabled,FlashSize=16M,ChipVariant=postv3,PartitionScheme=huge_app,DebugLevel=error --build-property compiler.optimization_flags=-O2 --build-property compiler.cpp.extra_flags=-DBOARD_PICOCALC_P4 --build-path build-p4 picomac`
  - Upload on COM9.

## Arduino rules
- Every .c/.cpp/.S under picomac/src is compiled. Includes must be relative.
- Files with no extension, or with `.inc`, are not copied (hence m68kfpu_inc.h and
  softfloat-*.h).
- Keep docs and assembler sources out of picomac/ (docs/umac).

## Timing model
- Frame: 130234 CPU cycles × speed, then VBL. 1 Hz every 60.15 frames.
- Sound: 370 samples at 22255 Hz per frame, written with hal_audio_write. Its blocking paces
  the Mac.
- VIA ticks = CPU cycles / (10 × speed): timers stay real-time at any speed.
- Musashi checks IRQs only when m68k_execute starts. umac_run therefore ends the slice on IRQ
  raise and at the next VIA timer event.

## Testing
- No Mac ROM is in the repo or on this machine; the user supplies a Plus v3 ROM (4D1F8172) and
  a System disk.
- A fake ROM can still test the CPU/video path: SSP/PC at 0..7, code at 0x2000 that writes the
  framebuffer.
- Don't commit unless asked.
