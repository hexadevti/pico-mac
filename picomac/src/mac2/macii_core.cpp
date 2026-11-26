// macii_core.cpp -- the Macintosh II core (kMacII): a 68020 + 68881 (Musashi built a second time,
// m68kcpu020.c) on the Mac II's memory map, with its two VIAs and a colour video card in NuBus slot 9.
//
// STATUS: it boots the real ROM and System 7.5.5 to the Finder from a disk image. It has the address
// map, VIA1/VIA2 (timers, the 60.15 Hz and 1 Hz interrupts, the slot interrupt), ADB (keyboard and
// mouse, adb.h), the clock and XPRAM (rtc.h, kept in picomac/pram2.bin), the ASC (sound, asc.h), the
// video card with its declaration ROM and driver (macii_slotrom.h), and disks through our .Sony
// driver (macii_sony.h). Not there yet: SCSI targets, the SWIM, the SCC (so no AppleTalk: rtc.h
// keeps both ports "async") and bus errors for empty slots. Accesses to what is not there are logged ("macii: unmapped ...") to show what the ROM wants next.
//
// Memory map (physical, 32-bit):
//   0x00000000  RAM (overlaid by the ROM after reset, until the ROM is read at its own address)
//   0x40000000  ROM, 256 KB, repeated
//   0x50000000  I/O, repeated every 0x20000: VIA1 +0x0000, VIA2 +0x2000, SCC +0x4000,
//               SCSI +0x10000, SCSI DMA +0x12000, ASC +0x14000, SWIM +0x16000
//   0xF9000000  slot 9: the video card (0xFs000000 for slots 9..E)
// 24-bit mode (the Mac II starts in it): 0x000000 RAM (8 MB), 0x800000 ROM, 0x900000..0xEFFFFF
// slots 9..E (0xs00000 -> 0xFs000000), 0xF00000 I/O. VIA2 port B bit 3 switches to 32-bit mode
// (TO VERIFY with the real ROM: the bit and its polarity).
//
// Timing: 15.6672 MHz; a frame is 260468 CPU cycles (Musashi counts 4 per instruction here), the VIA
// ticks at a twentieth of that (783.36 kHz), times the speed setting as in the Plus core.
//
// The video card (ours, not Apple's: the driver will be written for it, as Mini vMac and Basilisk II
// do): 512 KB of VRAM at slot offset 0, registers at +0x80000 (32-bit, big-endian):
//   +0x00 depth (log2 of bits per pixel, 0..3)   +0x04 row bytes   +0x08 base offset in VRAM
//   +0x0C colour table index                      +0x10 colour 0x00RRGGBB (write: next index)
//   +0x14 VBL interrupt enable (bit 0)            +0x18 VBL interrupt: read bit 0 = pending, write = ack
// The card decodes 20 address bits below its declaration ROM: VRAM and registers repeat every MB of
// the slot's 16. The Slot Manager hands out 0xF9900000 as the screen's base (it works as 0x900000 in
// 24-bit mode), and 32-Bit QuickDraw draws there in 32-bit mode.
// The screen is 640x480; Fit scales it to 320x240 (2x2 pixels averaged), 1:1 pans it with the pointer.
//
// As the Plus core: Ctrl+Caps Lock zoom, the arrows as a fast mouse (through the cursor globals),
// snapshots (mac_snapshot.cpp, the state in saveState/loadState), disk generations, "@mac".
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <string>
#include "../hal/hal.h"
#include "../frontend/ui.h"
#include "../frontend/settings.h"
#include "../frontend/browser.h"
#include "../../core.h"
#include "via6522.h"
#include "adb.h"
#include "ncr5380.h"
#include "ncr53c96.h"
#include "z8530.h"
#include "macii_slotrom.h"
#include "macii_sony.h"
#include "rtc.h"
#include "asc.h"
#include <unordered_map>
#include <vector>
#include "../mac/umac/keymap.h"   // MKC_*: ADB key codes
#include "../mac/mac_snapshot.h"
#include "../mac/mac_drives.h"
#include "macii_bus.h"

extern "C" {
#if defined(MACII_QUADRA)
#include "m68k040.h"
#define MACQUADRA_MMU_API_ONLY
#include "macquadra_mmu.h"
#define m68kXi_static_instruction_jump_table m68k040i_static_instruction_jump_table
#elif defined(MACII_IICI)
#include "m68k030.h"
#define MACIICI_MMU_API_ONLY
#include "maciici_mmu.h"
#define m68kXi_static_instruction_jump_table m68k030i_static_instruction_jump_table
#else
#include "m68k020.h"
#define m68kXi_static_instruction_jump_table m68k020i_static_instruction_jump_table
#endif
// Musashi's opcode table (m68kops.c): 256 KB of pointers, in flash.
extern void (*const m68kXi_static_instruction_jump_table[0x10000])(void);
}

extern const EmuCore kMacII;

