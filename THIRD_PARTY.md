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
  - The RAM writes in `cpu_write_byte/word/long` note the first sound-buffer word written (either
    page) since `umac_sound_mark_reset()`; `umac_sound_first_write()` returns it. The frontend
    plays the buffer as a ring from there.
  - Debugging: `umac_pc_sample(buf, n)` stores the PC after each `umac_run` slice;
    `umac_dump_regs` exposes the register dump. via.c: `via_peek(reg)` reads a register as
    stored. They serve mac_core's "@mac" console command.
  - `vbl_queue_repair` runs at the end of each `umac_run`. It walks the VBL queue (0x162) and
    cuts a link back to an element already seen, setting qTail to that element. The System 7.0.1
    sound driver can VInstall its VBL task while the task is still queued, because it clears its
    "running" flag before the ROM dequeues it. Enqueue then links the task to itself and the
    ROM's VBL loop never ends (seen in Prince of Persia). `umac_vbl_fixes()` counts the cuts.
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
  - Two drives (the original had a "FIXME: Disc 2"). Open is called once per drive index (in
    d1) and returns the drive number to AddDrive, 0 when there are no more. The other drives
    are mounted like Basilisk's `mount_mountable_volumes`: accRun returns a drive number, the
    driver posts a disk-inserted event, and GetNextEvent mounts the volume. Only installed
    drives match a drive number.
- **sonydrv.S / sonydrv.h**: the .Sony stub's Open loops over the drives, and its Control
  calls PostEvent(diskEvt) for the drive accRun returns. sonydrv.h was hand-assembled (no m68k
  assembler here) and checked with Musashi's disassembler.
- **machw.h**: run-time RAM size and the address mask.
- Snapshots: **state.h** (new) moves a module's state through one function that serves both
  ways. `via_state` (via.c), `scc_state` (scc.c) and `disc_state` (disc.c) cover their statics.
  main.c adds `umac_save_state`/`umac_load_state`, which save and load the CPU context, the cycle
  counters, overlay, RTC, keyboard, IWM and mouse state, then the modules, then all of RAM. The
  stream has a magic and a header (RAM and screen size, context size). `oldval` in
  `via_ra_changed` became the file-scope `via_ra_old`, so that it can be saved.

## Musashi (picomac/src/mac/umac/m68k*)

Karl Stenerud's 68000 core, as vendored by umac (Musashi licence: free to use, with the notice
kept; see docs/umac/readme-musashi.txt). `m68kops.c`/`m68kops.h` are the generator's output and
are checked in. The generator is in tools/musashi-gen.

Changes:

- m68kfpu.c was renamed **m68kfpu_inc.h**: m68kcpu.c includes it, and Arduino would otherwise
  compile it on its own. Arduino also copies no `.inc` file.
- softfloat-macros and softfloat-specialize gained a **.h** extension, because Arduino does not
  copy files without a known extension.
- **m68k_set_context_keep_hooks** (m68kcpu.c, m68k.h): loads a saved context but keeps the live
  cycle tables and callbacks (a snapshot's pointers are from another run).

## Frontend

src/frontend, src/hal and the UI font come from the author's emu16 / pico-amiga / pico-386
projects.
