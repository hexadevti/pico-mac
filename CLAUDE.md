# pico-mac -- notes for working on it

Mac Plus emulator (umac + Musashi) for PicoCalc + ESP32-P4. The frontend and HAL come from
pico-386 (c:\Users\lucia\repos\pico-386) and pico-amiga. Read README.md and THIRD_PARTY.md
first.

## Layout
- picomac/picomac.ino: Arduino entry (app_setup/app_loop). picomac/core.h: the EmuCore interface.
- picomac/src/frontend: home menu, in-game menu, browser, settings, ui (from pico-386). startups.cpp:
  the startups (as pico-386's): picomac/startups/startN.cfg = name, machine, disks, RAM, screen,
  speed, snapdir. The one open is written back on every settingsSave(); snapDir() uses its snapdir
  (+ -macii/-maciici). A card without startups gets one per machine from the old cfg (snapdir "" =
  the old per-machine folders). mac/mac_drives.h is the Drives submenu of both cores and the
  startup screen. The desktop test hooks (MAC_DISK, MAC_SNAP) bypass the startups.
- picomac/src/hal: hal.h, desktop (SDL2), picocalc (P4).
- picomac/src/mac/mac_core.cpp: the core (`kMacPlus`). It handles video modes, sound, key
  mapping, disk file ops and PRAM persistence.
- picomac/src/mac/mac_snapshot.cpp: snapshots (as pico-386). The machine state is umac_save_state /
  umac_load_state (umac/state.h); disk generations live in snapshots/disks.gen.
- picomac/src/mac2: the Mac II core (`kMacII`; boots System 7.5.5 to the Finder). macii_core.cpp holds the memory map,
  two VIAs (via6522.h) and our own video card in slot 9. m68kcpu020.c/m68kops020.c are Musashi
  rebuilt as a 68020 + 68881 (m68kconf020.h, symbols renamed by m68k020_names.h). The FPU normalizes
  unnormalized extended operands first (fx80_normalize): softfloat's FDIV looped for minutes on one
  (SimCity 2000's power grid; a hang with the clock stopped = look for a long m68k_execute). The home menu's
  "Machine" row picks the core. adb.h (the ADB transceiver on VIA1, keyboard and mouse),
  ncr5380.h (SCSI, no targets yet), macii_slotrom.h (the card's declaration ROM and its driver,
  which calls the emulator through opcodes 0x7110-0x7114), macii_sony.h (disks: our .Sony,
  patched over the ROM's at power-on, opcodes 0x7120-0x7123; the ROM checksum is recomputed),
  rtc.h (clock + 256-byte XPRAM in picomac/pram2.bin; SPConfig always reads "async": with no SCC,
  AppleTalk hangs the boot), asc.h (the Apple Sound Chip, FIFO and wavetable; its IRQ is VIA2 CB1; 0x804 is read-only and its half/empty bits are levels: the Sound Manager refills only on the half bit, and edge-only flags muted PoP).
  Speed on the P4: macii_iram.h puts the hot 68020 handlers in IRAM (regenerate the list from a
  -DMACII_OPCOUNT desktop run, which writes MACII_OPCOUNT_OUT every minute); the dispatch is a 16-bit
  index table (M68K_DISPATCH); the bus fast paths in macii_bus.h are always_inline there.
  With 8 MB, a 24-bit write at 0x800000-0x8FFFFF also lands in RAM from 0: the ROM's start-up
  test 5 needs that wrap, or it stops in its test loop (chime, white screen).
  The slot 9 card mirrors its first MB across the slot: 32-Bit QuickDraw draws at 0xF9900000.
  Snapshots: the same mac_snapshot.cpp (its own folder, "-macii", magic PMIISNP1); macii_core.cpp's
  saveState writes the sizes of every part first, so a build whose structs changed says "another
  version" instead of loading junk. The arrows move the pointer through the cursor globals, as the Plus.
  Colours: XPRAM 0x46 is slot 9's PRAM (board ID 0x0123, then 0x48 = the start-up mode, spID 0x80-0x83 =
  1-8 bits). System755.dsk has no Monitors panel, so the menu's Colors row writes it and restarts;
  a fresh PRAM starts at 256. MAC_KEYS on the Mac II also takes atXxY, mdown, mup (the pointer).
  CPU speed (not on the P4): s_speed is in quarters (4 = 1x); the setting 5 = Max, maxSpeedStep holds the frame's work near 11.5 ms (up to 16x). The VIAs stay real time.
  Below full speed the core stretches each frame's sound (SoundStretch, from the queued audio, up
  to 25%): without it the P4's DMA ran dry several times a second, a crackle on game sounds.
  The keys are ADB raw codes: the arrows are 0x3B-0x3E (the umac MKC_ ones, 0x7B-0x7E, are the
  right-hand modifiers there).
  ADB: System 7.5's driver auto-polls by sending Talk R0 and going to state 3; the interrupt there
  only says "data", the bytes come in states 1, 2 (adb.h).
- The Mac IIci (`kMacIIci`, desktop + web only; maciici_core.cpp is empty on the P4): macii_core.cpp
  compiled again with MACII_IICI (maciici_names.h renames it) on a 68030 (m68kcpu030.c/m68kops030.c,
  m68kconf030.h; umac's m68kcpu.c includes M68K_MMU_HEADER = maciici_mmu.h, the PMMU instructions).
  The table walk and a 4 KB-page TLB are in the core's bus (mmuWalk, translate, tlbFill). Map: RAM
  bank A at 0, bank B at 0x04000000 (each mirrored over its 64 MB; 8 MB = 4+4, 32 MB = 16+16), ROM
  0x40000000 (512 KB, 368CADFE), I/O 0x50000000 (off>>13: 0 VIA1, 8 SCSI, 10 ASC, 0x13 the RBV in
  place of VIA2), our video card in slot E. The RBV's slot lines are levels: every change of the
  card's VBL enable/pending must call cardIrq(), or the slot dispatcher loops forever (grey screen).
  The card's interrupt routine passes its slot (slotrom::build's `slot`) to jVBLTask: the cursor task
  runs only for the main screen's slot (ScrnVBLPtr), so a wrong slot freezes the pointer.
  CRP/SRP loads keep the TLB: _SwapMMUMode PLOADs the new TC's address, loads the CRP, then reads
  the TC through the old mapping (PLOAD fills the TLB, as the ATC). XPRAM in picomac/pramci.bin.
  Test: MAC_MACHINE=iici MAC_ROM3="picomac/368CADFE - Mac IIci.ROM"; System755.dsk boots to the Finder
  (MAC_KEYS "w40000,enter,..."). -DMACII_PC_HISTORY adds MACII_TRAP=<hex pc> (jump history at that PC).
- The Quadra 700 (`kMacQuadra`, desktop + web only): macii_core.cpp again with MACII_QUADRA (macquadra_names.h;
  MACII_PMMU = the TLB bus, shared with the IIci) on a 68040 (m68kcpu040.c/m68kops040.c, m68kconf040.h). Musashi's
  040 lacks MOVEC's MMU registers, CINV/CPUSH, PFLUSH, PTEST, MOVE16 and the 040 FSAVE frames: macquadra_mmu.h
  has those handlers and jumpTableToRam puts them over Musashi's. The 040 walk (8 KB pages, ITT/DTT) is in the
  core's bus. Map: ROM 1 MB at 0x40000000 (420DBFF3), RAM contiguous from 0, I/O 0x50000000 (off>>13: 0 VIA1,
  1 VIA2, 7 the 53C96 at +0xF000, 10 ASC, 15 SWIM), DAFB 0xF9 reads 0 (no monitor), our card in slot E. 25 MHz.
  VIA1 PA reads 0xC1 (the box: BoxFlag 16 = Gestalt 22). ncr53c96.h: no targets, a selection times out a few
  frames later (disconnect, VIA2 CB2); without it SCSI Manager 4.3 polls forever at "Starting up".
  XPRAM in picomac/pramq700.bin, snapshots in "-macquadra". Test: MAC_MACHINE=quadra
  MAC_ROM4="picomac/420DBFF3 - Quadra 700&900 & PB140&170.ROM"; System755.dsk boots to the Finder.
  MAC_RAMDUMP=<file> makes "dump" write the RAM too (desktop), for a disassembler.
  RAM 4/8/16/32 MB (MAC_RAM=0-3): contiguous, and only a power of two ends the ROM's sizing loop.
  z8530.h: the SCC at +0xC000 (IRQ level 4), with the transmitters only. Mac OS 8's Open Transport starts
  LocalTalk whatever SPConfig says and waits for its frames to go out ("Starting up" forever without it).
  Mac OS 8.1: the CD .iso boots as MAC_DISK (macii_sony.h takes the first Apple_HFS partition of a
  partitioned image; .iso/.cdr/.toast are read-only). An image bigger than a floppy is a hard disk:
  qType 1 with its size, Drive Info type 1, and a unit-table entry of its own (the installer refuses
  volumes on .Sony, refNum -5). A blank file initializes (Format succeeds); sdcard/picomac/MacOS81.dsk
  (512 MB) holds an 8.1 installed from the CD. Keep it HFS ("Mac OS Standard"): 68K Macs can't start from HFS+.
  MAC_KEYS_FILE=<file> drives a long run step by step (each time the file appears, its text is the next script).