namespace {

#if defined(MACII_QUADRA)
// The Quadra 700: a 68040 at 25 MHz (the VIAs as the IIci's), a 1 MB ROM, RAM from 0 (4 MB on the
// board and four SIMMs).
constexpr uint32_t kCpuHz = 25067520;
constexpr uint32_t kFrameCycles = 416749;           // kCpuHz / 60.15
constexpr uint32_t kViaDiv = 32;                    // CPU cycles per VIA tick
constexpr uint32_t kRomSize = 1024 * 1024;
const uint32_t kRamSizes[RAM2_COUNT] = { 4u << 20, 8u << 20, 16u << 20, 32u << 20 };
const char *const kRamNames[RAM2_COUNT] = { "4 MB", "8 MB", "16 MB", "32 MB" };
#define g_romPath g_settings.romPathQuadra
#define g_ramIndex g_settings.ramQuadra
constexpr const char *kMachineName = "Quadra 700";
#elif defined(MACII_IICI)
// The IIci: a 68030 at 25 MHz (32 times the VIA's 783.36 kHz), a 512 KB ROM.
constexpr uint32_t kCpuHz = 25067520;
constexpr uint32_t kFrameCycles = 416749;           // kCpuHz / 60.15
constexpr uint32_t kViaDiv = 32;                    // CPU cycles per VIA tick
constexpr uint32_t kRomSize = 512 * 1024;
const uint32_t kRamSizes[RAM2_COUNT] = { 4u << 20, 8u << 20, 16u << 20, 32u << 20 };
const char *const kRamNames[RAM2_COUNT] = { "4 MB", "8 MB", "16 MB", "32 MB" };
#define g_romPath g_settings.romPathIIci
#define g_ramIndex g_settings.ramIIci
constexpr const char *kMachineName = "Mac IIci";
#else
constexpr uint32_t kCpuHz = 15667200;
constexpr uint32_t kFrameCycles = 260468;           // kCpuHz / 60.15
constexpr uint32_t kViaDiv = 20;                    // CPU cycles per VIA tick
constexpr uint32_t kRomSize = 256 * 1024;
const uint32_t kRamSizes[RAM2_COUNT] = { 1u << 20, 2u << 20, 4u << 20, 8u << 20 };
const char *const kRamNames[RAM2_COUNT] = { "1 MB", "2 MB", "4 MB", "8 MB" };
#define g_romPath g_settings.romPathII
#define g_ramIndex g_settings.ramII
constexpr const char *kMachineName = "Mac II";
#endif
constexpr uint32_t kSliceMax = 4096;                // CPU cycles per m68k_execute at most
constexpr uint32_t kRate = 22255;
constexpr int kSamples = 370;                       // a frame of sound
constexpr int kStripH = 10;

// --- machine ------------------------------------------------------------------------------------
uint8_t *s_ram = nullptr;
uint32_t s_ramSize = 0;
uint8_t *s_rom = nullptr;
sony::Drives s_sony;                // the disks, through our .Sony driver (macii_sony.h)
// The drives' images ("" = none), as macii_sony.h's indexes: hard disk 1 (the boot disk), floppy 1,
// hard disk 2, floppy 2, the CD-ROM (read-only). In the settings: disk, disk2, hd2, floppy2, cdrom.
std::string s_path[sony::kDrives];
std::string &s_diskPath = s_path[0];
const char *const kDriveNames[sony::kDrives] = { "Hard disk 1", "Floppy 1", "Hard disk 2", "Floppy 2", "CD-ROM" };
constexpr int kCdrom = 4;
inline bool isFloppy(int i) { return i == 1 || i == 3 || i == kCdrom; }   // swapped while the Mac runs
std::string &settingOf(int i) {
  return i == 0 ? g_settings.disk : i == 1 ? g_settings.disk2 : i == 2 ? g_settings.hd2
       : i == 3 ? g_settings.floppy2 : g_settings.cdrom;
}
bool s_touched[sony::kDrives];      // the drive was written since the start or the last snapshot
bool s_running = false;
char s_error[96];
bool s_overlay = true;              // the ROM at 0 after reset
bool s_addr32 = false;              // 24-bit mode after reset
#if defined(MACII_IICI)
void updateIrq();
// The IIci's RBV in place of VIA2 (as MAME's pseudovia): no timers, no shift register. Register
// offsets (address & 0x13): 0x00 port B, 0x01 config, 0x02 slot interrupts (active low: 0x08 slot C,
// 0x10 D, 0x20 E, 0x40 built-in video), 0x03 the interrupt flags (0x02 any slot, 0x08 SCSI, 0x10 ASC,
// 0x80 any), 0x10 video (depth, monitor sense in bits 3-5), 0x12 slot enable, 0x13 interrupt enable.
// With no monitor on the built-in port (sense 7), the ROM leaves its video off.
struct Rbv {
  uint8_t r[0x14];
  void reset() { memset(r, 0, sizeof r); r[2] = 0x7F; r[3] = 0x1B; }
  bool irqOut() const { return (r[3] & 0x80) != 0; }
  uint8_t ifr() const { return r[3]; }
  uint8_t ier() const { return r[0x13]; }
  uint8_t portB() const { return r[0]; }
  void recalc() {
    if (~r[2] & 0x78 & r[0x12]) r[3] |= 0x02; else r[3] &= ~0x02;
    const uint8_t f = r[3] & r[0x13] & 0x1B;
    if (f) r[3] = f | 0x80; else r[3] &= 0x7F;
    updateIrq();
  }
  // A slot's interrupt line (bit: 0x08 C .. 0x20 E, 0x40 built-in video), true = asserted.
  void slot(uint8_t bit, bool on) {
    if (on) r[2] &= (uint8_t)~bit; else r[2] |= bit;
    recalc();
  }
  void asc() { r[3] |= 0x10; recalc(); }
  uint8_t read(uint32_t off) const {
    off &= 0x13;
    if (off == 0x10) return (uint8_t)((r[0x10] & ~0x38) | 0x38);   // monitor sense 7: none
    if (off == 0x12 || off == 0x13) return r[off] & 0x7F;
    return r[off];
  }
  void write(uint32_t off, uint8_t v) {
    off &= 0x13;
    switch (off) {
      case 0x02: r[2] |= v & 0x40; break;
      case 0x03: r[3] &= (uint8_t)~(v & 0x7F); break;
      case 0x12: case 0x13:
        if (off == 0x13 && v == 0xFF) { r[0x13] = 0x1F; break; }   // the IIci's POST writes this
        if (v & 0x80) r[off] |= v & 0x7F; else r[off] &= (uint8_t)~(v & 0x7F);
        break;
      default: r[off] = v;
    }
    recalc();
  }
};
Via6522 s_via1;
Rbv s_rbv;
constexpr uint32_t kCardSlot = 0xE;         // the IIci's slots are C, D and E
#else
Via6522 s_via1, s_via2;
#if defined(MACII_QUADRA)
constexpr uint32_t kCardSlot = 0xE;         // the Quadra 700's slots are D and E
#else
constexpr uint32_t kCardSlot = 9;
#endif
#endif
AdbBus s_adb;
#if defined(MACII_QUADRA)
Ncr53c96 s_scsi;                    // at 0x5000F000, a register every 0x10 bytes
Z8530 s_scc;                        // at 0x5000C000: B control, A control, B data, A data (+0/2/4/6)
#else
Ncr5380 s_scsi;                     // at 0x50F10000, a register every 0x10 bytes
#endif
Asc s_asc;                          // at 0x50F14000 (asc.h)
uint64_t s_adbAt = UINT64_MAX;     // the VIA tick at which the ADB byte in progress is done
int s_irqLevel = 0;

// CPU time
uint64_t s_cycles = 0;              // CPU cycles before the current slice
bool s_inSlice = false, s_sliceEnded = false;
int s_sliceEndedAt = 0;
uint64_t s_viaBase = 0, s_cycBase = 0;   // the VIA time at s_cycBase (speed changes rebase)
int s_speed = 4;                         // the 68020's speed in quarters of the real one (4 = 1x)
uint64_t s_runCycles = 0;                // the cycles run (the FPS meter's, as Max varies)

uint64_t cyclesNow() {
  if (!s_inSlice) return s_cycles;
  return s_cycles + (uint64_t)(s_sliceEnded ? s_sliceEndedAt : m68k_cycles_run());
}

void endSlice() {
  if (s_inSlice && !s_sliceEnded) {
    s_sliceEndedAt = m68k_cycles_run();
    s_sliceEnded = true;
    m68k_end_timeslice();
  }
}

uint64_t viaAt(uint64_t cyc) { return s_viaBase + (cyc - s_cycBase) * 4 / (kViaDiv * (uint64_t)s_speed); }
// The first cycle at which viaAt reaches t (rounded up).
uint64_t cyclesAtVia(uint64_t t) { return s_cycBase + ((t - s_viaBase) * kViaDiv * (uint64_t)s_speed + 3) / 4; }
uint64_t viaNow() { return viaAt(cyclesNow()); }

// The CPU speed setting (the startup's): 1x-4x, or 5 = Max (maxSpeedStep). The P4 can't run a Mac
// II faster than real time, so it is always 1x there. The VIAs stay real time (viaAt), so only the
// 68020 runs faster.
#if defined(ESP_PLATFORM)
constexpr bool kHasSpeed = false;
#else
constexpr bool kHasSpeed = true;
#endif
constexpr int kSpeedMax = 5;             // the setting's "Max"
constexpr int kMaxQuarters = 64;         // Max stops at 16x
// The ROM's start-up tests fail above 4x (the Quadra's: a sad Mac at 8x, its loops time the VIAs):
// Max stays at 4x until the first call to our .Sony (the tests are over), again after each RESET.
constexpr int kStartupQuarters = 16;
bool s_pastStartup = false;
int settingSpeed() {
  if (!kHasSpeed) return 1;
  return g_settings.speed < 1 ? 1 : g_settings.speed > kSpeedMax ? kSpeedMax : g_settings.speed;
}

void setSpeed(int quarters) {
  const uint64_t c = cyclesNow();
  s_viaBase = viaAt(c);
  s_cycBase = c;
  s_speed = quarters < 4 ? 4 : quarters > kMaxQuarters ? kMaxQuarters : quarters;
}

// The setting, at once (Max goes on from the speed it has).
void applySpeedSetting() { setSpeed(settingSpeed() == kSpeedMax ? s_speed : settingSpeed() * 4); }

// Max: the 68020 as fast as this computer or phone allows. The frame's own work (all but the wait
// for the sound) is held near kBusyUs of the 16.6 ms, leaving the rest for the picture and the
// browser. Up a quarter every 4 frames, down at once when the work or the sound queue says so.
void maxSpeedStep(uint32_t busyUs, uint32_t queuedUs) {
  constexpr float kBusyUs = 11500.0f;
  static float avg = 0;
  static uint32_t n = 0;
  if (settingSpeed() != kSpeedMax) { avg = 0; return; }
  if (busyUs > 200000) return;                     // back from the menu, a file dialog...
  avg = avg ? avg + ((float)busyUs - avg) * 0.15f : (float)busyUs;
  int q = s_speed;
  if (queuedUs < 20000 || avg > kBusyUs * 1.05f) q -= q / 16 > 1 ? q / 16 : 1;
  else if (avg < kBusyUs * 0.85f && ++n % 4 == 0) q++;
  if (!s_pastStartup && q > kStartupQuarters) q = kStartupQuarters;
  if (q != s_speed) setSpeed(q);
}

// --- interrupts: VIA1 level 1, VIA2 level 2, the SCC 4 (the Quadra's) ---------------------------
void updateIrq() {
#if defined(MACII_IICI)
  const int level = s_rbv.irqOut() ? 2 : s_via1.irqOut() ? 1 : 0;
#elif defined(MACII_QUADRA)
  const int level = s_scc.irq() ? 4 : s_via2.irqOut() ? 2 : s_via1.irqOut() ? 1 : 0;
#else
  const int level = s_via2.irqOut() ? 2 : s_via1.irqOut() ? 1 : 0;
#endif
  if (level == s_irqLevel) return;
  s_irqLevel = level;
  m68k_set_irq(level);
  if (level) endSlice();                            // Musashi looks at it when a slice starts
}

void viaIrq(void *, bool) { updateIrq(); }

// --- the video card (slot 9) --------------------------------------------------------------------
constexpr uint32_t kVramSize = 512 * 1024;
constexpr int kScreenW = 640, kScreenH = 480;
uint8_t *s_vram = nullptr;
uint32_t s_depth = 0, s_rowBytes = kScreenW / 8, s_base = 0;
uint32_t s_clut[256];               // 0x00RRGGBB
uint8_t s_clutIndex = 0;
bool s_vblEnable = false, s_vblPending = false;
volatile bool s_vramDirty = true;   // the whole picture changed (mode, colours, base)
// The CPU's VRAM writes, per 2 KB block: the drawing job redraws only the rows they touch.
constexpr int kVramBlkShift = 11, kVramBlks = kVramSize >> kVramBlkShift;
volatile uint8_t s_vramBlk[kVramBlks];
volatile bool s_vramAny = false;
uint8_t s_slotRom[slotrom::kSize];   // the declaration ROM, at the top of the slot
uint8_t s_mode = slotrom::kModeFirst;  // the driver's mode (spID 0x80-0x83)
bool s_grayMode = false;


void cardReset() {
  s_depth = 0;
  s_rowBytes = kScreenW / 8;
  s_base = 0;
  for (int i = 0; i < 256; i++) s_clut[i] = 0;
  s_clut[0] = 0xFFFFFF;                             // 1 bit: 0 white, 1 black
  s_clutIndex = 0;
  s_vblEnable = s_vblPending = false;
  s_mode = slotrom::kModeFirst;
  s_grayMode = false;
  s_vramDirty = true;
}

// Slot 9's interrupt: VIA2 port A bit 0 low, and CA1 (any slot) flagged.
// The IIci: the RBV's slot E line, a level, low while the VBL is pending and enabled.
void cardIrq() {
#if defined(MACII_IICI)
  s_rbv.slot(0x20, s_vblEnable && s_vblPending);
#else
  if (s_vblEnable && s_vblPending) s_via2.pulse(Via6522::CA1);
#endif
}

uint32_t cardReg(uint32_t off) {
  switch (off & 0xFC) {
    case 0x00: return s_depth;
    case 0x04: return s_rowBytes;
    case 0x08: return s_base;
    case 0x0C: return s_clutIndex;
    case 0x10: return s_clut[s_clutIndex];
    case 0x14: return s_vblEnable;
    case 0x18: return s_vblPending;
  }
  return 0;
}

void cardWriteReg(uint32_t off, uint32_t v) {
  switch (off & 0xFC) {
    case 0x00: s_depth = v & 3; break;
    case 0x04: s_rowBytes = v & 0xFFFF; break;
    case 0x08: s_base = v & (kVramSize - 1); break;
    case 0x0C: s_clutIndex = (uint8_t)v; return;
    case 0x10: s_clut[s_clutIndex++] = v & 0xFFFFFF; break;
#if defined(MACII_IICI)
    case 0x14: s_vblEnable = v & 1; cardIrq(); return;
    case 0x18: s_vblPending = false; cardIrq(); return;
#else
    case 0x14: s_vblEnable = v & 1; return;
    case 0x18: s_vblPending = false; return;
#endif
    default: return;
  }
  s_vramDirty = true;
}

// --- the bus ------------------------------------------------------------------------------------
// Unmapped accesses: logged once per 512 bytes (the first 256 such blocks), with the PC.
uint32_t s_unmapped = 0;
uint32_t s_seen[256];
int s_seenCount = 0;
void unmapped(const char *what, uint32_t addr, uint32_t phys) {
  s_unmapped++;
  const uint32_t key = phys & ~0x1FFu;            // once per 512 bytes (an I/O register's span)
  for (int i = 0; i < s_seenCount; i++) if (s_seen[i] == key) return;
  if (s_seenCount == 256) return;
  s_seen[s_seenCount++] = key;
  hal_log("macii: unmapped %s %08x (phys %08x) at pc %08x", what, (unsigned)addr, (unsigned)phys,
          (unsigned)m68k_get_reg(nullptr, M68K_REG_PPC));
}

// --- the IWM (floppy controller) at I/O +0x16000 ------------------------------------------------
// Only its registers for now, with no drive: the ROM sets its mode at boot and waits for it. Each
// of the 16 addresses (a 0x200 apart) turns a line off (even) or on (odd): CA0, CA1, CA2, LSTRB,
// ENABLE (motor), SELECT (drive 2), Q6, Q7. Q6/Q7 pick what a read returns: data, status (sense,
// enable, mode), handshake. With Q6 and Q7 on and the motor off, a write sets the mode.
uint8_t s_iwmLines = 0, s_iwmMode = 0;

uint8_t iwmAccess(int reg, bool write, uint8_t v) {
  const uint8_t bit = (uint8_t)(1u << (reg >> 1));
  if (reg & 1) s_iwmLines |= bit; else s_iwmLines &= ~bit;
  const bool q6 = s_iwmLines & 0x40, q7 = s_iwmLines & 0x80, enable = s_iwmLines & 0x10;
  if (write) {
    if (q6 && q7 && !enable && (reg & 1)) s_iwmMode = v & 0x1F;
    return 0;
  }
  if (q6 && !q7) return 0x80 | (enable ? 0x20 : 0) | s_iwmMode;   // sense 1: no drive answers
  if (!q6 && q7) return 0xC0;                                     // handshake: ready, no underrun
  return 0xFF;                                                    // data
}

#if defined(MACII_OPCOUNT)
extern "C" void opCountDump();
#endif
void fastUpdate();

// Reads and writes of 1, 2 or 4 bytes (big-endian). RAM and ROM take the fast path.
template <int N>
inline uint32_t rd(const uint8_t *m) {
  if (N == 1) return m[0];
  if (N == 2) return (uint32_t)m[0] << 8 | m[1];
  return (uint32_t)m[0] << 24 | (uint32_t)m[1] << 16 | (uint32_t)m[2] << 8 | m[3];
}

template <int N>
inline void wr(uint8_t *m, uint32_t v) {
  if (N == 1) { m[0] = (uint8_t)v; return; }
  if (N == 2) { m[0] = (uint8_t)(v >> 8); m[1] = (uint8_t)v; return; }
  m[0] = (uint8_t)(v >> 24); m[1] = (uint8_t)(v >> 16); m[2] = (uint8_t)(v >> 8); m[3] = (uint8_t)v;
}

// Our card in slot kCardSlot: its declaration ROM at the top of the slot, VRAM and registers in
// the first MB, repeated across the slot (32-Bit QuickDraw draws at 0xFs900000).
template <int N>
bool cardRead(uint32_t p, uint32_t *out) {
  if (((p >> 24) & 0xF) != kCardSlot) return false;
  const uint32_t full = p & 0xFFFFFF;
  if (full >= 0x1000000u - slotrom::kSize) {
    uint32_t v = 0;
    for (int i = 0; i < N; i++) v = v << 8 | s_slotRom[(full + i - (0x1000000u - slotrom::kSize)) & (slotrom::kSize - 1)];
    *out = v;
    return true;
  }
  const uint32_t off = full & 0xFFFFF;
  if (off + N <= kVramSize) { *out = rd<N>(s_vram + off); return true; }
  if ((off & ~0xFFu) == 0x80000) {
    *out = N == 4 ? cardReg(off) : N == 2 ? cardReg(off) >> (off & 2 ? 0 : 16) & 0xFFFF : cardReg(off) >> (8 * (3 - (off & 3))) & 0xFF;
    return true;
  }
  return false;
}

template <int N>
bool cardWrite(uint32_t p, uint32_t v) {
  if (((p >> 24) & 0xF) != kCardSlot) return false;
  const uint32_t off = p & 0xFFFFF;
  if (off + N <= kVramSize) { wr<N>(s_vram + off, v); s_vramBlk[off >> kVramBlkShift] = 1; s_vramAny = true; return true; }
  if ((off & ~0xFFu) == 0x80000 && N == 4) { cardWriteReg(off, v); return true; }
  return false;
}

#if defined(MACII_PMMU)
// --- the IIci's and the Quadra's bus: the 68030's PMMU, then the physical map --------------------------------------
// Physical: RAM from 0 (the ROM there while the overlay is on), the ROM at 0x40000000 (512 KB,
// repeated), I/O at 0x50000000 (repeated every 0x40000), the slots at 0xFs000000 (our card in E).
// The PMMU (maciici_mmu.h has its instructions): TC, SRP, CRP, TT0, TT1 and MMUSR; the walk of the
// tables follows the MC68030 manual (and MAME). Simplified: one address space (the TLB does not tell
// supervisor from user), no limits, no bus errors (a fault is logged and reads 0).
#if defined(MACII_QUADRA)
// The 68040's: TC (E 0x8000, P 0x4000 = 8 KB pages), URP, SRP, the four TTs and the MMUSR.
struct Mmu { uint32_t tc, urp, srp, itt0, itt1, dtt0, dtt1, sr; };
#else
struct Mmu { uint64_t srp, crp; uint32_t tc, tt0, tt1; uint16_t sr; };
#endif
Mmu s_mmu;
bool s_mmuOn = false;
uint32_t s_mmuFaults = 0;

void tlbFlush() {
  for (int i = 0; i < MACIICI_TLB; i++) { macii_fast.rtag[i] = macii_fast.wtag[i] = 1; }
  macii_fast.flen = 0;
}
void fastUpdate() { tlbFlush(); }

#if defined(MACII_QUADRA)
// The Quadra 700's I/O at 0x50000000, repeated every 0x40000 (MAME): VIA1 +0, VIA2 +0x2000, the
// Ethernet PROM +0x8000, SONIC +0xA000, SCC +0xC000, the 53C96 SCSI +0xF000, the ASC +0x14000,
// the SWIM +0x1E000. No Ethernet (it reads 0); the SCC only transmits (z8530.h), the 53C96 has no targets.
uint8_t ioRead(uint32_t p) {
  const uint32_t off = p & 0x3FFFF;
  switch (off >> 13) {
    case 0: return s_via1.read((off >> 9) & 15, viaNow());
    case 1: return s_via2.read((off >> 9) & 15, viaNow());
    case 7:
      if ((off & 0x1F00) == 0x1000) {               // the 53C96
        bool irq;
        const uint8_t v = s_scsi.read((off >> 4) & 15, &irq);
        if (irq) s_via2.pulse(Via6522::CB2);
        return v;
      }
      return 0;
    case 6: {
      const uint8_t v = s_scc.read((off >> 1) & 1, (off >> 2) & 1, viaNow());
      updateIrq();
      return v;
    }
    case 4: case 5: return 0;
    case 10: return s_asc.read(off);
    case 15: return iwmAccess((off >> 9) & 15, false, 0);
  }
  unmapped("I/O read", p, p);
  return 0;
}

void ioWrite(uint32_t p, uint8_t v) {
  const uint32_t off = p & 0x3FFFF;
  switch (off >> 13) {
    case 0: s_via1.write((off >> 9) & 15, v, viaNow()); return;
    case 1: s_via2.write((off >> 9) & 15, v, viaNow()); return;
    case 7:
      if ((off & 0x1F00) == 0x1000 && s_scsi.write((off >> 4) & 15, v)) s_via2.pulse(Via6522::CB2);
      return;
    case 6: s_scc.write((off >> 1) & 1, (off >> 2) & 1, v, viaNow()); updateIrq(); return;
    case 4: case 5: return;
    case 10: if (s_asc.write(off, v)) s_via2.pulse(Via6522::CB1); return;
    case 15: iwmAccess((off >> 9) & 15, true, v); return;
  }
  unmapped("I/O write", p, p);
}

// RAM: contiguous from 0 (the memory controller joins the banks). The ROM sizes it by its mirror
// and only a power of two ends that loop: hence 4, 8, 16 and 32 MB.
inline uint8_t *ramHost(uint32_t p) { return p < s_ramSize ? s_ram + p : nullptr; }

// The built-in video (DAFB): its VRAM at 0xF9000000, its registers at 0xF9800000. No monitor on
// it: the sense register (0x1C) reads the inverse of the lines, 0 = sense 7. Every register reads 0.
template <int N>
bool dafbRead(uint32_t p, uint32_t *out) {
  if ((p >> 24) != 0xF9) return false;
  *out = 0;
  return true;
}
#else
uint8_t ioRead(uint32_t p) {
  const uint32_t off = p & 0x3FFFF;
  switch (off >> 13) {
    case 0: return s_via1.read((off >> 9) & 15, viaNow());
    case 8: return s_scsi.read((off >> 4) & 7);
    case 10: return s_asc.read(off);
    case 11: return iwmAccess((off >> 9) & 15, false, 0);
    case 0x12: return 0;                            // the Bt478 (the built-in video's DAC): off
    case 0x13: return s_rbv.read(off);
  }
  unmapped("I/O read", p, p);
  return 0;
}

void ioWrite(uint32_t p, uint8_t v) {
  const uint32_t off = p & 0x3FFFF;
  switch (off >> 13) {
    case 0: s_via1.write((off >> 9) & 15, v, viaNow()); return;
    case 8: s_scsi.write((off >> 4) & 7, v); return;
    case 10: if (s_asc.write(off, v)) s_rbv.asc(); return;
    case 11: iwmAccess((off >> 9) & 15, true, v); return;
    case 0x12: return;
    case 0x13: s_rbv.write(off, v); return;
  }
  unmapped("I/O write", p, p);
}

// The IIci's RAM is two banks of four SIMMs: A at 0, B at 0x04000000, each 64 MB of space with its
// SIMMs mirrored over it (the ROM sizes a bank by that mirror, and its MMU tables then make the two
// look contiguous). 4 MB = A only (1 MB SIMMs), 8 = A + B, 16 = A (4 MB SIMMs), 32 = A + B. Host:
// bank A, then bank B, in s_ram.
uint32_t bankA() { return s_ramSize == (8u << 20) ? 4u << 20 : s_ramSize == (32u << 20) ? 16u << 20 : s_ramSize; }
// The host byte of physical RAM address p (an access never crosses a 4 KB page), or null.
inline uint8_t *ramHost(uint32_t p) {
  const uint32_t a = bankA();
  if (p < 0x04000000u) return a ? s_ram + (p & (a - 1)) : nullptr;
  if (p < 0x08000000u && s_ramSize > a) return s_ram + a + (p & (s_ramSize - a - 1));
  return nullptr;
}
#endif

template <int N>
uint32_t physRead(uint32_t p) {
  switch (p >> 28) {
    case 0: case 1: case 2: case 3:
      if (s_overlay) return rd<N>(s_rom + (p & (kRomSize - 1)));
      if (uint8_t *h = ramHost(p)) return rd<N>(h);
      break;
    case 4:
      if (s_overlay) { s_overlay = false; fastUpdate(); }
      return rd<N>(s_rom + (p & (kRomSize - 1)));
    case 5: {
      uint32_t v = 0;
      for (int i = 0; i < N; i++) v = v << 8 | ioRead(p + i);
      return v;
    }
    case 0xF: {
      uint32_t v;
      if (cardRead<N>(p, &v)) return v;
#if defined(MACII_QUADRA)
      if (dafbRead<N>(p, &v)) return v;
#endif
      break;
    }
  }
  unmapped(N == 1 ? "read8" : N == 2 ? "read16" : "read32", p, p);
  return 0;
}

template <int N>
void physWrite(uint32_t p, uint32_t v) {
  switch (p >> 28) {
    case 0: case 1: case 2: case 3:
      if (uint8_t *h = ramHost(p)) { wr<N>(h, v); return; }
      break;
    case 4:
      if (s_overlay) { s_overlay = false; fastUpdate(); }
      return;
    case 5:
      for (int i = 0; i < N; i++) ioWrite(p + i, (uint8_t)(v >> (8 * (N - 1 - i))));
      return;
    case 0xF:
      if (cardWrite<N>(p, v)) return;
#if defined(MACII_QUADRA)
      if ((p >> 24) == 0xF9) return;                // DAFB
#endif
      break;
  }
  unmapped(N == 1 ? "write8" : N == 2 ? "write16" : "write32", p, p);
}

// A table descriptor's long: RAM or ROM, no side effects.
uint32_t descRead(uint32_t p) {
  if (p >> 28 <= 3 && !s_overlay) { uint8_t *h = ramHost(p); return h ? rd<4>(h) : 0; }
  if (p >> 28 == 4 || (p >> 28 <= 3 && s_overlay)) return rd<4>(s_rom + (p & (kRomSize - 1)));
  return 0;
}
void descWrite(uint32_t p, uint32_t v) {
  if (p >> 28 <= 3 && !s_overlay) if (uint8_t *h = ramHost(p)) wr<4>(h, v);
}

int mmuFc() { return (m68k_get_reg(nullptr, M68K_REG_SR) & 0x2000) ? 5 : 1; }

#if defined(MACII_QUADRA)
// A 68040 transparent translation register: base 31-24, mask 23-16, E 0x8000, S 14-13 (0 user,
// 1 supervisor, 2-3 both), W 0x4 (write protect). The bus does not tell fetches from data: any of
// the four maps.
bool ttMatch(uint32_t tt, uint32_t a, int fc) {
  if (!(tt & 0x8000)) return false;
  const uint32_t ignore = (tt << 8) & 0xFF000000u;
  if (((a ^ tt) & 0xFF000000u & ~ignore) != 0) return false;
  const uint32_t s = (tt >> 13) & 3;
  if (s == 0 && (fc & 4)) return false;
  if (s == 1 && !(fc & 4)) return false;
  return true;
}

// The 68040's table walk (MC68040 manual, 3.2): root (URP/SRP, 512-byte aligned) -> 128 pointer
// descriptors (bits 31-25) -> 128 (24-18) -> the page table: 64 entries (17-12) of 4 KB pages or 32
// (17-13) of 8 KB. UDT 2/3 resident; PDT 1/3 resident, 2 indirect. update: U (and M on a write).
// The MMUSR comes back in *sr (the physical page, G, U1/U0, S, CM, M, W, T, R). false: invalid, a
// write to a write-protected page or a user access to a supervisor page.
bool mmuWalk(uint32_t a, int fc, bool write, bool update, uint32_t *pa, uint32_t *sr) {
  *sr = 0;
  const uint32_t tts[4] = { s_mmu.dtt0, s_mmu.dtt1, s_mmu.itt0, s_mmu.itt1 };
  for (uint32_t tt : tts)
    if (ttMatch(tt, a, fc)) {
      *pa = a;
      *sr = (a & 0xFFFFF000u) | (tt & 0x60) | (tt & 0x4) | 0x3;   // CM, W, T, R
      return !(write && (tt & 4));
    }
  const bool big = (s_mmu.tc & 0x4000) != 0;
  const bool super = (fc & 4) != 0;
  uint32_t tbl = (super ? s_mmu.srp : s_mmu.urp) & ~0x1FFu;
  bool wp = false;
  // The two pointer levels.
  const uint32_t idx[2] = { a >> 25, (a >> 18) & 0x7F };
  for (int lv = 0; lv < 2; lv++) {
    const uint32_t da = tbl + idx[lv] * 4;
    const uint32_t d = descRead(da);
    if ((d & 3) < 2) return false;
    wp |= (d & 4) != 0;
    if (update && !(d & 8)) descWrite(da, d | 8);
    tbl = lv == 0 ? d & ~0x1FFu : big ? d & ~0x7Fu : d & ~0xFFu;
  }
  uint32_t da = tbl + (big ? (a >> 13) & 0x1F : (a >> 12) & 0x3F) * 4;
  uint32_t d = descRead(da);
  if ((d & 3) == 0) return false;
  if ((d & 3) == 2) {                              // indirect
    da = d & ~3u;
    d = descRead(da);
    if ((d & 3) == 0 || (d & 3) == 2) return false;
  }
  wp |= (d & 4) != 0;
  const uint32_t mask = big ? 0x1FFFu : 0xFFFu;
  *pa = (d & ~mask) | (a & mask);
  if ((d & 0x80) && !super) { *sr = (*pa & 0xFFFFF000u) | (d & 0x7E4) | 1; return false; }
  if (update) {
    uint32_t nd = d | 8;
    if (write && !wp) nd |= 0x10;
    if (nd != d) descWrite(da, nd);
    d = nd;
  }
  *sr = (*pa & 0xFFFFF000u) | (d & 0x7F0) | (wp ? 4 : 0) | 1;
  return !(write && wp);
}

// A logical address -> a physical one (0x60000000 + the page offset when there is no mapping: it
// reads 0 and logs). *page: the whole 4 KB page maps alike (the TLB may keep it).
uint32_t translate(uint32_t a, bool write, bool *page) {
  *page = true;
  if (!s_mmuOn) return a;
  uint32_t pa, sr;
  if (!mmuWalk(a, mmuFc(), write, true, &pa, &sr)) {
    if (s_mmuFaults++ < 32)
      hal_log("macquadra: MMU %s fault at %08x (mmusr %08x) pc %08x", write ? "write" : "read", (unsigned)a, (unsigned)sr,
              (unsigned)m68k_get_reg(nullptr, M68K_REG_PPC));
    *page = false;
    return 0x60000000u | (a & 0xFFF);
  }
  return pa;
}
#else
bool ttMatch(uint32_t tt, uint32_t a, int fc, bool write) {
  if (!(tt & 0x8000)) return false;
  const uint32_t ignore = (tt << 8) & 0xFF000000u;
  if (((a ^ tt) & 0xFF000000u & ~ignore) != 0) return false;
  const int fcMask = tt & 7, fcBase = (tt >> 4) & 7;
  if ((fc & ~fcMask) != (fcBase & ~fcMask)) return false;
  if (!(tt & 0x100) && (((tt >> 9) & 1) != (write ? 0u : 1u))) return false;
  return true;
}


// The table walk for a (function code fc). update: set U (and M on a write) as the 68030 does.
// maxLevel: PTEST's level (0 = all). The MMUSR comes back in *sr (W 0x800, I 0x400, M 0x200,
// T 0x40, N = the levels). false: no translation (invalid, or a write to a write-protected page).
bool mmuWalk(uint32_t a, int fc, bool write, bool update, int maxLevel, uint32_t *pa, uint16_t *sr) {
  *sr = 0;
  if (ttMatch(s_mmu.tt0, a, fc, write) || ttMatch(s_mmu.tt1, a, fc, write)) { *pa = a; *sr = 0x40; return true; }
  const uint32_t tc = s_mmu.tc;
  const uint64_t root = ((tc & 0x02000000) && (fc & 4)) ? s_mmu.srp : s_mmu.crp;
  uint32_t dt = (uint32_t)(root >> 32) & 3, tbl = (uint32_t)root & 0xFFFFFFF0u;
  int shift = (tc >> 16) & 15;                      // IS: the top bits ignored
  const int tis[4] = { (int)(tc >> 12) & 15, (int)(tc >> 8) & 15, (int)(tc >> 4) & 15, (int)tc & 15 };
  bool wp = false;
  auto low = [&](int s) { return s >= 32 ? 0u : a & (0xFFFFFFFFu >> s); };
  if (dt == 1) { *pa = ((uint32_t)root & 0xFFFFFF00u) + low(shift); return true; }
  for (int lv = 0; lv < 4; lv++) {
    const int ti = tis[lv];
    if (dt == 0 || ti == 0) { *sr |= 0x0400 | lv; return false; }
    const uint32_t idx = (uint32_t)(((uint64_t)a << shift) >> (32 - ti)) & ((1u << ti) - 1);
    shift += ti;
    const bool isLong = dt == 3;
    const uint32_t da = tbl + idx * (isLong ? 8 : 4);
    const uint32_t d0 = descRead(da);
    const uint32_t addr = isLong ? descRead(da + 4) : d0;
    uint32_t ddt = d0 & 3;
    wp |= (d0 & 4) != 0;
    const bool stop = maxLevel && lv + 1 >= maxLevel;   // PTEST's level reached
    const bool last = lv == 3 || tis[lv + 1] == 0;
    if (ddt == 0) { *sr |= 0x0400 | (lv + 1); return false; }
    uint32_t pda = da, pd0 = d0, paddr = addr;       // the page descriptor (perhaps indirect)
    if (ddt != 1 && last) {
      // An indirect descriptor: it points to the page descriptor.
      pda = addr & ~3u;
      pd0 = descRead(pda);
      paddr = ddt == 3 ? descRead(pda + 4) : pd0;
      if ((pd0 & 3) != 1) { *sr |= 0x0400 | (lv + 1); return false; }
      wp |= (pd0 & 4) != 0;
      ddt = 1;
    }
    if (ddt == 1) {
      if (update) {
        uint32_t nd = pd0 | 0x08;
        if (write && !wp) nd |= 0x10;
        if (nd != pd0) descWrite(pda, nd);
      }
      if (wp) *sr |= 0x0800;
      if ((pd0 & 0x10) || (write && !wp && update)) *sr |= 0x0200;
      *sr |= (uint16_t)(lv + 1);
      *pa = (paddr & 0xFFFFFF00u) + low(shift);
      return !(write && wp);
    }
    if (stop) { *sr |= (uint16_t)(lv + 1); *pa = 0; return true; }
    if (update && !(d0 & 0x08)) descWrite(da, d0 | 0x08);
    tbl = addr & 0xFFFFFFF0u;
    dt = ddt;
  }
  *sr |= 0x0400 | 4;
  return false;
}

// A logical address -> a physical one (0x60000000 + the page offset when there is no mapping: it
// reads 0 and logs). *page: true when the whole 4 KB page maps alike (the TLB may keep it).
uint32_t translate(uint32_t a, bool write, bool *page) {
  *page = true;
  if (!s_mmuOn) return a;
  uint32_t pa;
  uint16_t sr;
  if (!mmuWalk(a, mmuFc(), write, true, 0, &pa, &sr)) {
    if (s_mmuFaults++ < 32)
      hal_log("maciici: MMU %s fault at %08x (mmusr %04x) pc %08x", write ? "write" : "read", (unsigned)a, sr,
              (unsigned)m68k_get_reg(nullptr, M68K_REG_PPC));
    *page = false;
    return 0x60000000u | (a & 0xFFF);
  }
  if (((s_mmu.tc >> 20) & 15) < 12 && !(sr & 0x40)) *page = false;   // pages under 4 KB
  return pa;
}
#endif

// The host bytes of the 4 KB physical page p (page aligned) for the TLB, or null. Writes: RAM only
// (the card's VRAM goes the slow way: busWrite marks what the drawing job redraws).
uint8_t *hostPage(uint32_t p, bool write) {
  if (p >> 28 <= 3) {
    if (s_overlay) return write ? nullptr : s_rom + (p & (kRomSize - 1));
    return ramHost(p);
  }
  if (p >> 28 == 4) return write ? nullptr : s_rom + (p & (kRomSize - 1));
  return nullptr;
}

void tlbFill(uint32_t a, uint32_t p, bool write) {
  uint8_t *h = hostPage(p & ~0xFFFu, write);
  if (!h) return;
  const unsigned i = MACIICI_SLOT(a);
  if (write) {
    macii_fast.wtag[i] = a & ~0xFFFu;
    macii_fast.whost[i] = h;
  }
  macii_fast.rtag[i] = a & ~0xFFFu;
  macii_fast.rhost[i] = h;
}

template <int N>
uint32_t busRead(uint32_t addr) {
  if (N > 1 && (addr & 0xFFF) > 0x1000u - N) {      // across two pages: byte by byte
    uint32_t v = 0;
    for (int i = 0; i < N; i++) v = v << 8 | busRead<1>(addr + i);
    return v;
  }
  bool page;
  const uint32_t p = translate(addr, false, &page);
  const uint32_t v = physRead<N>(p);
  if (page) tlbFill(addr, p, false);
  return v;
}

template <int N>
void busWrite(uint32_t addr, uint32_t v) {
  if (N > 1 && (addr & 0xFFF) > 0x1000u - N) {
    for (int i = 0; i < N; i++) busWrite<1>(addr + i, (v >> (8 * (N - 1 - i))) & 0xFF);
    return;
  }
  bool page;
  const uint32_t p = translate(addr, true, &page);
  physWrite<N>(p, v);
  if (page) tlbFill(addr, p, true);
}

// The fetch window: the cached page the PC is in (busRead has just filled it).
void fetchWindow(uint32_t a) {
  macii_fast.flen = 0;
  const unsigned i = MACIICI_SLOT(a);
  if (macii_fast.rtag[i] != (a & ~0xFFFu)) return;
  macii_fast.fbase = macii_fast.rhost[i];
  macii_fast.flo = a & ~0xFFFu;
  macii_fast.flen = 0xFFD;
}

// A logical address as the CPU sees it now, without touching the U/M bits (the debugger, gHost).
uint32_t phys(uint32_t a) {
  if (!s_mmuOn) return a;
  uint32_t pa;
#if defined(MACII_QUADRA)
  uint32_t sr;
  return mmuWalk(a, mmuFc(), false, false, &pa, &sr) ? pa : 0x60000000u;
#else
  uint16_t sr;
  return mmuWalk(a, mmuFc(), false, false, 0, &pa, &sr) ? pa : 0x60000000u;
#endif
}

// A RAM pointer for [a, a+len) as the CPU sees it, or null (not all in RAM, or not contiguous).
uint8_t *gHost(uint32_t a, uint32_t len) {
  if (s_overlay || len == 0) return nullptr;
  const uint32_t p = phys(a);
  uint8_t *h = (p >> 28) <= 3 ? ramHost(p) : nullptr;
  if (!h || h + len > s_ram + s_ramSize) return nullptr;
  for (uint32_t pg = (a & ~0xFFFu) + 0x1000; pg - a < len; pg += 0x1000) {
    const uint32_t q = phys(pg);
    if ((q >> 28) > 3 || ramHost(q) != h + (pg - a)) return nullptr;
  }
  return h;
}
#else
// A CPU address -> a physical (32-bit) one.
inline uint32_t phys(uint32_t a) {
  if (s_addr32) return a;
  a &= 0xFFFFFF;
  if (a < 0x800000) return a;
  if (a < 0x900000) return 0x40000000u | (a & 0xFFFFF);
  if (a >= 0xF00000) return 0x50F00000u | (a & 0xFFFFF);
  return 0xF0000000u | ((a >> 20) << 24) | (a & 0xFFFFF);
}

uint8_t ioRead(uint32_t p) {
  const uint32_t off = p & 0x1FFFF;
  switch (off >> 13) {
    case 0: return s_via1.read((off >> 9) & 15, viaNow());
    case 1: return s_via2.read((off >> 9) & 15, viaNow());
    case 8: return s_scsi.read((off >> 4) & 7);
    case 10: return s_asc.read(off);
    case 11: return iwmAccess((off >> 9) & 15, false, 0);
  }
  unmapped("I/O read", p, p);
  return 0;
}

void ioWrite(uint32_t p, uint8_t v) {
  const uint32_t off = p & 0x1FFFF;
  switch (off >> 13) {
    case 0: s_via1.write((off >> 9) & 15, v, viaNow()); return;
    case 1: s_via2.write((off >> 9) & 15, v, viaNow()); return;
    case 8: s_scsi.write((off >> 4) & 7, v); return;
    case 10:
#if defined(BOARD_DESKTOP)
      {  // MACII_ASC=1 (test hook): the ASC's register writes, and a count of the FIFO/wave bytes
        static const bool log = getenv("MACII_ASC") != nullptr;
        static uint32_t bytes = 0;
        if (log) {
          if ((off & 0xFFF) < 0x800) bytes++;
          else { hal_log("asc: w %03x=%02x (%u data bytes before) pc %08x", (unsigned)(off & 0xFFF), v, (unsigned)bytes, (unsigned)m68k_get_reg(nullptr, M68K_REG_PPC)); bytes = 0; }
        }
      }
#endif
      if (s_asc.write(off, v)) s_via2.pulse(Via6522::CB1);
      return;
    case 11: iwmAccess((off >> 9) & 15, true, v); return;
  }
  unmapped("I/O write", p, p);
}

template <int N>
uint32_t busRead(uint32_t addr) {
  const uint32_t p = phys(addr);
  switch (p >> 28) {
    case 0: case 1: case 2: case 3:
      if (s_overlay) return rd<N>(s_rom + (p & (kRomSize - 1)));
      if (p + N <= s_ramSize) return rd<N>(s_ram + p);
      break;
    case 4:
      if (s_overlay) { s_overlay = false; fastUpdate(); }
      return rd<N>(s_rom + (p & (kRomSize - 1)));
    case 5: {
      // The devices are 8 bits wide: the 68020's dynamic bus sizing makes a word or long access
      // N byte accesses at consecutive addresses (the ASC's 32-bit wave increments need this).
      uint32_t v = 0;
      for (int i = 0; i < N; i++) v = v << 8 | ioRead(p + i);
      return v;
    }
    case 0xF: {
      uint32_t v;
      if (cardRead<N>(p, &v)) return v;
      break;
    }
  }
  unmapped(N == 1 ? "read8" : N == 2 ? "read16" : "read32", addr, p);
  return 0;
}

template <int N>
void busWrite(uint32_t addr, uint32_t v) {
  const uint32_t p = phys(addr);
  switch (p >> 28) {
    case 0: case 1: case 2: case 3:
      if (p + N <= s_ramSize) { wr<N>(s_ram + p, v); return; }
      break;
    case 4:   // ROM
      if (s_overlay) { s_overlay = false; fastUpdate(); }
      // 8 MB fills the 24-bit RAM space, and a write past its end wraps to 0: the ROM's start-up
      // test checks that (a write at 0x800000 must show at 0) and stops in its test loop otherwise.
      if (!s_addr32 && s_ramSize == 0x800000u && (addr & 0xF00000u) == 0x800000u) wr<N>(s_ram + (addr & 0xFFFFFu), v);
      return;
    case 5:
      for (int i = 0; i < N; i++) ioWrite(p + i, (uint8_t)(v >> (8 * (N - 1 - i))));   // dynamic bus sizing
      return;
    case 0xF:
      if (cardWrite<N>(p, v)) return;
      break;
  }
  unmapped(N == 1 ? "write8" : N == 2 ? "write16" : "write32", addr, p);
}

// The fast path (macii_bus.h): RAM without a call while there is no overlay, and a fetch window
// over the RAM or the ROM block the PC is in. Every change of s_overlay, s_addr32 or s_ram calls this.
void fastUpdate() {
  const uint32_t ram = s_ram ? (s_addr32 ? s_ramSize : std::min<uint32_t>(s_ramSize, 0x800000u)) : 0;
  macii_fast.ram = s_ram;
  macii_fast.mask = s_addr32 ? 0xFFFFFFFFu : 0xFFFFFFu;
  macii_fast.limit = !s_overlay && ram >= 4 ? ram - 3 : 0;
  macii_fast.flen = 0;
}

void fetchWindow(uint32_t a) {
  macii_fast.flen = 0;
  if (s_overlay) return;
  const uint32_t p = phys(a);
  if (p >> 28 <= 3 && macii_fast.limit && p < macii_fast.limit) {
    macii_fast.fbase = s_ram;
    macii_fast.flo = a - p;
    macii_fast.flen = macii_fast.limit;
  } else if (p >> 28 == 4) {
    macii_fast.fbase = s_rom;
    macii_fast.flo = a & ~(kRomSize - 1);
    macii_fast.flen = kRomSize - 3;
  }
}

// A RAM pointer for [a, a+len) as the CPU sees it, or null (not all in RAM, or not contiguous).
uint8_t *gHost(uint32_t a, uint32_t len) {
  const uint32_t p = phys(a), q = phys(a + len - 1);
  if (s_overlay || len == 0 || q - p != len - 1 || (p >> 28) > 3 || q >= s_ramSize) return nullptr;
  return s_ram + p;
}

#endif   // MACII_PMMU

// --- the card's driver (macii_slotrom.h): Open, Control, Status, Close and the VBL ack ----------
// Guest memory as the CPU sees it (the driver passes addresses valid in the current mode).
uint32_t gRead8(uint32_t a) { return busRead<1>(a); }
uint32_t gRead16(uint32_t a) { return busRead<2>(a); }
uint32_t gRead32(uint32_t a) { return busRead<4>(a); }
void gWrite8(uint32_t a, uint32_t v) { busWrite<1>(a, v); }
void gWrite16(uint32_t a, uint32_t v) { busWrite<2>(a, v); }
void gWrite32(uint32_t a, uint32_t v) { busWrite<4>(a, v); }
constexpr int16_t kNoErr = 0, kControlErr = -17, kStatusErr = -18;

// Sets the card to a mode (spID); false if the mode is not one of ours.
bool setMode(uint8_t mode) {
  if (mode < slotrom::kModeFirst || mode > slotrom::kModeFirst + 3) return false;
  s_mode = mode;
  s_depth = mode - slotrom::kModeFirst;
  s_rowBytes = (uint32_t)(kScreenW << s_depth) / 8;
  s_base = 0;
  s_vramDirty = true;
  return true;
}

// 50% grey: alternate pixels of index 0 and the last index, shifted on odd rows.
void grayPage() {
  static const uint32_t pat[4] = { 0xAAAAAAAAu, 0xCCCCCCCCu, 0xF0F0F0F0u, 0xFF00FF00u };
  const uint32_t p = pat[s_depth & 3];
  for (int y = 0; y < kScreenH; y++) {
    const uint32_t v = (y & 1) ? ~p : p;
    uint8_t *row = s_vram + s_base + (uint32_t)y * s_rowBytes;
    for (uint32_t x = 0; x < s_rowBytes; x += 4) wr<4>(row + x, v);
  }
  s_vramDirty = true;
}

// SetEntries / GetEntries: a ColorSpec table (value, r, g, b words); start -1 = by value.
void entries(uint32_t param, bool set) {
  const uint32_t table = gRead32(param);
  const int start = (int16_t)gRead16(param + 4), count = (int16_t)gRead16(param + 6);
  const int max = (1 << (1 << s_depth)) - 1;
  for (int i = 0; i <= count; i++) {
    const uint32_t cs = table + 8u * (uint32_t)i;
    const int idx = start == -1 ? (int)(gRead16(cs) & 0xFF) : start + i;
    if (idx > max) continue;
    if (set) {
      uint32_t r = gRead16(cs + 2) >> 8, g = gRead16(cs + 4) >> 8, b = gRead16(cs + 6) >> 8;
      if (s_grayMode) { const uint32_t l = (r * 30 + g * 59 + b * 11) / 100; r = g = b = l; }
      s_clut[idx] = r << 16 | g << 8 | b;
    } else {
      const uint32_t c = s_clut[idx];
      if (start != -1) gWrite16(cs, (uint32_t)idx);
      gWrite16(cs + 2, (c >> 16 & 0xFF) * 0x101);
      gWrite16(cs + 4, (c >> 8 & 0xFF) * 0x101);
      gWrite16(cs + 6, (c & 0xFF) * 0x101);
    }
  }
  s_vramDirty = true;
}

int16_t driverControl(uint32_t pb, uint32_t dce) {
  const int code = (int16_t)gRead16(pb + 0x1A);
  const uint32_t param = gRead32(pb + 0x1C);
  const uint32_t base = gRead32(dce + 0x2A);         // dCtlDevBase
  switch (code) {
    case 0:                                          // VidReset
      setMode(slotrom::kModeFirst);
      gWrite16(param, s_mode); gWrite16(param + 6, 0); gWrite32(param + 8, base);
      return kNoErr;
    case 1: return kNoErr;                           // KillIO
    case 2:                                          // SetMode
      if (!setMode((uint8_t)gRead16(param)) || gRead16(param + 6) != 0) return kControlErr;
      gWrite32(param + 8, base);
      return kNoErr;
    case 3: entries(param, true); return kNoErr;     // SetEntries
    case 4: return kNoErr;                           // SetGamma
    case 5: grayPage(); return kNoErr;               // GrayPage
    case 6: s_grayMode = gRead8(param) != 0; return kNoErr;   // SetGray
    case 7: s_vblEnable = gRead8(param) == 0; cardIrq(); return kNoErr;  // SetInterrupt (0 = on)
    case 9: return kNoErr;                           // SetDefaultMode
  }
  return kControlErr;
}

int16_t driverStatus(uint32_t pb, uint32_t dce) {
  const int code = (int16_t)gRead16(pb + 0x1A);
  const uint32_t param = gRead32(pb + 0x1C);
  const uint32_t base = gRead32(dce + 0x2A);
  switch (code) {
    case 2: gWrite16(param, s_mode); gWrite16(param + 6, 0); gWrite32(param + 8, base); return kNoErr;   // GetMode
    case 3: entries(param, false); return kNoErr;    // GetEntries
    case 4: gWrite16(param + 6, 1); return kNoErr;   // GetPages
    case 5:                                          // GetBaseAddr
      if (gRead16(param + 6) != 0) return kStatusErr;
      gWrite32(param + 8, base);
      return kNoErr;
    case 6: gWrite8(param, s_grayMode ? 1 : 0); return kNoErr;     // GetGray
    case 7: gWrite8(param, s_vblEnable ? 0 : 1); return kNoErr;    // GetInterrupt
    case 9: gWrite8(param, slotrom::kModeFirst); return kNoErr;    // GetDefaultMode
  }
  return kStatusErr;
}

// An opcode 0x71xx from the driver. False: not ours (a real illegal instruction).
bool driverOp(uint16_t op) {
  const uint32_t a0 = m68k_get_reg(nullptr, M68K_REG_A0), a1 = m68k_get_reg(nullptr, M68K_REG_A1);
  int32_t d0 = 0;
  switch (op) {
    case slotrom::kEmulOpen:
      setMode(slotrom::kModeFirst);
      s_vblEnable = true;
      cardIrq();
      hal_log("macii: video driver open, DCE %08x, base %08x", (unsigned)a1, (unsigned)gRead32(a1 + 0x2A));
      break;
    case slotrom::kEmulControl: d0 = driverControl(a0, a1); break;
    case slotrom::kEmulStatus: d0 = driverStatus(a0, a1); break;
    case slotrom::kEmulClose: s_vblEnable = false; cardIrq(); break;
    case slotrom::kEmulVblAck: s_vblPending = false; cardIrq(); return true;   // the IIci's line is a level
    default: return false;
  }
  m68k_set_reg(M68K_REG_D0, (uint32_t)d0);
  return true;
}

// --- the clock and XPRAM: <sd>/picomac/pram2.bin = 256 XPRAM bytes + the clock (LE) ------------
MacRtc s_rtc;
constexpr uint32_t kMacEpochOffset = 2082844800u;   // 1904-01-01 to 1970-01-01, in seconds

#if defined(MACII_QUADRA)
std::string pramPath() { return sdPath("picomac/pramq700.bin"); }
#elif defined(MACII_IICI)
std::string pramPath() { return sdPath("picomac/pramci.bin"); }
#else
std::string pramPath() { return sdPath("picomac/pram2.bin"); }
#endif

// The host's time in the Mac's terms (local time, from 1904), when it has a clock (as the Plus core).
bool hostClock(uint32_t *secs) {
  const time_t now = time(nullptr);
  if (now <= 1577836800) return false;              // before 2020: no real clock
  struct tm g = *gmtime(&now);
  g.tm_isdst = -1;
  const long off = (long)(now - mktime(&g));
  *secs = (uint32_t)(now + off) + kMacEpochOffset;
  return true;
}

// No file: what the ROM writes when it zaps PRAM (its "NuMc" signature at 0x0C, the old PRAM at
// 0x10), but with SPConfig 0x22 (both serial ports async). The ROM's own zap also puts its defaults
// straight into low memory, SPConfig 0 included, and that boot would hang in AppleTalk (rtc.h).
// Slot 9's PRAM (XPRAM 0x46: the board ID, then the mode the screen starts in, spID 0x80-0x83 =
// 1-8 bits). The Monitors control panel would change it; System 7.5.5 from the Mini vMac bundle
// has none, so the menu's Colors row does (the Mac restarts with it).
constexpr int kSlotPram = 0x46 + 8 * (int)(kCardSlot - 9);   // the card's 8 bytes of slot PRAM
constexpr int kColorsByte = kSlotPram + 2;

void loadPram() {
  uint8_t b[260] = {};
  FILE *f = fopen(pramPath().c_str(), "rb");
  const bool have = f && fread(b, 1, sizeof b, f) == sizeof b;
  if (f) fclose(f);
  if (!have) {
    static const uint8_t kZap[32] = {
      0x00, 0x00, 0x4F, 0x48, 0x00, 0x00, 0x00, 0x00, 0x03, 0x88, 0x00, 0x4C, 'N', 'u', 'M', 'c',
      0xA8, 0x00, 0x00, 0x22, 0xCC, 0x0A, 0xCC, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x63, 0x00,
    };
    memset(b, 0, sizeof b);
    memcpy(b, kZap, sizeof kZap);
    b[kSlotPram] = 0x01; b[kSlotPram + 1] = 0x23; b[kColorsByte] = 0x83;   // our board, 256 colours
    b[0x77] = 0x01; b[0x78] = 0xFF; b[0x79] = 0xFF; b[0x7A] = 0xFF; b[0x7B] = 0xDF;
  }
  memcpy(s_rtc.xpram, b, 256);
  uint32_t secs = have ? (uint32_t)b[256] | b[257] << 8 | b[258] << 16 | (uint32_t)b[259] << 24 : 0;
  hostClock(&secs);
  if (secs < kMacEpochOffset) secs = kMacEpochOffset + 1735689600u;   // 2025-01-01
  s_rtc.seconds = secs;
  s_rtc.dirty = false;
}

void savePram() {
  uint8_t b[260];
  memcpy(b, s_rtc.xpram, 256);
  const uint32_t t = s_rtc.seconds;
  b[256] = t; b[257] = t >> 8; b[258] = t >> 16; b[259] = t >> 24;
  makeDirs(sdPath("picomac"));
  if (FILE *f = fopen(pramPath().c_str(), "wb")) { fwrite(b, 1, sizeof b, f); fclose(f); }
  s_rtc.dirty = false;
}

// --- VIA ports ----------------------------------------------------------------------------------
// VIA1 port A inputs: as MAME's Mac II, 0x81. PA6 low = a Mac II (high = IIcx/SE/30, whose IWM
// the ROM then looks for at +0x1C000), PA0 high for the SWIM.
#if defined(MACII_QUADRA)
uint8_t via1InA(void *) { return 0xC1; }          // MAME's Quadra 700
#elif defined(MACII_IICI)
uint8_t via1InA(void *) { return 0xC7; }          // MAME's IIci
#else
uint8_t via1InA(void *) { return 0x81; }
#endif

// VIA1 port B: PB3 = ADB /INT (input), PB4/PB5 = the ADB state (outputs), PB0-2 the RTC (rtc.h).
uint8_t via1InB(void *) {
  const uint8_t v = s_adb.intLine() ? 0xFF : 0xF7;
  return s_rtc.dataOut() ? v : (uint8_t)(v & ~1);
}
void via1OutB(void *, uint8_t v) {
  s_rtc.portB(v);
#if defined(BOARD_DESKTOP)
  { static int last = -1; static const bool tr = getenv("MACII_ADB") != nullptr;   // test hook: ADB states
    const int st = (v >> 4) & 3;
    if (tr && st != last) hal_log("adb: st %d sr %02x acr %02x pc %08x", st, s_via1.shiftReg(), s_via1.acr(), (unsigned)m68k_get_reg(nullptr, M68K_REG_PPC));
    last = st; }
#endif
  if (s_adb.setState((v >> 4) & 3, (s_via1.acr() & 0x10) != 0, s_via1.shiftReg())) {
    s_adbAt = viaNow() + AdbBus::kByteTicks;
    endSlice();                                     // so that run() sees the new event
  }
}

// The ADB byte in progress is done at VIA time `t`: the shift register's interrupt.
void adbTick(uint64_t t) {
  if (t < s_adbAt) return;
  s_adbAt = UINT64_MAX;
  const uint8_t b = s_adb.done();
#if defined(BOARD_DESKTOP)
  { static const bool tr = getenv("MACII_ADB") != nullptr;
    if (tr) hal_log("adb: done %02x int %d", b, s_adb.intLine()); }
#endif
  if (s_via1.acr() & 0x10) s_via1.pulse(Via6522::SRF);
  else s_via1.shiftIn(b);
}

#if !defined(MACII_IICI)
// VIA2 port A: the slot interrupts, active low (bit 0 = slot 9 ... bit 5 = slot E; the Quadra's
// DAFB is bit 6).
uint8_t via2InA(void *) { return (s_vblEnable && s_vblPending) ? (uint8_t)~(1u << (kCardSlot - 9)) : 0xFF; }
#if defined(MACII_QUADRA)
// The Quadra: VIA2 port B reads 0xCF (MAME); no 24-bit HMMU, the 68040's MMU does 24-bit mode.
uint8_t via2InB(void *) { return 0xCF; }
void via2OutB(void *, uint8_t) {}
#else

// VIA2 port B: bit 3 = 32-bit mode, bit 2 = power off (active low). System 7 switches modes all
// the time (SwapMMUMode), so no log here: on the P4 each line costs serial time.
void via2OutB(void *, uint8_t v) {
  const bool a32 = (v & 0x08) != 0;
  if (a32 != s_addr32) { s_addr32 = a32; fastUpdate(); }
}
#endif

#endif

// --- video --------------------------------------------------------------------------------------
// Fit: the 640x480 screen at 320x240, each pixel the average of 2x2. 1:1 pan: a window of it
// that follows the pointer (Ctrl+Caps Lock or the menu's Screen row). The Plus's Native screen
// means Fit here.
constexpr int kFitW = 320, kFitH = 240, kFitY = (SCREEN_H - kStripH - kFitH) / 2;
bool s_redraw = true;
int s_wholeLeft = 0;                 // whole pictures still owed (drawJob: one per buffer)
int s_screenLive = SCREEN_FIT;       // SCREEN_FIT or SCREEN_PAN
int s_panX = 0, s_panY = 0;
uint32_t s_noteUntil = 0;
volatile bool s_stripUsed = false;

// One screen row as colour indices.
void rowIndices(const uint8_t *src, int depth, uint8_t *out) {
  switch (depth) {
    case 3: memcpy(out, src, kScreenW); break;
    case 2: for (int x = 0; x < kScreenW; x += 2) { const uint8_t b = *src++; out[x] = b >> 4; out[x + 1] = b & 15; } break;
    case 1:
      for (int x = 0; x < kScreenW; x += 4) {
        const uint8_t b = *src++;
        out[x] = b >> 6; out[x + 1] = (b >> 4) & 3; out[x + 2] = (b >> 2) & 3; out[x + 3] = b & 3;
      }
      break;
    default:
      for (int x = 0; x < kScreenW; x += 8) {
        const uint8_t b = *src++;
        for (int i = 0; i < 8; i++) out[x + i] = (b >> (7 - i)) & 1;
      }
      break;
  }
}

uint16_t rgb565(uint32_t c) { return (uint16_t)(((c >> 19) & 31) << 11 | ((c >> 10) & 63) << 5 | ((c >> 3) & 31)); }

// One pixel's colour (0x00RRGGBB): the snapshot's picture.
uint32_t screenPixel(int x, int y) {
  const uint8_t *row = s_vram + s_base + (uint32_t)y * s_rowBytes;
  int i;
  switch (s_depth) {
    case 3: i = row[x]; break;
    case 2: i = (row[x >> 1] >> ((~x & 1) * 4)) & 15; break;
    case 1: i = (row[x >> 2] >> ((3 - (x & 3)) * 2)) & 3; break;
    default: i = (row[x >> 3] >> (7 - (x & 7))) & 1; break;
  }
  return s_clut[i];
}

// 640x480 -> 320x240: each output pixel averages 2x2. use: the VRAM blocks to redraw (null: all);
// [*top, *end) are the output rows drawn.
void drawFit(uint16_t *out, const uint8_t *use, int *top, int *end) {
  static uint8_t a[kScreenW], b[kScreenW];
  *top = kFitH; *end = 0;
  for (int y = 0; y < kFitH; y++) {
    const uint32_t r0 = s_base + (uint32_t)(2 * y) * s_rowBytes, r1 = r0 + s_rowBytes;
    if (use) {   // a write may start up to 3 bytes before the row
      bool hit = false;
      const uint32_t last = std::min<uint32_t>((r1 + s_rowBytes - 1) >> kVramBlkShift, kVramBlks - 1);
      for (uint32_t k = (r0 >= 3 ? r0 - 3 : 0) >> kVramBlkShift; k <= last && !hit; k++) hit = use[k];
      if (!hit) continue;
    }
    if (y < *top) *top = y;
    *end = y + 1;
    if (r1 + s_rowBytes > kVramSize) { memset(out + (kFitY + y) * SCREEN_W, 0, kFitW * 2); continue; }
    rowIndices(s_vram + r0, (int)s_depth, a);
    rowIndices(s_vram + r1, (int)s_depth, b);
    uint16_t *o = out + (kFitY + y) * SCREEN_W;
    for (int x = 0; x < kFitW; x++) {
      const uint32_t c0 = s_clut[a[2 * x]], c1 = s_clut[a[2 * x + 1]], c2 = s_clut[b[2 * x]], c3 = s_clut[b[2 * x + 1]];
      const uint32_t r = ((c0 >> 16 & 255) + (c1 >> 16 & 255) + (c2 >> 16 & 255) + (c3 >> 16 & 255)) >> 2;
      const uint32_t g = ((c0 >> 8 & 255) + (c1 >> 8 & 255) + (c2 >> 8 & 255) + (c3 >> 8 & 255)) >> 2;
      const uint32_t bl = ((c0 & 255) + (c1 & 255) + (c2 & 255) + (c3 & 255)) >> 2;
      o[x] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (bl >> 3));
    }
  }
}

