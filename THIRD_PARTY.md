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
- For the Mac II's 68020 build (below), none of which changes the Plus's 68000:
  - **CPU_HAS_FPU** (m68kcpu.h): the FPU opcodes are allowed on a 68020 when the config sets
    `M68K_EMULATE_FPU` (a 68020 + 68881). Upstream only allows them from the 68030. The two FPU
    handlers in m68kops.c and tools/musashi-gen/m68k_in.c test it instead of `CPU_TYPE_IS_030_PLUS`.
  - **M68K_STATIC_INSTR_CYCLES** (m68kcpu.h): the cycles charged per instruction with static
    tables, which was a fixed 8. It defaults to 8; the 68020 build uses 5.
  - **fx80_normalize** (m68kfpu_inc.h): `fpgen_rm_reg` normalizes an unnormalized extended
    operand (exponent set, integer bit clear) before the operation, as the 68881 does. Softfloat
    assumes normal operands: floatx80_div's correction loop ran ~2^63/significand times, and
    SimCity 2000's power grid froze the Mac II for minutes.
  - **M68K_FIXED_CPU_API** (m68kcpu.c): with a fixed CPU type, `m68k_set_cpu_type` asserts against
    this API value when it is defined, because `CPU_TYPE_020` (internal) differs from
    `M68K_CPU_TYPE_68020` (the API).
- For the Mac IIci's 68030 build: **M68K_MMU_HEADER** (m68kcpu.c): when the config defines it,
  m68kcpu.c includes that header in place of m68kmmu.h. The IIci's maciici_mmu.h holds the PMMU
  instructions there (PMOVE, PLOAD, PFLUSH, PTEST); the table walk and the translation cache are
  in the core's bus. Without the define nothing changes.

### The 68020 build (picomac/src/mac2/m68k*)

The same Musashi sources, compiled a second time as a 68020 + 68881 for the Mac II core:
m68kcpu020.c and m68kops020.c `#include` m68kcpu.c and m68kops.c with `MUSASHI_CNF` pointing at
m68kconf020.h. That config fixes the CPU at the 68020, enables the FPU, and maps the bus to
`macii_read*`/`macii_write*` (macii_bus.h). m68k020_names.h renames every global symbol
(`m68k_*` → `m68k020_*`, `m68ki_*` → `m68k020i_*`) so that both CPUs link into one program. The
softfloat code is shared. Each machine keeps its own fixed-type static jump table, so the Plus runs
no slower.

m68kconf020.h differs from umac's m68kconf.h in these ways:
- `M68K_EMULATE_010` and `M68K_EMULATE_EC020` are ON. The CPU type is still a fixed 68020. With
  them OFF, m68kcpu.h's `CPU_TYPE_IS_010_LESS` falls back to `CPU_TYPE_IS_020_LESS`, which is true
  when there is no 030. The 68020 then decoded indexed addressing (and more) as a 68000 would.
- `M68K_ILLG_HAS_CALLBACK` goes to `macii_illegal`. It handles opcodes 0x7110-0x7114, which our
  video card's driver uses to call the emulator (as Basilisk II does), and it logs the rest.
- `M68K_INSTRUCTION_HOOK` is on only in a debugging build (`-DMACII_PC_HISTORY`).
- `M68K_SEPARATE_READS` is ON, and the memory macros go to inline functions in macii_bus.h:
  RAM is read and written there without a call, and instruction fetches use a window over the RAM
  or the ROM block the PC is in. Anything else goes to `macii_read*`/`macii_write*`.
- m68kcpu.c dispatches through `M68K_DISPATCH(ir)`; it defaults to `instruction_jump_table[ir]()`
  (the Plus, unchanged). The 020 config replaces it with a 16-bit index per opcode into the
  distinct handlers, both built in RAM at start-up from the static table (in flash); on the P4 a
  dispatch from flash was a slow cache miss.
- `M68K_INSTRUCTION_HOOK` is also on in `-DMACII_OPCOUNT` (a desktop build that counts opcodes).
- On the P4, `M68K_FAST_FUNC` in the 020 config puts code in IRAM, and macii_iram.h does the same
  for the 300 handlers that ran most and m68kcpu.h's out-of-line helpers. It only adds declarations
  with a section attribute; the Musashi sources are not edited for it.

- The ILLG callback also takes opcodes 0x7120-0x7123, from our .Sony driver (below).

### The 68030 build (picomac/src/mac2/m68k*030*, the Mac IIci; not on the P4)

A third compile of the same sources, as the 020 build: m68kcpu030.c and m68kops030.c with
m68kconf030.h (a fixed `CPU_TYPE_030`, the FPU, `HAS_PMMU`, and `M68K_MMU_HEADER` = maciici_mmu.h)
and m68k030_names.h for the symbols (`m68k030_*`). maciici_core.cpp compiles macii_core.cpp again
with `MACII_IICI` and maciici_names.h, so the IIci core is the Mac II's with its own memory map
(the RBV in place of VIA2, two RAM banks, the video card in slot E) and the MMU.

adb.h, ncr5380.h, rtc.h (the clock and XPRAM) and macii_slotrom.h (the card's declaration ROM and
its driver) are new code written for pico-mac, not taken from other projects. asc.h (the Apple Sound
Chip) is new code too, written after the register descriptions in MAME's asc.cpp (BSD-3-Clause);
no MAME code is copied.

macii_sony.h replaces the Mac II ROM's .Sony driver in memory (and fixes the ROM checksum), so
that disk images boot as on umac. Its C++ side is a port of umac's disc.c, itself a cut-down
version of Basilisk II's sony.cpp (Basilisk II (C) 1997-2008 Christian Bauer, GPLv2; umac MIT).
The 68k driver is umac's sonydrv.S with opcodes 0x7120-0x7123 in place of its magic address,
hand-assembled.

## Frontend

src/frontend, src/hal and the UI font come from the author's emu16 / pico-amiga / pico-386
projects.