- web/: the phone version (as pico-386's). web/src/hal_web.cpp (EMU_WEB) replaces the SDL HAL; the cores
  take the page's pointer once a frame (webPointer, EMU_WEB): absolute Mac pixels from the touch screen
  (web_pointer) or trackpad moves (web_mouse), written to the cursor globals. The button's changes are queued
  and the core takes one every three frames, so short taps and a double tap's release all reach the Mac. web/shell is the page (app.js: Mac
  keyboard, the touch-screen mode `touchAbs`, the Files screen); KEY_CTRL is ⌘, KEY_SYM ⌥, KEY_ALT the
  mouse button, and Caps lives on the page (the cores ignore KEY_CAPS). The web adds picomac/disks and
  picomac/roms. web/worker/relay.js is the "Receive from a PC" relay only (no TCP).
- picomac/src/mac/umac: the patched umac + Musashi. Every change must be listed in
  THIRD_PARTY.md.

## Builds
- Desktop (MSYS2 MinGW32 + SDL2):
  `MSYSTEM=MINGW32 /c/msys64/usr/bin/bash.exe -lc "cd /c/Users/lucia/repos/pico-mac && cmake -G 'Unix Makefiles' -S . -B build -DCMAKE_BUILD_TYPE=Debug && make -C build -j8"`
  - umac is always built at -O2.
  - Test hooks: MAC_SD, MAC_DISK, MAC_DISK2 (second drive), MAC_SNAP (start from a .snp), MAC_MACHINE=ii
    (the Mac II core), MAC_ROM2 (its ROM, relative to MAC_SD), MAC_KEYS
    ("w1500,shot,quit"), MAC_SHOT.