// A SCREEN_W x h window of the screen at (x0, y0), 1:1.
void drawPan(int x0, int y0, int h, uint16_t *out) {
  static uint8_t idx[kScreenW];
  static uint16_t pal[256];
  const int colours = 1 << (1 << s_depth);
  for (int i = 0; i < colours; i++) pal[i] = rgb565(s_clut[i]);
  for (int y = 0; y < h; y++) {
    uint16_t *o = out + y * SCREEN_W;
    const uint32_t r = s_base + (uint32_t)(y0 + y) * s_rowBytes;
    if (y0 + y >= kScreenH || r + s_rowBytes > kVramSize) { memset(o, 0, SCREEN_W * 2); continue; }
    rowIndices(s_vram + r, (int)s_depth, idx);
    for (int x = 0; x < SCREEN_W; x++) o[x] = pal[idx[x0 + x]];
  }
}

// Keeps the pointer (the Mouse global at 0x830: v, h) at least 48 pixels inside the window.
void followPointer(int visH) {
  const int my = (int16_t)(s_ram[0x830] << 8 | s_ram[0x831]);
  const int mx = (int16_t)(s_ram[0x832] << 8 | s_ram[0x833]);
  const int m = 48;
  if (mx < s_panX + m) s_panX = mx - m;
  if (mx > s_panX + SCREEN_W - m) s_panX = mx - SCREEN_W + m;
  if (my < s_panY + m) s_panY = my - m;
  if (my > s_panY + visH - m) s_panY = my - visH + m;
  if (s_panX > kScreenW - SCREEN_W) s_panX = kScreenW - SCREEN_W;
  if (s_panY > kScreenH - visH) s_panY = kScreenH - visH;
  if (s_panX < 0) s_panX = 0;
  if (s_panY < 0) s_panY = 0;
}

