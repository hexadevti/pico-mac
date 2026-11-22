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
- picomac/src/mac/mac_snapshot.cpp: snapshots (as pico-386). The machine state is umac_save_state /
  umac_load_state (umac/state.h); disk generations live in snapshots/disks.gen.
- picomac/src/mac/umac: the patched umac + Musashi. Every change must be listed in
  THIRD_PARTY.md.

## Builds
- Desktop (MSYS2 MinGW32 + SDL2):
  `MSYSTEM=MINGW32 /c/msys64/usr/bin/bash.exe -lc "cd /c/Users/lucia/repos/pico-mac && cmake -G 'Unix Makefiles' -S . -B build -DCMAKE_BUILD_TYPE=Debug && make -C build -j8"`
  - umac is always built at -O2.
  - Test hooks: MAC_SD, MAC_DISK, MAC_DISK2 (second drive), MAC_SNAP (start from a .snp), MAC_KEYS
    ("w1500,shot,quit"), MAC_SHOT.
- A hung Mac: "@mac" on the P4 console (COM9, 115200; open it with DTR/RTS off, or the board
  resets) or "dump" in MAC_KEYS prints the registers, VIA, the top PCs over 30 frames and
  the code there as words. @shot and @prof (system_p4.cpp) work too. Reading COM9 is fine;
  flashing over it is not.
- P4: `ARDUINO_DIRECTORIES_DATA=~/.emu6502-p4/data ARDUINO_DIRECTORIES_USER=~/.emu6502-p4/user arduino-cli compile --fqbn esp32:esp32:esp32p4:PSRAM=enabled,FlashSize=16M,ChipVariant=postv3,PartitionScheme=huge_app,DebugLevel=error --build-property compiler.optimization_flags=-O2 --build-property compiler.cpp.extra_flags=-DBOARD_PICOCALC_P4 --build-path build-p4 picomac`
  - Never flash over serial (it overwrites the PicoCalc's loader). Copy the .bin through the PicoCalc's loader.

## Arduino rules
- Every .c/.cpp/.S under picomac/src is compiled. Includes must be relative.
- Files with no extension, or with `.inc`, are not copied (hence m68kfpu_inc.h and
  softfloat-*.h).
- Keep docs and assembler sources out of picomac/ (docs/umac).

## Timing model
- Frame: 130234 CPU cycles × speed, then VBL. 1 Hz every 60.15 frames.
- Sound: 370 samples at 22255 Hz per frame. The driver refills the buffer as a ring from the VBL
  (the ROM from 0, the Sound Manager from where it stopped, e.g. 37). umac notes the first sample
  written in the frame (umac_sound_first_write), and the frame plays the buffer from there, wrapping
  around. Plain 0..369 order put seams in every frame (hiss, "blown speaker"). Then SoundFilter
  (20 Hz high-pass only; a 6 kHz low-pass made it muffled) and hal_audio_write; its blocking paces
  the Mac. MAC_WAV=file dumps the stream (desktop).
- VIA ticks = CPU cycles / (10 × speed): timers stay real-time at any speed.
- Musashi checks IRQs only when m68k_execute starts. umac_run therefore ends the slice on IRQ
  raise and at the next VIA timer event.

## Testing
- No Mac ROM is in the repo or on this machine; the user supplies a Plus v3 ROM (4D1F8172) and
  a System disk.
- A fake ROM can still test the CPU/video path: SSP/PC at 0..7, code at 0x2000 that writes the
  framebuffer.
- Don't commit unless asked.