- A hung Mac: "@mac" on the P4 console (COM9, 115200; open it with DTR/RTS off, or the board
  resets) or "dump" in MAC_KEYS prints the registers, VIA, the top PCs over 30 frames and
  the code there as words. @shot and @prof (system_p4.cpp) work too. Reading COM9 is fine;
  flashing over it is not.
- P4: `ARDUINO_DIRECTORIES_DATA=~/.emu6502-p4/data ARDUINO_DIRECTORIES_USER=~/.emu6502-p4/user arduino-cli compile --fqbn esp32:esp32:esp32p4:PSRAM=enabled,FlashSize=16M,ChipVariant=postv3,PartitionScheme=huge_app,DebugLevel=error --build-property compiler.optimization_flags=-O2 --build-property compiler.cpp.extra_flags=-DBOARD_PICOCALC_P4 --build-path build-p4 picomac`
  - Never flash over serial (it overwrites the PicoCalc's loader). Copy the .bin through the PicoCalc's loader.
- Web: `python web/build.py` (emsdk in ~/emsdk; objects in build-web/, output in web/dist).
  `python web/serve.py 8767 --only=v3.ROM,System7_0_1,9779D2C4,System755` seeds the page's OPFS from
  ./sdcard (localhost only). Test in headless Chrome over CDP (Input.dispatchTouchEvent for touches).

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
- The ROMs and disk images are in sdcard/picomac (gitignored; never commit them): the Plus v3
  ROM (4D1F8172), "9779D2C4 - MacII (800k v2).ROM" and System7_0_1.img (a bare HFS volume).
- Mac II: MACII_TRACE=1 logs the CPU and VIAs each second, MACII_ADB=1 the ADB states and bytes. Build with -DMACII_PC_HISTORY to log the
  last 64 jumps on an illegal instruction. The ROM is disassembled with Musashi's disassembler
  (a small host tool). With MAC_DISK=<full path to System7_0_1.img> it boots System 7, which stops at "created with
  Minimal software" (that image is a Plus-only install). sdcard/picomac/System755.dsk (System 7.5.5
  Color, from a Mini vMac bundle) boots to the Finder: MAC_KEYS="w100000,enter,..." dismisses the
  "not shut down properly" alert.
- A fake ROM can still test the CPU/video path: SSP/PC at 0..7, code at 0x2000 that writes the
  framebuffer.
- Don't commit unless asked.