#if defined(EMU_WEB)
// The page shows the Mac's own 640x480 picture, through the CLUT; the panel is never seen.
void webFrame() {
  static uint16_t *pic = nullptr;
  if (!pic) pic = (uint16_t *)hal_alloc_big(kScreenW * kScreenH * 2);
  if (!pic) return;
  static uint8_t idx[kScreenW];
  uint16_t pal[256];
  for (int i = 0; i < 256; i++) pal[i] = rgb565(s_clut[i]);
  for (int y = 0; y < kScreenH; y++) {
    const uint32_t r = s_base + (uint32_t)y * s_rowBytes;
    if (r + s_rowBytes > kVramSize) { memset(pic + y * kScreenW, 0, (kScreenH - y) * kScreenW * 2); break; }
    rowIndices(s_vram + r, (int)s_depth, idx);
    for (int x = 0; x < kScreenW; x++) pic[y * kScreenW + x] = pal[idx[x]];
  }
  hal_web_pc_frame(pic, kScreenW, kScreenH, kScreenW);
}
#endif

// The drawing job (core 0 on the P4).
void drawJob(void *) {
  if (!hal_video_ready()) return;
  const int visH = s_stripUsed ? SCREEN_H - kStripH : SCREEN_H;
  const int px = s_panX, py = s_panY;
  if (s_screenLive == SCREEN_PAN && s_ram && !s_overlay) followPointer(visH);
  // The blocks written since the last picture; this buffer also lacks the last picture's (two buffers).
  static uint8_t cur[kVramBlks], prev[kVramBlks], use[kVramBlks];
  static bool prevAny = false, prevAll = true;
  bool curAny = false;
  if (s_vramAny) {
    s_vramAny = false;
    for (int k = 0; k < kVramBlks; k++)   // read, then clear what was set: a later write stays for the next
      if (s_vramBlk[k]) { s_vramBlk[k] = 0; cur[k] = 1; curAny = true; } else cur[k] = 0;
  }
  const bool curAll = s_vramDirty;
  if (!s_redraw && !s_wholeLeft && !curAll && !curAny && !prevAny && !prevAll && px == s_panX && py == s_panY) return;
  s_vramDirty = false;
#if defined(EMU_WEB)
  prevAny = prevAll = false;
  s_redraw = false;
  s_wholeLeft = 0;
  webFrame();
  return;
#endif
  const bool all = curAll || prevAll;
  if (!all) for (int k = 0; k < kVramBlks; k++) use[k] = (curAny && cur[k]) || (prevAny && prev[k]);
  if (curAny) memcpy(prev, cur, sizeof prev);
  prevAny = curAny;
  prevAll = curAll;
  // A whole picture goes to both buffers in turn (the menu shows the front one as its backdrop).
  if (s_redraw) s_wholeLeft = 2;
  const bool whole = s_wholeLeft > 0;
  if (whole) s_wholeLeft--;
  s_redraw = false;
  uint16_t *back = hal_video_back();
  if (whole && visH < SCREEN_H) memset(back + visH * SCREEN_W, 0, (SCREEN_H - visH) * SCREEN_W * 2);
  if (s_screenLive == SCREEN_PAN) {
    drawPan(s_panX, s_panY, visH, back);
    hal_video_submit(0, visH);
    return;
  }
  int top, end;
  drawFit(back, whole || all ? nullptr : use, &top, &end);
  if (whole) {
    memset(back, 0, kFitY * SCREEN_W * 2);
    memset(back + (kFitY + kFitH) * SCREEN_W, 0, (visH - kFitY - kFitH) * SCREEN_W * 2);
    hal_video_submit(0, visH);
  } else if (end > top) {
    hal_video_submit(kFitY + top, end - top);
  }
}

// A line of text in the bottom strip for 2 s; null clears the strip.
void showNote(const char *text) {
#if defined(EMU_WEB)
  hal_web_status(0, text);
#endif
  const int y = PANEL_H - kStripH;
  ui::fill(0, y, PANEL_W, kStripH, 0);
  if (text) ui::text(PANEL_W / 2, y + 5, text, ui::kAccent, 1, ui::MC);
  hal_job_wait();
  hal_video_wait();
  hal_panel_push(0, y, PANEL_W, kStripH, ui::canvas() + y * PANEL_W, PANEL_W);
  s_noteUntil = text ? (hal_millis() + 2000) | 1 : 0;
  s_stripUsed = text || g_settings.showFps;
  s_redraw = true;
}

