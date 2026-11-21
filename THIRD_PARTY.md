# Third-party code

## umac (picomac/src/mac/umac)

Matt Evans' Mac Plus emulator, https://github.com/evansm7/umac, MIT license (the notice is at
the top of each file). Its README and history are in docs/umac/, together with sonydrv.S, the
source of the replacement .Sony driver in sonydrv.h. That file was moved out of the sketch
because Arduino tries to assemble every .S file.

Changes made for pico-mac:

- **main.c**: rewritten below the original licence header. The unix front end (SDL, mmap,
  getopt) is gone. In its place is a library API (umac.h): `umac_init(ram, rom, size, discs)`,
  `umac_run(cycles)`, reset, speed, mouse, keyboard, RTC/PRAM access and the sound and video
  page getters.
  - RAM size and screen size are chosen at run time (`umac_ram_size`, `umac_disp_w/h`), not at
    compile time.
  - The keyboard is a real M0110 protocol model: Inquiry/Instant/Model/Test, a 64-byte queue,
    the keypad prefix 0x79 for codes 0x40 and up, and ADB arrow codes mapped to the Plus
    keyboard's.
  - Real RTC: a serial command state machine on VIA port B, seconds, 20 bytes of PRAM and
    write-protect.
  - VIA timing comes from CPU cycles. Timer events cut the CPU slice short, and so do raised
    IRQs, because Musashi only checks interrupts when `m68k_execute` starts.
  - Device word/long accesses go through the byte handlers. Unmapped accesses read 0 instead
    of exiting.
- **via.c / via.h**:
  - Working timers 1 and 2: latches, one-shot and free-run on timer 1, and IRQ flags cleared on
    read/write as on the 6522. They run on a time base the host supplies (`time_now`,
    `timer_set`), plus `via_tick`/`via_next_event`.
  - Getters for ORA/ORB/DDRB.
- **rom.c**:
  - The screen-size patch is a run-time choice. It is skipped at 512x342. An `#error` became a
    run-time size check.
  - A misplaced parenthesis in the big-endian read was fixed. The compile-time RAM-size block
    was removed.
- **disc.c**:
  - Guest addresses are masked to RAM, and a transfer that would run past RAM is refused.
  - `ioActCount` is set to the transferred length.
  - Eject calls back into the front end, and no drive is mounted when no image is given.
- **machw.h**: run-time RAM size and the address mask.

## Musashi (picomac/src/mac/umac/m68k*)

Karl Stenerud's 68000 core, as vendored by umac (Musashi licence: free to use, with the notice
kept; see docs/umac/readme-musashi.txt). `m68kops.c`/`m68kops.h` are the generator's output and
are checked in. The generator is in tools/musashi-gen.

Changes:

- m68kfpu.c was renamed **m68kfpu_inc.h**: m68kcpu.c includes it, and Arduino would otherwise
  compile it on its own. Arduino also copies no `.inc` file.
- softfloat-macros and softfloat-specialize gained a **.h** extension, because Arduino does not
  copy files without a known extension.

## Frontend

src/frontend, src/hal and the UI font come from the author's emu16 / pico-amiga / pico-386
projects.