// --- power --------------------------------------------------------------------------------------
uint32_t romChecksum() { return (uint32_t)s_rom[0] << 24 | s_rom[1] << 16 | s_rom[2] << 8 | s_rom[3]; }

void machineReset() {
  memset(s_ram, 0, s_ramSize);
  s_pastStartup = false;
  s_overlay = true;
  s_addr32 = false;
  fastUpdate();
  s_cycles = 0;
  s_viaBase = s_cycBase = 0;
  s_irqLevel = 0;
  s_unmapped = 0;
  s_seenCount = 0;
  s_iwmLines = s_iwmMode = 0;
  cardReset();
  s_sony.reset();
  s_rtc.reset();
  s_adb.reset();
  s_scsi.reset();
#if defined(MACII_QUADRA)
  s_scc.reset();
#endif
  s_asc.reset();
  s_adbAt = UINT64_MAX;
  s_via1.reset();
#if defined(MACII_PMMU)
#if defined(MACII_IICI)
  s_rbv.reset();
#else
  s_via2.reset();
#endif
  memset(&s_mmu, 0, sizeof s_mmu);
  s_mmuOn = false;
  s_mmuFaults = 0;
  tlbFlush();
  m68k_init();
#if defined(MACII_QUADRA)
  m68k_set_cpu_type(M68K_CPU_TYPE_68040);
#else
  m68k_set_cpu_type(M68K_CPU_TYPE_68030);
#endif
#else
  s_via2.reset();
  m68k_init();
  m68k_set_cpu_type(M68K_CPU_TYPE_68020);
#endif
  m68k_set_irq(0);
  m68k_pulse_reset();
  hal_log("macii: reset, pc %08x sp %08x", (unsigned)m68k_get_reg(nullptr, M68K_REG_PC),
          (unsigned)m68k_get_reg(nullptr, M68K_REG_SP));
}

bool powerOn() {
  const std::string romPath = sdPath(g_romPath.c_str());
  FILE *f = fopen(romPath.c_str(), "rb");
  if (!f) { snprintf(s_error, sizeof s_error, "No ROM: put it at /%s", g_romPath.c_str()); return false; }
  const size_t got = fread(s_rom, 1, kRomSize, f);
  fclose(f);
  const uint32_t sum = romChecksum();
#if defined(MACII_QUADRA)
  if (got != kRomSize) { snprintf(s_error, sizeof s_error, "The ROM must be 1 MB (a Quadra 700 ROM)"); return false; }
  const bool known = sum == 0x420DBFF3;
  hal_log("macquadra: ROM %08x%s", (unsigned)sum, known ? "" : " (not the Quadra 700 ROM I know; trying it)");
#elif defined(MACII_IICI)
  if (got != kRomSize) { snprintf(s_error, sizeof s_error, "The ROM must be 512 KB (a Mac IIci ROM)"); return false; }
  const bool known = sum == 0x368CADFE;
  hal_log("maciici: ROM %08x%s", (unsigned)sum, known ? "" : " (not the IIci ROM I know; trying it)");
#else
  if (got != kRomSize) { snprintf(s_error, sizeof s_error, "The ROM must be 256 KB (a Mac II ROM)"); return false; }
  // Mac II (two versions) and IIx/IIcx/SE/30: the ones meant to run here. Others are tried anyway.
  const bool known = sum == 0x97851DB6 || sum == 0x9779D2C4 || sum == 0x97221136;
  hal_log("macii: ROM %08x%s", (unsigned)sum, known ? "" : " (not a Mac II ROM I know; trying it)");
#endif
  if (!sony::patchRom(s_rom, kRomSize)) { snprintf(s_error, sizeof s_error, "No .Sony driver in the ROM"); return false; }

  for (bool &t : s_touched) t = false;    // a new start: the next write is a new generation
  for (int i = 0; i < sony::kDrives; i++) {
    s_sony.image[i].close();
    bool twice = false;                     // an image in two drives: the first one has it
    for (int k = 0; k < i; k++) twice = twice || s_path[k] == s_path[i];
    if (s_path[i].empty() || twice) continue;
    char err[96];
    if (!s_sony.image[i].open(s_path[i].c_str(), err, sizeof err)) {
      snprintf(s_error, sizeof s_error, "%s: %s", kDriveNames[i], err);
      return false;
    }
    if (i == kCdrom) s_sony.image[i].readOnly = true;
    hal_log("macii: %s %s, %u KB%s", kDriveNames[i], s_path[i].c_str(), (unsigned)(s_sony.image[i].size >> 10),
            s_sony.image[i].readOnly ? ", read-only" : "");
  }

  const uint32_t ram = kRamSizes[g_ramIndex];
  if (ram != s_ramSize) {
    hal_free(s_ram);
    s_ram = (uint8_t *)hal_alloc_big(ram);
    s_ramSize = s_ram ? ram : 0;
    if (!s_ram) { snprintf(s_error, sizeof s_error, "Out of memory for %s of RAM", kRamNames[g_ramIndex]); return false; }
  }
  s_speed = settingSpeed() == kSpeedMax ? 4 : settingSpeed() * 4;   // Max starts at 1x
  machineReset();
  s_redraw = true;
  s_panX = s_panY = 0;
  return true;
}

// "@mac" (debugging): the PC after each slice, for 30 frames.
constexpr int kDbgPcs = 8192;
uint32_t *s_dbgPcs = nullptr;
int s_dbgCount = 0;

// Runs `cycles` CPU cycles, in slices that end at the next VIA timer event.
uint32_t s_slices = 0;               // m68k_execute calls (the frame log)

void run(uint32_t cycles) {
  const uint64_t end = s_cycles + cycles;
  while (s_cycles < end) {
    const uint64_t t = viaAt(s_cycles);
    s_via1.tick(t);
#if !defined(MACII_IICI)
    s_via2.tick(t);
#endif
    adbTick(t);
#if defined(MACII_QUADRA)
    s_scc.tick(t);
    updateIrq();
#endif
    uint64_t slice = end - s_cycles;
    if (slice > kSliceMax) slice = kSliceMax;
    uint64_t ev = s_via1.nextEvent(t);
#if !defined(MACII_IICI)
    const uint64_t ev2 = s_via2.nextEvent(t);
    if (ev2 < ev) ev = ev2;
#endif
    if (s_adbAt != UINT64_MAX && s_adbAt - t < ev) ev = s_adbAt > t ? s_adbAt - t : 0;
#if defined(MACII_QUADRA)
    const uint64_t ev3 = s_scc.nextEvent(t);
    if (ev3 < ev) ev = ev3;
#endif
    if (ev != UINT64_MAX) {
      const uint64_t at = cyclesAtVia(t + (ev ? ev : 1));
      const uint64_t need = at > s_cycles ? at - s_cycles : 1;
      if (need < slice) slice = need;
    }
    s_inSlice = true;
    s_sliceEnded = false;
    s_slices++;
    int used = m68k_execute((int)slice);
    s_inSlice = false;
    if (used < 1) used = 1;
    s_cycles += (uint64_t)used;
    if (s_dbgPcs && s_dbgCount < kDbgPcs) s_dbgPcs[s_dbgCount++] = m68k_get_reg(nullptr, M68K_REG_PC);
  }
}

// --- input: the PicoCalc's keys to the ADB keyboard and mouse ----------------------------------
// As on the Plus core: Ctrl = Command, Sym = Option, Alt = the mouse button; the arrows are the
// Mac's arrow keys, the mouse or both (g_settings.arrows; both Shifts, Ctrl+Tab or F1: next).
enum { ARROWS_KEYS, ARROWS_MOUSE, ARROWS_BOTH, ARROWS_COUNT };
const char *const kArrowNames[ARROWS_COUNT] = { "Keys", "Mouse", "Both" };
bool s_shiftL = false, s_shiftR = false, s_ctrlHeld = false, s_shiftHeld = false, s_button = false;
uint8_t s_arrows = 0;                // held arrows: 1 left, 2 right, 4 up, 8 down
uint32_t s_arrowSince = 0, s_arrowLast = 0;
uint8_t s_down[256];                 // per PicoCalc key: the ADB key it pressed + 1 (0 = none)
bool s_downShift[256];               // ... with a Shift we added

struct AsciiKey { char c; uint8_t mkc; bool shift; };
const AsciiKey kAscii[] = {
  { '1', MKC_1, 0 }, { '2', MKC_2, 0 }, { '3', MKC_3, 0 }, { '4', MKC_4, 0 }, { '5', MKC_5, 0 },
  { '6', MKC_6, 0 }, { '7', MKC_7, 0 }, { '8', MKC_8, 0 }, { '9', MKC_9, 0 }, { '0', MKC_0, 0 },
  { '!', MKC_1, 1 }, { '@', MKC_2, 1 }, { '#', MKC_3, 1 }, { '$', MKC_4, 1 }, { '%', MKC_5, 1 },
  { '^', MKC_6, 1 }, { '&', MKC_7, 1 }, { '*', MKC_8, 1 }, { '(', MKC_9, 1 }, { ')', MKC_0, 1 },
  { '-', MKC_Minus, 0 }, { '_', MKC_Minus, 1 }, { '=', MKC_Equal, 0 }, { '+', MKC_Equal, 1 },
  { '[', MKC_LeftBracket, 0 }, { '{', MKC_LeftBracket, 1 }, { ']', MKC_RightBracket, 0 },
  { '}', MKC_RightBracket, 1 }, { '\\', MKC_BackSlash, 0 }, { '|', MKC_BackSlash, 1 },
  { ';', MKC_SemiColon, 0 }, { ':', MKC_SemiColon, 1 }, { '\'', MKC_SingleQuote, 0 },
  { '"', MKC_SingleQuote, 1 }, { ',', MKC_Comma, 0 }, { '<', MKC_Comma, 1 }, { '.', MKC_Period, 0 },
  { '>', MKC_Period, 1 }, { '/', MKC_Slash, 0 }, { '?', MKC_Slash, 1 }, { '`', MKC_Grave, 0 },
  { '~', MKC_Grave, 1 }, { ' ', MKC_Space, 0 },
};
const uint8_t kLetters[26] = {
  MKC_A, MKC_B, MKC_C, MKC_D, MKC_E, MKC_F, MKC_G, MKC_H, MKC_I, MKC_J, MKC_K, MKC_L, MKC_M,
  MKC_N, MKC_O, MKC_P, MKC_Q, MKC_R, MKC_S, MKC_T, MKC_U, MKC_V, MKC_W, MKC_X, MKC_Y, MKC_Z,
};

// The ADB key for a PicoCalc key, 0xFF for none; *shift = it needs Shift held.
uint8_t adbKey(uint8_t code, bool *shift) {
  *shift = false;
  if (code >= 'a' && code <= 'z') return kLetters[code - 'a'];
  if (code >= 'A' && code <= 'Z') { *shift = true; return kLetters[code - 'A']; }
  for (const AsciiKey &k : kAscii) if ((uint8_t)k.c == code) { *shift = k.shift; return k.mkc; }
  switch (code) {
    case KEY_ENTER: return MKC_Return;
    case KEY_BACKSPACE: case KEY_DEL: return MKC_BackSpace;
    case KEY_TAB: return MKC_Tab;
    case KEY_ESC: return 0x35;                      // an ADB keyboard has Esc
    case KEY_CTRL: return MKC_Command;
    case KEY_MAC_CTRL: return 0x36;                 // ADB Control (MKC_Control, 0x3B, is an arrow here)
    case KEY_SYM: return MKC_Option;
    case KEY_SHIFT_L: case KEY_SHIFT_R: return MKC_Shift;
    // ADB's raw codes, not the Plus keyboard's virtual ones (MKC_Left is 0x7B: on ADB that is the
    // right Shift, and the arrows did nothing in games).
    case KEY_LEFT: return 0x3B;
    case KEY_RIGHT: return 0x3C;
    case KEY_DOWN: return 0x3D;
    case KEY_UP: return 0x3E;
  }
  return 0xFF;
}

uint8_t arrowBit(uint8_t code) {
  switch (code) {
    case KEY_LEFT: return 1;
    case KEY_RIGHT: return 2;
    case KEY_UP: return 4;
    case KEY_DOWN: return 8;
  }
  return 0;
}

void releaseAll() {
  for (int c = 0; c < 256; c++) {
    if (!s_down[c]) continue;
    s_adb.key((uint8_t)(s_down[c] - 1), false);
    if (s_downShift[c] && !s_shiftHeld) s_adb.key(MKC_Shift, false);
    s_down[c] = 0;
    s_downShift[c] = false;
  }
  s_arrows = 0;
  s_button = false;
  s_shiftHeld = false;
  s_adb.mouse(0, 0, false);
}

// The arrows' mode (ARROWS_*), kept for the next start, with a note.
void setArrows(int mode) {
  g_settings.arrows = mode;
  s_arrows = 0;
  settingsSave();
  showNote(mode == ARROWS_KEYS    ? "Arrows: Mac arrow keys   (both Shifts: next)"
           : mode == ARROWS_MOUSE ? "Arrows: mouse, Alt = button   (both Shifts: next)"
                                  : "Arrows: mouse + arrow keys   (both Shifts: next)");
}

// Ctrl+Caps Lock: the 640x480 screen scaled to fit, or 1:1 following the pointer.
void toggleZoom() {
  hal_job_wait();
  s_screenLive = s_screenLive == SCREEN_PAN ? SCREEN_FIT : SCREEN_PAN;
  hal_video_wait();
  hal_panel_fill(0, 0, PANEL_W, PANEL_H, 0);
  showNote(s_screenLive == SCREEN_PAN ? "Zoom 1:1 (Ctrl+Caps Lock: off)" : "Zoom off");
}

void macii_keyEvent(uint8_t code, uint8_t state) {
  if (!s_running) return;
  if (code == KEY_RELEASE_ALL) {
    releaseAll();
    s_shiftL = s_shiftR = s_ctrlHeld = false;
    return;
  }
  const int mode = g_settings.arrows;
  // A held arrow keeps the pointer going; other keys: the Mac repeats them itself.
  if (state == KEYSTATE_HELD && !(mode != ARROWS_KEYS && arrowBit(code) && !s_down[code])) return;
  const bool down = state != KEYSTATE_RELEASED;
  const bool pressed = state == KEYSTATE_PRESSED;
  if (code == KEY_SHIFT_L) s_shiftL = down;
  if (code == KEY_SHIFT_R) s_shiftR = down;
  if (code == KEY_CTRL) s_ctrlHeld = down;
  if (pressed && ((code == KEY_SHIFT_L && s_shiftR) || (code == KEY_SHIFT_R && s_shiftL) ||
                  (code == KEY_TAB && s_ctrlHeld) || code == KEY_F1)) {
    releaseAll();
    setArrows((mode + 1) % ARROWS_COUNT);
    return;
  }
  // Ctrl+Caps Lock (as the Plus core): Fit <-> 1:1.
  if (code == KEY_CAPS && s_ctrlHeld) {
    if (pressed) {
      releaseAll();
      toggleZoom();
    }
    return;
  }
  if (code == KEY_ALT) { s_button = down; s_adb.mouse(0, 0, s_button); return; }
  if (const uint8_t a = arrowBit(code)) {
    if (!down) s_arrows &= ~a;
    else if (mode != ARROWS_KEYS && !s_down[code]) {
      // Only a real pause (no arrow for 250 ms) starts the acceleration again.
      if (!s_arrows && hal_millis() - s_arrowLast > 250) s_arrowSince = hal_millis();
      s_arrows |= a;
    }
    if (mode == ARROWS_MOUSE && !s_down[code]) return;
  }
  if (code == KEY_SHIFT_L || code == KEY_SHIFT_R) s_shiftHeld = down;
  if (down) {
    if (s_down[code]) return;
    bool shift;
    const uint8_t k = adbKey(code, &shift);
    if (k == 0xFF) return;
    if (shift && !s_shiftHeld) s_adb.key(MKC_Shift, true);
    s_downShift[code] = shift;
    s_adb.key(k, true);
    s_down[code] = (uint8_t)(k + 1);
  } else {
    if (!s_down[code]) return;
    s_adb.key((uint8_t)(s_down[code] - 1), false);
    if (s_downShift[code] && !s_shiftHeld) s_adb.key(MKC_Shift, false);
    s_down[code] = 0;
    s_downShift[code] = false;
  }
}

uint16_t rd16(uint32_t a) { return (uint16_t)(s_ram[a] << 8 | s_ram[a + 1]); }
void wr16(uint32_t a, int v) { s_ram[a] = (uint8_t)(v >> 8); s_ram[a + 1] = (uint8_t)v; }

// The held arrows move the pointer, faster the longer they are held (by time, not frames: the P4
// may not run every frame at 60 Hz): 1/2 pixel a frame for the first 250 ms, then up to 8 at 2 s.
// As on the Plus core, the pointer is set through the Mac's globals (as Mini vMac does): MTemp
// (0x828) and RawMouse (0x82C), then CrsrNew (0x8CE) = CrsrCouple (0x8CF) so the VBL task moves
// it. ADB deltas would go through the ROM's acceleration and stay slow. Before the cursor is set
// up (CrsrPin, 0x834, not valid yet), they go to the ADB mouse.
void moveMouse() {
  if (!s_arrows) return;
  const uint32_t now = hal_millis();
  s_arrowLast = now;
  const uint32_t held = now - s_arrowSince;
  int speed = 8;                                    // 1/16 pixel a frame
  if (held > 250) speed = held >= 2000 ? 128 : 8 + (int)((held - 250) * 120 / 1750);
  static int frac = 0;
  if (held < 20) frac = 0;
  frac += speed;
  const int step = frac >> 4;
  frac &= 15;
  if (!step) return;
  const int dx = ((s_arrows & 2) ? step : 0) - ((s_arrows & 1) ? step : 0);
  const int dy = ((s_arrows & 8) ? step : 0) - ((s_arrows & 4) ? step : 0);   // +v is down
  const int top = (int16_t)rd16(0x834), left = (int16_t)rd16(0x836);         // CrsrPin
  const int bottom = (int16_t)rd16(0x838), right = (int16_t)rd16(0x83A);
  if (s_overlay || bottom <= top || right <= left || bottom > 4096 || right > 4096) {
    s_adb.mouse(dx, dy, s_button);
    return;
  }
  // From where the pointer is (Mouse, 0x830), unless the VBL task has not taken the last move yet
  // (CrsrNew still set): then from MTemp.
  const uint32_t base = s_ram[0x8CE] ? 0x828 : 0x830;
  int v = (int16_t)rd16(base) + dy, h = (int16_t)rd16(base + 2) + dx;
  if (v < top) v = top;
  if (v > bottom - 1) v = bottom - 1;
  if (h < left) h = left;
  if (h > right - 1) h = right - 1;
  wr16(0x828, v); wr16(0x82A, h);
  wr16(0x82C, v); wr16(0x82E, h);
  s_ram[0x8CE] = s_ram[0x8CF];
}

#if defined(EMU_WEB)
// The page's pointer (hal_web_mouse): a touched spot (absolute) or the touchpad's moves, set
// through the same globals as moveMouse, and its button through the ADB mouse. A press or
// release that comes with a move waits one frame, so the click lands where the finger went.
void webPointer() {
  int dx, dy, x, y, b;
  if (!hal_web_mouse(&dx, &dy, &x, &y, &b)) return;
  const bool moved = dx || dy || x >= 0;
  if (moved) {
    const int top = (int16_t)rd16(0x834), left = (int16_t)rd16(0x836);       // CrsrPin
    const int bottom = (int16_t)rd16(0x838), right = (int16_t)rd16(0x83A);
    if (s_overlay || bottom <= top || right <= left || bottom > 4096 || right > 4096) {
      if (dx || dy) s_adb.mouse(dx, dy, s_button);
    } else {
      const uint32_t base = s_ram[0x8CE] ? 0x828 : 0x830;
      int v = x >= 0 ? y : (int16_t)rd16(base) + dy, h = x >= 0 ? x : (int16_t)rd16(base + 2) + dx;
      if (v < top) v = top;
      if (v > bottom - 1) v = bottom - 1;
      if (h < left) h = left;
      if (h > right - 1) h = right - 1;
      wr16(0x828, v); wr16(0x82A, h);
      wr16(0x82C, v); wr16(0x82E, h);
      s_ram[0x8CE] = s_ram[0x8CF];
    }
  }
  static int sent = 0;
  static bool waited = false;
  if (b == sent) return;
  if (moved && !waited) { waited = true; return; }
  waited = false;
  sent = b;
  s_button = b != 0;
  s_adb.mouse(0, 0, s_button);
}
#endif

// A bug in a Mac OS 8.1 start-up patch (it hooks the level-1 interrupt): looking for its own code
// near the handler, it adds a skip offset with ADD.W, so when the code it finds straddles a 64 KB
// boundary the sum loses the carry, the compare never matches and the search starts over for good
// (16 MB on the Quadra after the five drives moved the system heap: "Starting up", the first icon).
// While the CPU is in that loop, the three instructions become the same test done without the carry:
//   move.l a4,d1 / add.w tbl(pc,d2.w),d1 / cmp.l a0,d1  ->  move.l a0,d1 / sub.l a4,d1 / cmp.w tbl(pc,d2.w),d1
// and the loop's BRA back to the CMP goes to the MOVE (d1 is now worked out afresh each time).
void fixPatchSearch() {
  static const uint16_t kLoop[] = { 0xB5C8, 0x67E2, 0xB258, 0x66F8, 0x5588, 0x2848, 0x220C, 0xD27B, 0x203C,
                                    0xB288, 0x670C, 0xB7C9, 0x6710, 0x3619, 0xB658, 0x66DA, 0x60F0 };
  constexpr int kWords = sizeof kLoop / sizeof kLoop[0];
  const uint32_t pc = m68k_get_reg(nullptr, M68K_REG_PC) & ~1u;
  for (uint32_t at = pc - 2 * kWords; at <= pc; at += 2) {
    uint8_t *h = gHost(at, 2 * kWords);
    if (!h) continue;
    int i = 0;
    while (i < kWords && rd<2>(h + 2 * i) == kLoop[i]) i++;
    if (i < kWords) continue;
    wr<2>(h + 12, 0x2208); wr<2>(h + 14, 0x928C); wr<2>(h + 16, 0xB27B); wr<2>(h + 18, 0x203A);
    wr<2>(h + 32, 0x60EA);
    hal_log("macii: fixed the 8.1 patch search at %08x", (unsigned)at);
    return;
  }
}

// The coupling capacitor of the output stage (one-pole high-pass, ~20 Hz), as the Plus core.
struct SoundFilter {
  float hpR, hpX = 0, hpY = 0;
  SoundFilter() { hpR = expf(-2.0f * 3.14159265f * 20.0f / (float)kRate); }
  int16_t run(int16_t in) {
    const float x = (float)in;
    hpY = hpR * (hpY + x - hpX);
    hpX = x;
    const float y = hpY;
    return (int16_t)(y > 32767.0f ? 32767 : y < -32768.0f ? -32768 : (int)y);
  }
};
SoundFilter s_soundFilter;

// Below full speed (the P4 plays Prince of Persia at 92-94%), a frame's 370 samples take longer
// than 16.6 ms to make: the output ran dry several times a second, and each empty DMA buffer played
// as silence -- a crackle on the game's sound (the music, on lighter screens, near 100%, kept up).
// So when the queue runs low the frame is stretched to fill the time (a little lower in pitch, at
// most 25%); at full speed the writes block with the queue full and nothing changes.
struct SoundStretch {
  static constexpr uint32_t kLowUs = 25000;          // start stretching below this much queued
  float stretch = 1.0f;
  uint32_t phase = 0;                                // 16.16, as audio_p4's resampler
  int32_t prev = 0;
  int16_t out[kSamples * 5 / 4 + 4];
  void reset() { stretch = 1.0f; phase = 0; prev = 0; }
  int run(const int16_t *in, int n, uint32_t queuedUs) {
    float want = 1.0f;
    if (queuedUs < kLowUs) want = 1.0f + 0.25f * (float)(kLowUs - queuedUs) / (float)kLowUs;
    stretch += (want - stretch) * 0.125f;            // smoothed: no warble from frame to frame
    const uint32_t step = (uint32_t)(65536.0f / stretch);
    int m = 0;
    for (int i = 0; i < n; i++) {
      const int32_t x = in[i];
      while (phase < 65536u && m < (int)(sizeof out / sizeof out[0])) {
        out[m++] = (int16_t)(prev + (((x - prev) * (int32_t)(phase >> 2)) >> 14));
        phase += step;
      }
      phase = phase >= 65536u ? phase - 65536u : 0;   // (0: only if out filled up)
      prev = x;
    }
    return m;
  }
};
SoundStretch s_stretch;

// --- snapshots (mac_snapshot.cpp) ---------------------------------------------------------------
// After the header: "MII1", the sizes of the parts (another build's layout = "another version"),
// then the 68020's context, the core's variables, the devices, the RAM and the VRAM. Saved and
// loaded between frames, once the menu has closed.
#if defined(MACII_QUADRA)
constexpr char kStateTag[4] = { 'M', 'Q', '7', '1' };
#elif defined(MACII_IICI)
constexpr char kStateTag[4] = { 'M', 'I', 'C', '1' };
#define s_via2 s_rbv                                // the RBV takes VIA2's place in the file
#else
constexpr char kStateTag[4] = { 'M', 'I', 'I', '1' };
#endif
struct Globals {
  uint64_t cycles, viaBase, cycBase, adbAt;
  int32_t speed, irqLevel;
  uint32_t depth, rowBytes, base, clut[256];
  uint8_t clutIndex, vblEnable, vblPending, mode, grayMode, overlay, addr32, iwmLines, iwmMode;
#if defined(MACII_PMMU)
  Mmu mmu;
  uint8_t mmuOn;
#endif
};
enum { PART_CPU, PART_GLOBALS, PART_VIA, PART_ADB, PART_RTC, PART_ASC, PART_SCSI, PART_SONY, PART_RAM, PART_VRAM, PART_COUNT };

void stateSizes(uint32_t *z) {
  z[PART_CPU] = m68k_context_size();
  z[PART_GLOBALS] = sizeof(Globals);
  z[PART_VIA] = sizeof(Via6522) + sizeof(s_via2);
  z[PART_ADB] = sizeof(AdbBus);
  z[PART_RTC] = sizeof(MacRtc);
  z[PART_ASC] = sizeof(Asc);
#if defined(MACII_QUADRA)
  z[PART_SCSI] = sizeof(s_scsi) + sizeof(s_scc);   // the 53C96, then the SCC
#else
  z[PART_SCSI] = sizeof(s_scsi);
#endif
  z[PART_SONY] = sony::Drives::kStateSize;
  z[PART_RAM] = s_ramSize;
  z[PART_VRAM] = kVramSize;
}

bool saveState(FILE *f) {
  uint32_t z[PART_COUNT];
  stateSizes(z);
  Globals g;
  memset(&g, 0, sizeof g);
  g.cycles = s_cycles; g.viaBase = s_viaBase; g.cycBase = s_cycBase; g.adbAt = s_adbAt;
  g.speed = s_speed | 0x10000; g.irqLevel = s_irqLevel;   // 0x10000: in quarters
  g.depth = s_depth; g.rowBytes = s_rowBytes; g.base = s_base;
  memcpy(g.clut, s_clut, sizeof g.clut);
  g.clutIndex = s_clutIndex; g.vblEnable = s_vblEnable; g.vblPending = s_vblPending; g.mode = s_mode;
  g.grayMode = s_grayMode; g.overlay = s_overlay; g.addr32 = s_addr32; g.iwmLines = s_iwmLines; g.iwmMode = s_iwmMode;
#if defined(MACII_PMMU)
  g.mmu = s_mmu; g.mmuOn = s_mmuOn;
#endif
  uint8_t *cpu = (uint8_t *)malloc(z[PART_CPU]);
  uint8_t adb[sizeof(AdbBus)], sony[sony::Drives::kStateSize];
  if (!cpu) return false;
  m68k_get_context(cpu);
  s_adb.saveState(adb);
  s_sony.saveState(sony);
  bool ok = fwrite(kStateTag, 1, 4, f) == 4 && fwrite(z, 4, PART_COUNT, f) == PART_COUNT &&
            fwrite(cpu, 1, z[PART_CPU], f) == z[PART_CPU] && fwrite(&g, sizeof g, 1, f) == 1 &&
            fwrite((const void *)&s_via1, sizeof s_via1, 1, f) == 1 && fwrite((const void *)&s_via2, sizeof s_via2, 1, f) == 1 &&
            fwrite(adb, sizeof adb, 1, f) == 1 && fwrite((const void *)&s_rtc, sizeof s_rtc, 1, f) == 1 &&
            fwrite((const void *)&s_asc, sizeof s_asc, 1, f) == 1 && fwrite((const void *)&s_scsi, sizeof s_scsi, 1, f) == 1 &&
#if defined(MACII_QUADRA)
            fwrite((const void *)&s_scc, sizeof s_scc, 1, f) == 1 &&
#endif
            fwrite(sony, sizeof sony, 1, f) == 1 && fwrite(s_ram, 1, s_ramSize, f) == s_ramSize &&
            fwrite(s_vram, 1, kVramSize, f) == kVramSize;
  free(cpu);
  return ok;
}

// A VIA from the file; its callbacks stay this build's.
bool loadVia(FILE *f, Via6522 &v) {
  const Via6522 keep = v;
  if (fread((void *)&v, sizeof v, 1, f) != 1) return false;
  v.ctx = keep.ctx; v.inA = keep.inA; v.inB = keep.inB; v.outA = keep.outA; v.outB = keep.outB;
  v.irq = keep.irq; v.srAccess = keep.srAccess;
  return true;
}

int loadState(FILE *f) {
  char tag[4];
  uint32_t z[PART_COUNT], want[PART_COUNT];
  stateSizes(want);
  if (fread(tag, 1, 4, f) != 4 || memcmp(tag, kStateTag, 4) || fread(z, 4, PART_COUNT, f) != PART_COUNT ||
      memcmp(z, want, sizeof z))
    return -2;
  uint8_t *cpu = (uint8_t *)malloc(z[PART_CPU]);
  if (!cpu) return -2;
  Globals g;
  uint8_t adb[sizeof(AdbBus)], sony[sony::Drives::kStateSize];
  bool ok = fread(cpu, 1, z[PART_CPU], f) == z[PART_CPU] && fread(&g, sizeof g, 1, f) == 1 &&
            loadVia(f, s_via1) &&
#if defined(MACII_IICI)
            fread((void *)&s_rbv, sizeof s_rbv, 1, f) == 1 &&
#else
            loadVia(f, s_via2) &&
#endif
            fread(adb, sizeof adb, 1, f) == 1 &&
            fread((void *)&s_rtc, sizeof s_rtc, 1, f) == 1 && fread((void *)&s_asc, sizeof s_asc, 1, f) == 1 &&
            fread((void *)&s_scsi, sizeof s_scsi, 1, f) == 1 &&
#if defined(MACII_QUADRA)
            fread((void *)&s_scc, sizeof s_scc, 1, f) == 1 &&
#endif
            fread(sony, sizeof sony, 1, f) == 1 &&
            fread(s_ram, 1, s_ramSize, f) == s_ramSize && fread(s_vram, 1, kVramSize, f) == kVramSize;
  if (ok) {
    m68k_set_context_keep_hooks(cpu);
    s_adb.loadState(adb);
    s_sony.loadState(sony);
    s_cycles = g.cycles; s_viaBase = g.viaBase; s_cycBase = g.cycBase; s_adbAt = g.adbAt;
    s_speed = g.speed & 0x10000 ? g.speed & 0xFFFF : g.speed * 4; s_irqLevel = g.irqLevel;
    if (s_speed < 4) s_speed = 4;
    s_pastStartup = true;                             // a snapshot is past the start-up tests
    s_depth = g.depth & 3; s_rowBytes = g.rowBytes; s_base = g.base & (kVramSize - 1);
    memcpy(s_clut, g.clut, sizeof s_clut);
    s_clutIndex = g.clutIndex; s_vblEnable = g.vblEnable; s_vblPending = g.vblPending; s_mode = g.mode;
    s_grayMode = g.grayMode; s_overlay = g.overlay; s_addr32 = g.addr32; s_iwmLines = g.iwmLines; s_iwmMode = g.iwmMode;
#if defined(MACII_PMMU)
    s_mmu = g.mmu; s_mmuOn = g.mmuOn;
#endif
    fastUpdate();
    s_vramDirty = true;
  }
  free(cpu);
  return ok ? 0 : -1;
}

std::string s_snapSave, s_snapReplace, s_snapLoad;  // the save's name, the snapshot it overwrites

void saveSnapshot() {
  hal_job_wait();
  showNote("Saving snapshot...");
  for (sony::Image &im : s_sony.image) im.flush();
  MacSnapMachine m;
  m.disk = s_path[0];
  m.disk2 = s_path[1];
  m.hd2 = s_path[2];
  m.floppy2 = s_path[3];
  m.cdrom = s_path[4];
  m.rom = g_romPath;
  for (int i = 0; i < RAM2_COUNT; i++) if (kRamSizes[i] == s_ramSize) m.ram = i;
  m.screen = s_screenLive;
  const char *e = macsnap_save(s_snapSave, s_snapReplace, m, screenPixel, kScreenW, kScreenH, saveState);
  // The disks as they are now go with it: their next write is a new generation.
  if (!e) for (bool &t : s_touched) t = false;
  showNote(e ? e : ("Saved: " + s_snapSave).c_str());
  s_snapSave.clear();
  s_snapReplace.clear();
}

// Starts the Mac II the snapshot needs (its disks, ROM, RAM and screen become the settings) and
// loads its state. A snapshot that cannot be loaded leaves a fresh start with those disks.
bool startFromSnapshot(const std::string &snap) {
  MacSnapMachine m;
  if (!macsnap_machine(snap, m)) { snprintf(s_error, sizeof s_error, "Cannot read the snapshot"); return false; }
  const std::string paths[sony::kDrives] = { m.disk, m.disk2, m.hd2, m.floppy2, m.cdrom };
  for (int i = 0; i < sony::kDrives; i++) settingOf(i) = s_path[i] = paths[i];
  g_romPath = m.rom;
  g_ramIndex = m.ram;
  g_settings.screen = m.screen;
  settingsSave();
  s_screenLive = m.screen == SCREEN_PAN ? SCREEN_PAN : SCREEN_FIT;
  releaseAll();
  if (!powerOn()) return false;
  bool broken;
  if (const char *e = macsnap_restore(snap, &broken, loadState)) {
    ui::message("Snapshot", e, 2500);
    hal_panel_fill(0, 0, PANEL_W, PANEL_H, 0);
    return broken ? powerOn() : true;               // not "broken": nothing was loaded
  }
  applySpeedSetting();                              // the setting, not the snapshot's
  // The clock moves on to now when there is one: the RTC and the System's copy (Time, 0x20C).
  uint32_t secs;
  if (hostClock(&secs)) {
    s_rtc.seconds = secs;
    s_ram[0x20C] = (uint8_t)(secs >> 24); s_ram[0x20D] = (uint8_t)(secs >> 16);
    s_ram[0x20E] = (uint8_t)(secs >> 8); s_ram[0x20F] = (uint8_t)secs;
  }
  s_redraw = true;
  return true;
}

// --- debugging a hung Mac: "@mac" on the P4's console ("dump" in MAC_KEYS) -----------------------
// The PC after each slice for 30 frames; then the registers, the VIAs, the most frequent PCs and
// the code at the top ones, in hal_log lines.
volatile bool s_dbgWant = false;
int s_dbgFrames = 0;

void dbgCommand(const char *line) {
  if (!strcmp(line, "@mac")) s_dbgWant = true;
}

// A word of RAM or ROM as the CPU sees it now, without side effects (0 elsewhere).
unsigned peekWord(uint32_t a) {
  const uint32_t p = phys(a & ~1u);
  if ((p >> 28) == 4 || ((p >> 28) < 4 && s_overlay)) return rd<2>(s_rom + (p & (kRomSize - 1)));
#if defined(MACII_PMMU)
  if ((p >> 28) < 4) { uint8_t *h = ramHost(p); return h ? rd<2>(h) : 0; }
#else
  if ((p >> 28) < 4 && p + 2 <= s_ramSize) return rd<2>(s_ram + p);
#endif
  return 0;
}

void dbgDump() {
  hal_log("@mac frame %u, %d PCs, %s, overlay %d", (unsigned)0, s_dbgCount, s_addr32 ? "32-bit" : "24-bit", s_overlay);
  char b[200], *o = b;
  for (int r = M68K_REG_D0; r <= M68K_REG_D7; r++) o += sprintf(o, " d%d %08x", r - M68K_REG_D0, (unsigned)m68k_get_reg(nullptr, (m68k_register_t)r));
  hal_log("@mac%s", b);
  o = b;
  for (int r = M68K_REG_A0; r <= M68K_REG_A7; r++) o += sprintf(o, " a%d %08x", r - M68K_REG_A0, (unsigned)m68k_get_reg(nullptr, (m68k_register_t)r));
  hal_log("@mac%s", b);
  hal_log("@mac pc %08x sr %04x usp %08x isp %08x msp %08x vbr %08x", (unsigned)m68k_get_reg(nullptr, M68K_REG_PC),
          (unsigned)m68k_get_reg(nullptr, M68K_REG_SR), (unsigned)m68k_get_reg(nullptr, M68K_REG_USP),
          (unsigned)m68k_get_reg(nullptr, M68K_REG_ISP), (unsigned)m68k_get_reg(nullptr, M68K_REG_MSP),
          (unsigned)m68k_get_reg(nullptr, M68K_REG_VBR));
  hal_log("@mac via1 ifr %02x ier %02x pb %02x acr %02x | via2 ifr %02x ier %02x pb %02x | irq %d",
          s_via1.ifr(), s_via1.ier(), s_via1.portB(), s_via1.acr(), s_via2.ifr(), s_via2.ier(), s_via2.portB(), s_irqLevel);
  hal_log("@mac Ticks %08x  Time %08x", (unsigned)rd<4>(s_ram + 0x16A), (unsigned)rd<4>(s_ram + 0x20C));
  const int n = s_dbgCount;
  std::sort(s_dbgPcs, s_dbgPcs + n);
  uint32_t top[12] = {}; int topN[12] = {};
  for (int i = 0; i < n;) {
    int j = i;
    while (j < n && s_dbgPcs[j] == s_dbgPcs[i]) j++;
    const int c = j - i;
    for (int k = 0; k < 12; k++)
      if (c > topN[k]) {
        for (int m = 11; m > k; m--) { top[m] = top[m - 1]; topN[m] = topN[m - 1]; }
        top[k] = s_dbgPcs[i]; topN[k] = c;
        break;
      }
    i = j;
  }
  for (int k = 0; k < 12 && topN[k]; k++) hal_log("@mac pc %08x x%d", (unsigned)top[k], topN[k]);
  for (int k = 0; k < 3 && topN[k]; k++) {
    const uint32_t from = (top[k] - 0x20) & ~1u;
    for (uint32_t a = from; a < from + 0x60; a += 16) {
      o = b;
      o += sprintf(o, "@mac %08x:", (unsigned)a);
      for (int i = 0; i < 16; i += 2) o += sprintf(o, " %04x", peekWord(a + i));
      hal_log("%s", b);
    }
  }
  // The stack, then the A6 chain: each frame's caller.
  const uint32_t sp = m68k_get_reg(nullptr, M68K_REG_A7);
  for (uint32_t a = sp; a < sp + 64; a += 16) {
    o = b;
    o += sprintf(o, "@mac sp %08x:", (unsigned)a);
    for (int i = 0; i < 16; i += 2) o += sprintf(o, " %04x", peekWord(a + i));
    hal_log("%s", b);
  }
  o = b;
  o += sprintf(o, "@mac a6 chain:");
  uint32_t fp = m68k_get_reg(nullptr, M68K_REG_A6);
  for (int k = 0; k < 10 && fp && !(fp & 1); k++) {
    o += sprintf(o, " %08x", (unsigned)(peekWord(fp + 4) << 16 | peekWord(fp + 6)));
    const uint32_t nfp = peekWord(fp) << 16 | peekWord(fp + 2);
    if (nfp <= fp) break;
    fp = nfp;
  }
  hal_log("%s", b);
#if !defined(ESP_PLATFORM)
  if (const char *path = getenv("MAC_RAMDUMP"))       // desktop: the RAM, for a disassembler
    if (FILE *f = fopen(path, "wb")) { fwrite(s_ram, 1, s_ramSize, f); fclose(f); hal_log("@mac RAM in %s", path); }
#endif
  hal_log("@mac end");
}

// Before a frame: start or finish a "@mac".
void dbgStep() {
  if (s_dbgWant && !s_dbgPcs) {
    s_dbgWant = false;
    s_dbgPcs = (uint32_t *)malloc(kDbgPcs * sizeof(uint32_t));
    s_dbgCount = 0;
    s_dbgFrames = 0;
    return;
  }
  if (s_dbgPcs && ++s_dbgFrames >= 30) {
    dbgDump();
    free(s_dbgPcs);
    s_dbgPcs = nullptr;
  }
}

// --- EmuCore ------------------------------------------------------------------------------------
uint32_t s_frames = 0, s_secFrac = 0;
int16_t s_sound[kSamples];
uint32_t s_speedT0 = 0, s_speedF0 = 0, s_speedW0 = 0;
uint64_t s_speedC0 = 0;
char s_speedText[64];
uint32_t s_underrunsShown = 0;
uint32_t s_lastFrameMs = 0;

// The dispatch (M68K_DISPATCH, m68kconf020.h): a 16-bit index per opcode into the distinct handlers,
// in internal RAM if it fits (else PSRAM). From flash, a random table entry per instruction was a
// cache miss to the slow SPI flash; a 256 KB pointer copy no longer fit beside the IRAM code.
void jumpTableToRam() {
  if (macii_op_index) return;
  using Handler = void (*)(void);
  uint16_t *idx = (uint16_t *)hal_alloc_fast(0x10000 * sizeof(uint16_t));
  if (!idx) return;
  std::unordered_map<Handler, uint16_t> seen;
  std::vector<Handler> list;
  for (int i = 0; i < 0x10000; i++) {
    Handler h = m68kXi_static_instruction_jump_table[i];
#if defined(MACII_QUADRA)
    // The 68040's own instructions (macquadra_mmu.h), over Musashi's.
    if (i == 0x4E7A) h = macquadra_op_movec_cr;
    else if (i == 0x4E7B) h = macquadra_op_movec_rc;
    else if (i >= 0xF300 && i < 0xF380) h = macquadra_op_fsave;
    else if ((i & 0xFF00) == 0xF400) h = macquadra_op_cinv;
    else if ((i & 0xFFE0) == 0xF500) h = macquadra_op_pflush;
    else if ((i & 0xFFF8) == 0xF548 || (i & 0xFFF8) == 0xF568) h = macquadra_op_ptest;
    else if ((i & 0xFFE0) == 0xF600 || (i & 0xFFF8) == 0xF620) h = macquadra_op_move16;
#endif
    auto it = seen.find(h);
    if (it == seen.end()) { it = seen.emplace(h, (uint16_t)list.size()).first; list.push_back(h); }
    idx[i] = it->second;
  }
  Handler *hs = (Handler *)hal_alloc_fast(list.size() * sizeof(Handler));
  if (!hs) { hal_free(idx); return; }
  memcpy(hs, list.data(), list.size() * sizeof(Handler));
  macii_op_index = idx;
  macii_op_handlers = hs;
  hal_log("macii: %u opcode handlers, index at %p", (unsigned)list.size(), (void *)idx);
}

void jumpTableFree() {
  hal_free(macii_op_index);
  hal_free(macii_op_handlers);
  macii_op_index = nullptr;
  macii_op_handlers = nullptr;
}

bool macii_init() {
  s_sony.setBus({ gRead8, gRead16, gRead32, gWrite8, gWrite16, gWrite32, gHost });
  jumpTableToRam();
  s_rom = (uint8_t *)hal_alloc_big(kRomSize);
  s_vram = (uint8_t *)hal_alloc_big(kVramSize);
  slotrom::build(s_slotRom, kCardSlot);
  hal_serial_cmd = dbgCommand;
  s_error[0] = 0;
  return s_rom && s_vram && macii_op_index;
}

// path: the boot disk image (hard disk 1), or "" for none. The other drives are g_settings.disk2
// (floppy 1), hd2 and floppy2. A snapshot set with macsnap_set_pending is started from instead.
bool macii_loadRom(const char *path) {
  const std::string snap = macsnap_take_pending();
  s_snapSave.clear();
  s_snapLoad.clear();
  s_diskPath = path ? path : "";
  // From before four drives: the second disk was the floppy, or a hard disk if it was bigger.
  struct stat st;
  if (!g_settings.disk2.empty() && g_settings.hd2.empty() && !stat(g_settings.disk2.c_str(), &st) &&
      st.st_size > 1440 * 1024 + 84) {
    g_settings.hd2 = g_settings.disk2;
    g_settings.disk2.clear();
    settingsSave();
  }
  for (int i = 1; i < sony::kDrives; i++) s_path[i] = settingOf(i);
  s_screenLive = g_settings.screen == SCREEN_PAN ? SCREEN_PAN : SCREEN_FIT;
  s_via1.irq = viaIrq;
#if !defined(MACII_IICI)
  s_via2.irq = viaIrq;
#endif
  s_via1.inA = via1InA;
  s_via1.inB = via1InB;
  s_via1.outB = via1OutB;
#if !defined(MACII_IICI)
  s_via2.inA = via2InA;
  s_via2.outB = via2OutB;
#if defined(MACII_QUADRA)
  s_via2.inB = via2InB;
#endif
#endif
  loadPram();
  memset(s_down, 0, sizeof s_down);
  memset(s_downShift, 0, sizeof s_downShift);
  s_arrows = 0;
  s_button = s_shiftHeld = s_shiftL = s_shiftR = s_ctrlHeld = false;
  if (!(snap.empty() ? powerOn() : startFromSnapshot(snap))) { hal_log("%s: %s", kMachineName, s_error); return false; }
  hal_audio_start(kRate);
  s_stretch.reset();
  s_running = true;
  s_frames = s_secFrac = 0;
  s_speedT0 = hal_micros();
  s_speedF0 = 0;
  s_speedC0 = s_runCycles;
  s_speedW0 = hal_audio_wait_us();
  s_noteUntil = 0;
  s_stripUsed = g_settings.showFps;
  s_redraw = true;
  hal_log("macii: started, %u MB RAM", (unsigned)(s_ramSize >> 20));
  return true;
}

// Reset is a power cycle: the PRAM is kept, the disks flushed.
void macii_reset() {
  if (!s_running) return;
  hal_job_wait();
  savePram();
  releaseAll();
  for (sony::Image &im : s_sony.image) im.flush();
  if (!powerOn()) { ui::message("Error", s_error, 2500); s_running = false; }
  hal_panel_fill(0, 0, PANEL_W, PANEL_H, 0);
}

bool s_colorsChanged = false;        // the Colors row moved: the Mac restarts once the menu closes
void restartWithDisks(const char *title);

void macii_runFrame(bool draw) {
  if (!s_running) { hal_delay(16); return; }
  if (!s_snapSave.empty()) saveSnapshot();
  if (s_colorsChanged) {
    s_colorsChanged = false;
    restartWithDisks("Colors");
    hal_panel_fill(0, 0, PANEL_W, PANEL_H, 0);
    return;
  }
  if (!s_snapLoad.empty()) {
    std::string snap;
    snap.swap(s_snapLoad);
    ui::message("Snapshot", "Restarting from it", 600);
    hal_job_wait();
    savePram();
    for (sony::Image &im : s_sony.image) im.flush();
    if (!startFromSnapshot(snap)) {
      ui::message("Error", s_error, 2500);
      s_running = false;
    }
    hal_panel_fill(0, 0, PANEL_W, PANEL_H, 0);
    return;
  }
  const uint32_t ms = hal_millis();
  if (ms - s_lastFrameMs > 200) s_redraw = true;   // back from the menu
  s_lastFrameMs = ms;

  moveMouse();
#if defined(EMU_WEB)
  hal_web_ui_pc();
  webPointer();
#endif
  dbgStep();
  if (s_adbAt == UINT64_MAX && s_adb.autoPoll()) s_adbAt = viaNow() + AdbBus::kByteTicks;   // ADB idle: auto-poll
  // Where a frame's time goes, logged every 600 frames: the 68020, the rest, waiting for the sound.
  static uint32_t pCpu = 0, pRest = 0, pAudio = 0, pT = 0;
  const uint32_t tA = hal_micros();
  if (pT && tA - pT < 1000000) pRest += tA - pT;
  // The frame in 10 parts, the ASC playing 37 samples after each: its FIFO drains as it would.
  for (int part = 0; part < 10; part++) {
    const uint32_t cyc = (uint32_t)((uint64_t)(kFrameCycles * (part + 1) / 10 - kFrameCycles * part / 10) * s_speed / 4);
    s_runCycles += cyc;
    run(cyc);
#if defined(MACII_IICI)
    if (s_asc.render(s_sound + part * (kSamples / 10), kSamples / 10)) s_rbv.asc();
#else
    if (s_asc.render(s_sound + part * (kSamples / 10), kSamples / 10)) s_via2.pulse(Via6522::CB1);
#endif
  }
  // A disk's first write since the start or the last snapshot: the snapshots before no longer fit it.
  for (int i = 0; i < sony::kDrives; i++)
    if (s_sony.image[i].written) {
      s_sony.image[i].written = false;
      if (!s_touched[i]) { s_touched[i] = true; macsnap_touch(s_path[i]); }
    }
  // The frame's end: VIA1 CA1 is the 60.15 Hz interrupt, CA2 the 1 Hz one; the card's VBL.
  s_via1.pulse(Via6522::CA1);
  s_secFrac += 1000;
  if (s_secFrac >= 60150) { s_secFrac -= 60150; s_via1.pulse(Via6522::CA2); s_rtc.seconds++; }
#if defined(MACII_QUADRA)
  if (s_scsi.tick()) s_via2.pulse(Via6522::CB2);    // a selection timed out
#endif
  s_vblPending = true;
  cardIrq();
  s_frames++;
#if defined(MACII_OPCOUNT)
  if (s_frames % 3600 == 0) opCountDump();
#endif
  if (s_frames % 60 == 0) {
    fixPatchSearch();
    for (sony::Image &im : s_sony.image) im.flush();
    if (s_rtc.dirty) savePram();
  }
#if defined(BOARD_DESKTOP)
  // MACII_TRACE=1 (test hook): the CPU and the VIAs once a second.
  static const bool trace = getenv("MACII_TRACE") != nullptr;
  if (trace && s_frames % 60 == 0)
    hal_log("macii: frame %u pc %08x sr %04x a7 %08x d0 %08x d6 %08x d7 %08x | via1 ifr %02x ier %02x pb %02x | via2 ifr %02x ier %02x pb %02x | %s",
            (unsigned)s_frames, (unsigned)m68k_get_reg(nullptr, M68K_REG_PC), (unsigned)m68k_get_reg(nullptr, M68K_REG_SR),
            (unsigned)m68k_get_reg(nullptr, M68K_REG_A7), (unsigned)m68k_get_reg(nullptr, M68K_REG_D0), (unsigned)m68k_get_reg(nullptr, M68K_REG_D6), (unsigned)m68k_get_reg(nullptr, M68K_REG_D7),
            s_via1.ifr(), s_via1.ier(), s_via1.portB(), s_via2.ifr(), s_via2.ier(), s_via2.portB(), s_addr32 ? "32-bit" : "24-bit");
#endif

  for (int i = 0; i < kSamples; i++) s_sound[i] = s_soundFilter.run(s_sound[i]);
  const uint32_t tB = hal_micros();
  {
    const int n = s_stretch.run(s_sound, kSamples, hal_audio_queued_us());
    hal_audio_write(s_stretch.out, n);              // its blocking paces the Mac
  }
  const uint32_t tPrev = pT;
  pT = hal_micros();
  if (tPrev) maxSpeedStep(tB - tPrev, hal_audio_queued_us());
  pCpu += tB - tA;
  pAudio += pT - tB;
  if (s_frames % 600 == 0) {
    hal_log("macii: per frame (us): cpu+devices %u, sound wait %u, rest %u; %u slices; sound x%.3f; cpu %d.%02dx", (unsigned)(pCpu / 600), (unsigned)(pAudio / 600), (unsigned)(pRest / 600), (unsigned)(s_slices / 600), (double)s_stretch.stretch, s_speed / 4, s_speed % 4 * 25);
    pCpu = pAudio = pRest = 0;
    s_slices = 0;
  }

  if (s_noteUntil && (int32_t)(hal_millis() - s_noteUntil) >= 0) {
    s_noteUntil = 0;
    if (!g_settings.showFps) showNote(nullptr);
  }
  if (g_settings.showFps != s_stripUsed && !s_noteUntil) { s_stripUsed = g_settings.showFps; s_redraw = true; }
  if (draw && hal_video_ready()) hal_job_start(drawJob, nullptr);
}

void macii_setInput(int, uint32_t) {}
uint32_t macii_frameRate() { return 60; }

void macii_shutdown() {
#if defined(MACII_OPCOUNT)
  opCountDump();
#endif
  hal_job_wait();
  if (s_running) savePram();
  s_running = false;
  for (sony::Image &im : s_sony.image) im.close();
  hal_free(s_ram);
  hal_free(s_rom);
  hal_free(s_vram);
  s_ram = s_rom = s_vram = nullptr;
  s_ramSize = 0;
  jumpTableFree();
}

const char *macii_fpsText() {
  const uint32_t now = hal_micros(), us = now - s_speedT0;
  if (us) {
    const uint64_t cyc = s_runCycles - s_speedC0;
    // gaps: sound output buffers that ran dry since the last reading (each one is a click)
    const uint32_t under = hal_audio_underruns();
    // max: the speed with no waiting for the sound, from the time the frames took minus that wait
    // (the headroom: 4.0x = this machine could run four Mac IIs, a 68020 at 63 MHz)
    const uint32_t wait = hal_audio_wait_us() - s_speedW0, busy = us > wait ? us - wait : 1;
    const uint64_t maxX10 = cyc * 10 * 1000000 / kCpuHz / busy;
    snprintf(s_speedText, sizeof s_speedText, "%u%% of a %s  max %u.%ux (%u MHz)  %u gaps",
             (unsigned)(cyc * 100 * 1000000 / kCpuHz / us), kMachineName, (unsigned)(maxX10 / 10), (unsigned)(maxX10 % 10),
             (unsigned)(cyc * 1000000 / busy / 1000000), (unsigned)(under - s_underrunsShown));
    s_underrunsShown = under;
  }
  s_speedT0 = now;
  s_speedF0 = s_frames;
  s_speedC0 = s_runCycles;
  s_speedW0 = hal_audio_wait_us();
  return s_speedText;
}

// --- in-game menu rows (as the Plus core) ---------------------------------------------------------
// The snapshots, then Drives (a submenu, as pico-386's Disks), then the settings.
// CPU speed: not on the P4 (kHasSpeed).
#if defined(ESP_PLATFORM)
enum { ROW_SNAP_LOAD, ROW_SNAP_SAVE, ROW_DRIVES, ROW_SCREEN, ROW_COLORS, ROW_ARROWS, ROW_COUNT, ROW_SPEED = -1 };
#else
enum { ROW_SNAP_LOAD, ROW_SNAP_SAVE, ROW_DRIVES, ROW_SCREEN, ROW_SPEED, ROW_COLORS, ROW_ARROWS, ROW_COUNT };
#endif
char s_speedName[16];
const char *const kScreenNames[2] = { "Fit 320x240", "1:1 pan" };
const char *const kColorNames[4] = { "Black & white", "4", "16", "256" };

int colorsMode() {
  const int m = s_rtc.xpram[kColorsByte] - slotrom::kModeFirst;
  return m >= 0 && m <= 3 ? m : 0;
}

// A new hard disk: the Mac restarts with it (as a power cycle).
void restartWithDisks(const char *title) {
  ui::message(title, "Restarting the Mac", 600);
  hal_job_wait();
  savePram();
  for (sony::Image &im : s_sony.image) im.flush();
  releaseAll();
  if (!powerOn()) {
    ui::message("Error", s_error, 2500);
    s_running = false;
  }
}

// Another drive already holds `path`: its name, or null.
const char *driveWith(const std::string &path, int except) {
  for (int k = 0; k < sony::kDrives; k++) if (k != except && !path.empty() && s_path[k] == path) return kDriveNames[k];
  return nullptr;
}

// Puts path ("" = none) into floppy drive i while the Mac runs: 1 done, 0 the Mac must restart for
// it (its driver has no drives yet), -1 not done (*why says why).
int setFloppy(int i, const std::string &path, const char **why) {
  static char err[96];
  if (s_sony.hasDisk(i)) { *why = "Eject it in the Mac first (drag it to the Trash)"; return -1; }
  if (const char *other = driveWith(path, i)) { snprintf(err, sizeof err, "That disk is in %s", other); *why = err; return -1; }
  sony::Image &im = s_sony.image[i];
  im.close();
  if (!path.empty() && !im.open(path.c_str(), err, sizeof err)) { *why = err; s_path[i].clear(); return -1; }
  if (i == kCdrom) im.readOnly = true;
  settingOf(i) = path;
  settingsSave();
  s_path[i] = path;
  s_touched[i] = false;
  if (!s_sony.installed(i)) return 0;
  if (!path.empty()) s_sony.insert(i);
  return 1;
}

int macii_menuItems(CoreMenuItem *out, int max) {
  if (max < ROW_COUNT) return 0;
  out[ROW_SNAP_LOAD] = { "Load snapshot", nullptr, false, ui::ICON_LOAD };
  out[ROW_SNAP_SAVE] = { "Save snapshot", nullptr, false, ui::ICON_SAVE };
  out[ROW_DRIVES] = { "Drives", nullptr, false, ui::ICON_FOLDER };
  out[ROW_SCREEN] = { "Screen (Ctrl+Caps)", kScreenNames[s_screenLive == SCREEN_PAN], true, ui::ICON_PAD };
  if (kHasSpeed) {
    if (settingSpeed() == kSpeedMax) snprintf(s_speedName, sizeof s_speedName, "Max (%d.%dx)", s_speed / 4, s_speed % 4 * 25 / 10);
    else snprintf(s_speedName, sizeof s_speedName, "%dx", settingSpeed());
    out[ROW_SPEED] = { "CPU speed", s_speedName, true, ui::ICON_SKIP };
  }
  out[ROW_COLORS] = { "Colors (restarts)", kColorNames[colorsMode()], true, ui::ICON_GEAR };
  out[ROW_ARROWS] = { "Arrows (both Shifts)", kArrowNames[g_settings.arrows], true, ui::ICON_PAD };
  return ROW_COUNT;
}

// Drives: the two hard disks (a change restarts the Mac; it starts from hard disk 1), the two
// floppies and the CD-ROM, which take any image at once while empty (a disk the Mac has mounted is
// ejected there first).
constexpr int kMenuOrder[sony::kDrives] = { 0, 2, 1, 3, 4 };   // the hard disks first

bool drivesMenu() {
  macdrives::Drive drives[sony::kDrives];
  for (int r = 0; r < sony::kDrives; r++) {
    const int i = kMenuOrder[r];
    drives[r] = { kDriveNames[i], s_path[i],
                  i == 0 ? "The Mac starts from it: a change restarts it"
                         : i == kCdrom ? "A CD image (.iso, .cdr, .toast); read-only, goes in at once"
                         : isFloppy(i) ? "Any disk image; goes in at once" : "A change restarts the Mac",
                  isFloppy(i) ? ui::ICON_LOAD : ui::ICON_SAVE };
  }
  return macdrives::menu(kMacII, drives, sony::kDrives, [](int r, const std::string &path) {
    const int i = kMenuOrder[r];
    if (!isFloppy(i)) {
      if (!path.empty()) {
        if (const char *other = driveWith(path, i)) {
          ui::message(kDriveNames[i], (std::string("That disk is in ") + other).c_str(), 2000);
          return false;
        }
        sony::Image test;                           // not a disk image: say so, keep the Mac running
        char err[96];
        if (!test.open(path.c_str(), err, sizeof err)) { ui::message(kDriveNames[i], err, 2500); return false; }
        test.close();
      }
      settingOf(i) = path;
      settingsSave();
      s_path[i] = path;
      restartWithDisks(kDriveNames[i]);
      return true;
    }
    const char *why = nullptr;
    const int res = setFloppy(i, path, &why);
    if (res < 0) { ui::message(kDriveNames[i], why, 2500); return false; }
    if (res == 0) restartWithDisks(kDriveNames[i]); // the Mac is still starting: no drives yet
    else ui::message(kDriveNames[i], path.empty() ? "Ejected" : macdrives::fileName(path).c_str(), 700);
    return true;
  });
}

bool macii_menuAction(int index, uint8_t key) {
  if (index == ROW_DRIVES) return key == KEY_ENTER && drivesMenu();
  if (index == ROW_SPEED) {                         // at once: the VIAs keep real time
    g_settings.speed = (settingSpeed() - 1 + (key == KEY_LEFT ? -1 : 1) + kSpeedMax) % kSpeedMax + 1;
    applySpeedSetting();
    return false;
  }
  const int d = key == KEY_LEFT ? -1 : 1;
  switch (index) {
    case ROW_SNAP_SAVE: {
      if (key != KEY_ENTER) return false;
      std::string name, replace;
      if (!macsnap_pick_save(replace, name)) return false;
      s_snapSave = name;                            // saved once the menu has closed (runFrame)
      s_snapReplace = replace;
      return true;
    }
    case ROW_SNAP_LOAD: {
      if (key != KEY_ENTER) return false;
      std::string snap;
      if (!macsnap_pick("Load snapshot", snap)) return false;
      s_snapLoad = snap;
      return true;
    }
    case ROW_SCREEN:
      s_screenLive = s_screenLive == SCREEN_PAN ? SCREEN_FIT : SCREEN_PAN;
      g_settings.screen = s_screenLive;
      s_redraw = true;
      return false;
    case ROW_COLORS:
      s_rtc.xpram[kSlotPram] = 0x01; s_rtc.xpram[kSlotPram + 1] = 0x23;
      s_rtc.xpram[kColorsByte] = (uint8_t)(slotrom::kModeFirst + (colorsMode() + d + 4) % 4);
      s_rtc.dirty = true;
      s_colorsChanged = true;
      return false;
    case ROW_ARROWS:
      g_settings.arrows = (g_settings.arrows + d + ARROWS_COUNT) % ARROWS_COUNT;
      s_arrows = 0;
      return false;
  }
  return false;
}

const char *macii_lastError() {
  static char text[40];
  snprintf(text, sizeof text, "Cannot start the %s", kMachineName);
  return s_error[0] ? s_error : text;
}

} // namespace

// --- the 68020's bus (macii_bus.h) ------------------------------------------------------------------
extern "C" {
MaciiFast macii_fast;
unsigned short *macii_op_index = nullptr;
void (**macii_op_handlers)(void) = nullptr;
unsigned int macii_fetch16_slow(unsigned int a) { const uint32_t v = busRead<2>(a); fetchWindow(a); return v; }
unsigned int macii_fetch32_slow(unsigned int a) { const uint32_t v = busRead<4>(a); fetchWindow(a); return v; }
unsigned int macii_read8(unsigned int a) { return busRead<1>(a); }
unsigned int macii_read16(unsigned int a) { return busRead<2>(a); }
unsigned int macii_read32(unsigned int a) { return busRead<4>(a); }
void macii_write8(unsigned int a, unsigned int v) { busWrite<1>(a, v); }
void macii_write16(unsigned int a, unsigned int v) { busWrite<2>(a, v); }
void macii_write32(unsigned int a, unsigned int v) { busWrite<4>(a, v); }
int macii_irq_ack(int) { return M68K_INT_ACK_AUTOVECTOR; }

#if defined(MACII_QUADRA)
// The 68040's MMU registers (MOVEC, macquadra_mmu.h). Any change empties the TLB.
void macquadra_mmu_set(int reg, unsigned int v) {
  switch (reg) {
    case MACQUADRA_MMU_TC:
      if ((v & 0xC000) != (s_mmu.tc & 0xC000))
        hal_log("macquadra: TC %04x at pc %08x", (unsigned)v & 0xC000, (unsigned)m68k_get_reg(nullptr, M68K_REG_PPC));
      s_mmu.tc = v & 0xC000;
      s_mmuOn = (v & 0x8000) != 0;
      break;
    case MACQUADRA_MMU_URP: s_mmu.urp = v & ~0x1FFu; break;
    case MACQUADRA_MMU_SRP: s_mmu.srp = v & ~0x1FFu; break;
    case MACQUADRA_MMU_ITT0: s_mmu.itt0 = v & 0xFFFFE364u; break;
    case MACQUADRA_MMU_ITT1: s_mmu.itt1 = v & 0xFFFFE364u; break;
    case MACQUADRA_MMU_DTT0: s_mmu.dtt0 = v & 0xFFFFE364u; break;
    case MACQUADRA_MMU_DTT1: s_mmu.dtt1 = v & 0xFFFFE364u; break;
    case MACQUADRA_MMU_SR: s_mmu.sr = v; return;
  }
  tlbFlush();
}
unsigned int macquadra_mmu_get(int reg) {
  switch (reg) {
    case MACQUADRA_MMU_TC: return s_mmu.tc;
    case MACQUADRA_MMU_URP: return s_mmu.urp;
    case MACQUADRA_MMU_SRP: return s_mmu.srp;
    case MACQUADRA_MMU_ITT0: return s_mmu.itt0;
    case MACQUADRA_MMU_ITT1: return s_mmu.itt1;
    case MACQUADRA_MMU_DTT0: return s_mmu.dtt0;
    case MACQUADRA_MMU_DTT1: return s_mmu.dtt1;
    case MACQUADRA_MMU_SR: return s_mmu.sr;
  }
  return 0;
}
void macquadra_mmu_flush(void) { tlbFlush(); }
// PTEST: the MMUSR of a search (with the MMU off, the address itself, resident).
unsigned int macquadra_mmu_ptest(unsigned int addr, int fc, int write) {
  if (!s_mmuOn) return (addr & 0xFFFFF000u) | 1;
  uint32_t pa, sr;
  mmuWalk(addr, fc, write != 0, true, &pa, &sr);
  return sr;
}
#endif

#if defined(MACII_IICI)
// The PMMU's registers (maciici_mmu.h). TC: E (bit 31), SRE (25), FCL (24, ignored), PS (20-23), IS
// (16-19), TIA-TID; a TC whose fields do not add up to 32 is a configuration exception.
int maciici_mmu_set(int reg, unsigned long long v) {
  switch (reg) {
    case MACIICI_MMU_TC: {
      const uint32_t tc = (uint32_t)v;
      if (tc & 0x80000000u) {
        int sum = (tc >> 20 & 15) + (tc >> 16 & 15);
        for (int sh = 12; sh >= 0 && (tc >> sh & 15); sh -= 4) sum += tc >> sh & 15;
        if (sum != 32 || (tc >> 20 & 15) < 8) {
          hal_log("maciici: bad TC %08x at pc %08x", (unsigned)tc, (unsigned)m68k_get_reg(nullptr, M68K_REG_PPC));
          return 1;
        }
      }
      s_mmu.tc = tc;
      s_mmuOn = (tc & 0x80000000u) != 0;
      break;
    }
    case MACIICI_MMU_SRP: case MACIICI_MMU_CRP:
      if (((v >> 32) & 3) == 0) return 1;
      (reg == MACIICI_MMU_SRP ? s_mmu.srp : s_mmu.crp) = v;
      // No flush: the ATC keeps what PLOAD put there. _SwapMMUMode depends on it: it PLOADs the
      // address of the new TC, loads the CRP, and then reads the TC through the old mapping.
      return 0;
    case MACIICI_MMU_TT0: s_mmu.tt0 = (uint32_t)v & 0xFFFF8777u; break;
    case MACIICI_MMU_TT1: s_mmu.tt1 = (uint32_t)v & 0xFFFF8777u; break;
    case MACIICI_MMU_SR: s_mmu.sr = (uint16_t)v; return 0;
  }
  tlbFlush();
  return 0;
}
unsigned long long maciici_mmu_get(int reg) {
  switch (reg) {
    case MACIICI_MMU_TC: return s_mmu.tc;
    case MACIICI_MMU_SRP: return s_mmu.srp;
    case MACIICI_MMU_CRP: return s_mmu.crp;
    case MACIICI_MMU_TT0: return s_mmu.tt0;
    case MACIICI_MMU_TT1: return s_mmu.tt1;
    case MACIICI_MMU_SR: return s_mmu.sr;
  }
  return 0;
}
void maciici_mmu_flush(void) { tlbFlush(); }
unsigned int maciici_mmu_ptest(unsigned int addr, int fc, int write, int level, unsigned int *pa) {
  uint32_t p = addr;
  uint16_t sr = 0;
  if (s_mmuOn || level) mmuWalk(addr, fc, write != 0, false, level, &p, &sr);
  *pa = p;
  return sr;
}
void maciici_mmu_pload(unsigned int addr, int fc, int write) {
  if (!s_mmuOn) return;
  uint32_t p;
  uint16_t sr;
  // Into the translation cache, as the ATC.
  if (!mmuWalk(addr, fc, write != 0, true, 0, &p, &sr)) return;
  if (((s_mmu.tc >> 20) & 15) < 12 && !(sr & 0x40)) return;   // pages under 4 KB: not cached
  tlbFill(addr, p, false);   // reads only: a write must still go through the walk (the M bit)
}
#endif
void macii_pulse_reset(void) { s_pastStartup = false; }   // the ROM restarts: its tests again
// MACII_PC_HISTORY (a debugging build, -DMACII_PC_HISTORY): the last 64 jumps (PCs), printed
// with an illegal instruction.
#if defined(MACII_PC_HISTORY)
static unsigned s_pcHist[64], s_pcHistAt = 0;
static unsigned s_pcLong[4096], s_pcLongAt = 0;
void macii_instr_hook(unsigned int pc) {   // jumps only: a PC not just after the last one
  static unsigned last = 0;
  if (pc - last > 10) {
    s_pcHist[s_pcHistAt++ & 63] = pc;
    s_pcLong[s_pcLongAt++ & 4095] = pc;
    // MACII_TRAP=<hex pc>: the jumps before the first time the PC gets there
    static const unsigned trap = getenv("MACII_TRAP") ? (unsigned)strtoul(getenv("MACII_TRAP"), nullptr, 16) : 0;
    static bool done = false;
    if (trap && pc == trap && !done) {
      done = true;
      char line[64 * 9 + 1], *o = line;
      for (unsigned i = 0; i < 64; i++) o += sprintf(o, " %08x", s_pcHist[(s_pcHistAt + i) & 63]);
      hal_log("macii: the PCs before %08x:%s", trap, line);
      std::string l;
      unsigned prev = 0;
      for (unsigned i = 0; i < 4096; i++) {
        const unsigned v = s_pcLong[(s_pcLongAt + i) & 4095];
        if (v == prev) continue;
        prev = v;
        char t[12]; snprintf(t, sizeof t, " %08x", v); l += t;
        if (l.size() > 180) { hal_log("macii: long%s", l.c_str()); l.clear(); }
      }
      hal_log("macii: long%s", l.c_str());
      dbgDump();
    }
  }
  last = pc;
}
#endif

#if defined(MACII_OPCOUNT)   // DEBUG: which handlers run
static uint32_t s_opCount[65536];
void macii_instr_hook(unsigned int pc) { s_opCount[macii_fetch16(pc)]++; }
void opCountDump() {
  FILE *f = fopen(getenv("MACII_OPCOUNT_OUT") ? getenv("MACII_OPCOUNT_OUT") : "opcount.txt", "w");
  if (!f) return;
  fprintf(f, "base %p\n", (void *)&macii_illegal);
  for (int i = 0; i < 65536; i++) if (s_opCount[i]) fprintf(f, "%04x %p %u\n", i, (void *)m68kXi_static_instruction_jump_table[i], (unsigned)s_opCount[i]);
  fclose(f);
}
#endif

int macii_illegal(int opcode) {
  if ((opcode & 0xFFF0) == 0x7120) {                // the .Sony driver (macii_sony.h)
    s_pastStartup = true;
    const int32_t d0 = s_sony.op((uint16_t)opcode, m68k_get_reg(nullptr, M68K_REG_A0), m68k_get_reg(nullptr, M68K_REG_A1),
                                 m68k_get_reg(nullptr, M68K_REG_A2), m68k_get_reg(nullptr, M68K_REG_D1));
    m68k_set_reg(M68K_REG_D0, (uint32_t)d0);
    return 1;
  }
  if ((opcode & 0xFF00) == 0x7100) return driverOp((uint16_t)opcode) ? 1 : 0;
  static int n = 0;
  if (n < 32) {
    n++;
    hal_log("macii: illegal instruction %04x at pc %08x", (unsigned)opcode & 0xFFFF, (unsigned)m68k_get_reg(nullptr, M68K_REG_PPC));
#if defined(MACII_PC_HISTORY)
    char line[64 * 9 + 1], *o = line;
    for (unsigned i = 0; i < 64; i++) o += sprintf(o, " %08x", s_pcHist[(s_pcHistAt + i) & 63]);
    hal_log("macii: the PCs before:%s", line);
#endif
  }
  return 0;
}
}

// The desktop's bigshot (hal_sdl.cpp) asks the Plus core; the Mac II's whole screen, 1:1.
const uint16_t *maciicore_fullshot(int *w, int *h) {
  static uint16_t *buf = nullptr;
  if (!s_running) return nullptr;
  if (!buf) buf = (uint16_t *)hal_alloc_big(kScreenW * kScreenH * 2);
  if (!buf) return nullptr;
  static uint8_t idx[kScreenW];
  for (int y = 0; y < kScreenH; y++) {
    const uint32_t r = s_base + (uint32_t)y * s_rowBytes;
    if (r + s_rowBytes > kVramSize) break;
    rowIndices(s_vram + r, (int)s_depth, idx);
    for (int x = 0; x < kScreenW; x++) buf[y * kScreenW + x] = rgb565(s_clut[idx[x]]);
  }
  *w = kScreenW;
  *h = kScreenH;
  return buf;
}

// The desktop's MAC_KEYS "atXxY" / "mdown" / "mup" (hal_sdl.cpp): the pointer to (x, y) through the
// cursor globals, as moveMouse; the button through the ADB mouse. x < 0: the button only.
void maciicore_mouse(int x, int y, int button) {
  if (!s_running || !s_ram) return;
  if (x >= 0) {
    wr16(0x828, y); wr16(0x82A, x);
    wr16(0x82C, y); wr16(0x82E, x);
    s_ram[0x8CE] = s_ram[0x8CF];
  }
  if (button >= 0) { s_button = button != 0; s_adb.mouse(0, 0, s_button); }
}

// The desktop's MAC_KEYS "disk2": MAC_DISK2_LATER into floppy 1 while it runs (as the menu does).
void maciicore_disk2(const char *path) {
  if (!s_running) return;
  const char *why = "";
  const int r = setFloppy(1, path ? path : "", &why);
  hal_log("macii: floppy 1 %s: %s", path ? path : "(none)", r > 0 ? "inserted" : r == 0 ? "needs a restart" : why);
}

const EmuCore kMacII = {
#if defined(MACII_QUADRA)
  "Macintosh Quadra 700", "mac",
#elif defined(MACII_IICI)
  "Macintosh IIci", "mac",
#else
  "Macintosh II", "mac",
#endif
  ".dsk.img.image.hfv.hfs.dc42.diskcopy",
  macii_init, macii_loadRom, macii_reset, macii_runFrame, macii_setInput, macii_frameRate,
  nullptr, nullptr, macii_shutdown,
  macii_keyEvent, macii_menuItems, macii_menuAction, macii_lastError, true, macii_fpsText,
};
