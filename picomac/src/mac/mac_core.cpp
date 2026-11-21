// mac_core.cpp -- the Macintosh Plus core: umac (Matt Evans) + Musashi, behind the frontend's
// EmuCore interface (core.h). Laid out like pico-386's pc_core.cpp.
//
// A frame is 1/60.15 s of Mac time: the 68000 runs 130234 cycles (times the speed setting), then
// the vertical blank interrupt, and the frame's 370 sound samples are sent from the Mac's sound
// buffer. hal_audio_write() blocks while the output queue is full, which paces the Mac at real
// speed. The picture is drawn on the other core (hal_job_start) when the panel has finished the
// last one, and only when the Mac's screen changed.
//
// Screens: Fit scales the 512x342 screen to 320x214 in grey (8x8 source pixels -> 5x5, box
// filtered); 1:1 shows a 320x320 window of it that follows the pointer; Native patches the ROM
// for a 320x320 Mac screen (applied at power-on and Reset).
//
// Keys: Ctrl = Command, Sym = Option, Alt = the mouse button. The arrows move the pointer, or are
// the Mac's arrow keys (F1 switches). The frontend keeps Ctrl+Esc (menu) and its other hotkeys.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <string>
#include "../hal/hal.h"
#include "../frontend/ui.h"
#include "../frontend/settings.h"
#include "../frontend/browser.h"
#include "../../core.h"

extern "C" {
#include "umac/umac.h"
#include "umac/rom.h"
#include "umac/keymap.h"
}

extern const EmuCore kMacPlus;

namespace {

constexpr uint32_t kFrameCycles = 130234;           // 7833600 Hz / 60.15 Hz
constexpr uint32_t kRate = 22255;                   // the Mac's sound: one sample per scan line
constexpr int kStripH = 10;                         // the bottom strip: FPS meter and notes
constexpr uint32_t kMacEpochOffset = 2082844800u;   // 1904-01-01 to 1970-01-01, in seconds

const uint32_t kRamSizes[RAM_COUNT] = { 512u << 10, 1u << 20, 2u << 20, 4u << 20 };
const char *const kRamNames[RAM_COUNT] = { "512 KB", "1 MB", "2 MB", "4 MB" };
const char *const kScreenNames[SCREEN_COUNT] = { "Fit (grey)", "1:1 pan", "Native 320x320" };

// --- machine ------------------------------------------------------------------------------------
uint8_t *s_ram = nullptr;
uint8_t *s_rom = nullptr;
uint32_t s_ramSize = 0;
bool s_running = false;
char s_error[96];
std::string s_diskPath;
int s_screenLive = SCREEN_FIT;      // the mode the ROM was patched for (native or not)

// --- the disk image: read and written in place on the card -------------------------------------
FILE *s_disk = nullptr;
uint32_t s_diskOffset = 0;          // 84 for a DiskCopy 4.2 image
bool s_diskDirty = false;

int diskRead(void *, uint8_t *data, unsigned offset, unsigned len) {
  if (!s_disk || fseek(s_disk, (long)(s_diskOffset + offset), SEEK_SET)) return -1;
  return fread(data, 1, len, s_disk) == len ? 0 : -1;
}

int diskWrite(void *, uint8_t *data, unsigned offset, unsigned len) {
  if (!s_disk || fseek(s_disk, (long)(s_diskOffset + offset), SEEK_SET)) return -1;
  s_diskDirty = true;
  return fwrite(data, 1, len, s_disk) == len ? 0 : -1;
}

uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// Opens the image (read-write if it can) and fills d. A DiskCopy 4.2 image's 84-byte header is
// skipped (its checksum goes stale once the Mac writes; Mini vMac and BasiliskII don't mind).
bool openDisk(const std::string &path, disc_descr_t &d) {
  memset(&d, 0, sizeof d);
  if (path.empty()) return true;                    // no disk: the Mac shows the "?" floppy
  bool ro = false;
  s_disk = fopen(path.c_str(), "r+b");
  if (!s_disk) { s_disk = fopen(path.c_str(), "rb"); ro = true; }
  if (!s_disk) { snprintf(s_error, sizeof s_error, "Cannot open the disk image"); return false; }
  fseek(s_disk, 0, SEEK_END);
  const long size = ftell(s_disk);
  uint8_t h[84];
  s_diskOffset = 0;
  uint32_t data = (uint32_t)size;
  fseek(s_disk, 0, SEEK_SET);
  if (size > 84 && fread(h, 1, 84, s_disk) == 84 && h[0] < 64 && h[0x52] == 1 && h[0x53] == 0 &&
      84 + (long)be32(h + 0x40) + (long)be32(h + 0x44) == size) {
    s_diskOffset = 84;
    data = be32(h + 0x40);
  }
  if (data < 400 * 1024 || (data & 511)) {
    snprintf(s_error, sizeof s_error, "Not a Mac disk image (%ld bytes)", size);
    fclose(s_disk);
    s_disk = nullptr;
    return false;
  }
  d.size = data;
  d.read_only = ro;
  d.op_read = diskRead;
  d.op_write = diskWrite;
  hal_log("mac: disk %s, %u KB%s%s", path.c_str(), (unsigned)(data >> 10), s_diskOffset ? ", DiskCopy 4.2" : "",
          ro ? ", read-only" : "");
  return true;
}

void closeDisk() {
  if (s_disk) fclose(s_disk);
  s_disk = nullptr;
  s_diskDirty = false;
}

// --- PRAM and the clock: <sd>/picomac/pram.bin = 20 PRAM bytes + the clock (LE) -----------------
std::string pramPath() { return sdPath("picomac/pram.bin"); }

void loadPram() {
  uint8_t b[24] = {};
  FILE *f = fopen(pramPath().c_str(), "rb");
  bool have = f && fread(b, 1, 24, f) == 24;
  if (f) fclose(f);
  if (have) memcpy(umac_rtc_pram(), b, 20);
  // The host's clock when it has one (the desktop; a P4 without network time starts in 1970),
  // else where the Mac's clock was when it last stopped.
  const time_t now = time(nullptr);
  uint32_t secs = have ? (uint32_t)b[20] | b[21] << 8 | b[22] << 16 | (uint32_t)b[23] << 24 : 0;
  if (now > 1577836800) {                           // after 2020: a real clock
    // the Mac keeps local time: add the zone's offset (UTC read back as local time)
    struct tm g = *gmtime(&now);
    g.tm_isdst = -1;
    const long off = (long)(now - mktime(&g));
    secs = (uint32_t)(now + off) + kMacEpochOffset;
  }
  if (secs < kMacEpochOffset) secs = kMacEpochOffset + 1735689600u;   // 2025-01-01
  umac_rtc_set_seconds(secs);
}

void savePram() {
  uint8_t b[24];
  memcpy(b, umac_rtc_pram(), 20);
  const uint32_t s = umac_rtc_seconds();
  b[20] = s; b[21] = s >> 8; b[22] = s >> 16; b[23] = s >> 24;
  makeDirs(sdPath("picomac"));
  if (FILE *f = fopen(pramPath().c_str(), "wb")) { fwrite(b, 1, 24, f); fclose(f); }
}

// --- video --------------------------------------------------------------------------------------
uint16_t s_grey[65];                // 0..64 white weight -> RGB565
uint8_t s_byteW[256][5];            // a source byte's 8 pixels -> the black weight (0..8) of 5 dest pixels
uint8_t *s_prevFb = nullptr;        // the last screen drawn, to skip unchanged frames
uint32_t s_fbBytes = 0;
bool s_redraw = true;
int s_panX = 0, s_panY = 0;
volatile bool s_stripUsed = false;  // the FPS meter or a note is up: the bottom 10 rows are theirs
uint32_t s_noteUntil = 0;

// 8 source pixels -> 5 destination pixels: each covers 1.6 source pixels, in fifths.
struct Tap { uint8_t src, w; };
const Tap kTaps[5][3] = {
  { { 0, 5 }, { 1, 3 }, { 0, 0 } },
  { { 1, 2 }, { 2, 5 }, { 3, 1 } },
  { { 3, 4 }, { 4, 4 }, { 0, 0 } },
  { { 4, 1 }, { 5, 5 }, { 6, 2 } },
  { { 6, 3 }, { 7, 5 }, { 0, 0 } },
};

void initTables() {
  for (int i = 0; i <= 64; i++) {
    const int v = i * 255 / 64;
    s_grey[i] = (uint16_t)(((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3));
  }
  for (int b = 0; b < 256; b++)
    for (int d = 0; d < 5; d++) {
      int w = 0;
      for (const Tap &t : kTaps[d]) if (t.w && (b & (0x80 >> t.src))) w += t.w;
      s_byteW[b][d] = (uint8_t)w;
    }
}

constexpr int kFitW = 320, kFitH = 214, kFitY = (SCREEN_H - kStripH - kFitH) / 2;

void drawFit(const uint8_t *fb, uint16_t *out) {
  uint8_t acc[kFitW];
  for (int dy = 0; dy < kFitH; dy++) {
    memset(acc, 0, sizeof acc);
    for (const Tap &t : kTaps[dy % 5]) {
      if (!t.w) continue;
      const int sy = (dy / 5) * 8 + t.src;
      if (sy >= 342) continue;
      const uint8_t *row = fb + sy * 64;
      uint8_t *a = acc;
      for (int bx = 0; bx < 64; bx++, a += 5) {
        const uint8_t *w = s_byteW[row[bx]];
        a[0] += w[0] * t.w; a[1] += w[1] * t.w; a[2] += w[2] * t.w; a[3] += w[3] * t.w; a[4] += w[4] * t.w;
      }
    }
    uint16_t *o = out + (kFitY + dy) * SCREEN_W;
    for (int x = 0; x < kFitW; x++) o[x] = s_grey[64 - acc[x]];
  }
}

// A w x h window at (x0, y0) of a 1-bit screen `pitch` bytes wide, 1:1. x0 is a multiple of 8.
void drawMono(const uint8_t *fb, int pitch, int x0, int y0, int w, int h, uint16_t *out) {
  for (int y = 0; y < h; y++) {
    const uint8_t *row = fb + (y0 + y) * pitch + x0 / 8;
    uint16_t *o = out + y * SCREEN_W;
    for (int bx = 0; bx < w / 8; bx++) {
      const uint8_t b = row[bx];
      for (int i = 0; i < 8; i++) *o++ = (b & (0x80 >> i)) ? 0x0000 : 0xFFFF;
    }
  }
}

// Keeps the pointer (the Mac's Mouse global at 0x830: v, h) at least 48 pixels inside the window.
void followPointer(int visH) {
  const int my = (int16_t)(s_ram[0x830] << 8 | s_ram[0x831]);
  const int mx = (int16_t)(s_ram[0x832] << 8 | s_ram[0x833]);
  const int m = 48;
  if (mx < s_panX + m) s_panX = mx - m;
  if (mx > s_panX + SCREEN_W - m) s_panX = mx - SCREEN_W + m;
  if (my < s_panY + m) s_panY = my - m;
  if (my > s_panY + visH - m) s_panY = my - visH + m;
  if (s_panX > 512 - SCREEN_W) s_panX = 512 - SCREEN_W;
  if (s_panY > 342 - visH) s_panY = 342 - visH;
  if (s_panX < 0) s_panX = 0;
  if (s_panY < 0) s_panY = 0;
  s_panX &= ~7;
}

// The drawing job (core 0 on the P4).
void drawJob(void *) {
  if (!hal_video_ready()) return;
  const uint8_t *fb = s_ram + umac_get_fb_offset();
  const int visH = s_stripUsed ? SCREEN_H - kStripH : SCREEN_H;
  const int px = s_panX, py = s_panY;
  if (s_screenLive == SCREEN_PAN) followPointer(visH);
  if (!s_redraw && px == s_panX && py == s_panY && !memcmp(fb, s_prevFb, s_fbBytes)) return;
  memcpy(s_prevFb, fb, s_fbBytes);
  s_redraw = false;
  uint16_t *back = hal_video_back();
  switch (s_screenLive) {
    case SCREEN_FIT:
      drawFit(s_prevFb, back);
      hal_video_submit(kFitY, kFitH);
      break;
    case SCREEN_PAN:
      drawMono(s_prevFb, 64, s_panX, s_panY, SCREEN_W, visH, back);
      hal_video_submit(0, visH);
      break;
    default:
      drawMono(s_prevFb, umac_disp_w / 8, 0, 0, umac_disp_w, visH, back);
      hal_video_submit(0, visH);
      break;
  }
}

// A line of text in the bottom strip for 2 s; null clears the strip.
void showNote(const char *text) {
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

// --- input --------------------------------------------------------------------------------------
bool s_mouseArrows = true;          // the arrows move the pointer (else they are the Mac's arrows)
uint8_t s_arrows = 0;               // held arrows: 1 left, 2 right, 4 up, 8 down
int s_arrowFrames = 0;
bool s_button = false;
bool s_shiftHeld = false;           // the real Shift keys
uint8_t s_down[256];                // per PicoCalc key code: the Mac key it pressed + 1 (0 = none)
bool s_downShift[256];              // ... with a Shift we added

// ASCII -> Mac key code, and whether it needs Shift.
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

// The Mac key for a PicoCalc key, 0xFF for none; *shift = it needs Shift held.
uint8_t macKey(uint8_t code, bool *shift) {
  *shift = false;
  if (code >= 'a' && code <= 'z') return kLetters[code - 'a'];
  if (code >= 'A' && code <= 'Z') { *shift = true; return kLetters[code - 'A']; }
  for (const AsciiKey &k : kAscii) if ((uint8_t)k.c == code) { *shift = k.shift; return k.mkc; }
  switch (code) {
    case KEY_ENTER: return MKC_Return;
    case KEY_BACKSPACE: case KEY_DEL: return MKC_BackSpace;
    case KEY_TAB: return MKC_Tab;
    case KEY_ESC: return MKC_Grave;                 // a Plus keyboard has ` where Esc is now
    case KEY_CTRL: return MKC_Command;
    case KEY_SYM: return MKC_Option;
    case KEY_SHIFT_L: case KEY_SHIFT_R: return MKC_Shift;
    case KEY_LEFT: return MKC_Left;
    case KEY_RIGHT: return MKC_Right;
    case KEY_UP: return MKC_Up;
    case KEY_DOWN: return MKC_Down;
  }
  return 0xFF;
}

void releaseAll() {
  for (int c = 0; c < 256; c++) {
    if (!s_down[c]) continue;
    umac_kbd_event(s_down[c] - 1, 0);
    if (s_downShift[c] && !s_shiftHeld) umac_kbd_event(MKC_Shift, 0);
    s_down[c] = 0;
    s_downShift[c] = false;
  }
  s_arrows = 0;
  s_button = false;
  s_shiftHeld = false;
  umac_mouse(0, 0, 0);
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

void mac_keyEvent(uint8_t code, uint8_t state) {
  if (!s_running) return;
  if (code == KEY_RELEASE_ALL) { releaseAll(); return; }
  if (state == KEYSTATE_HELD) return;               // the Mac repeats keys itself
  const bool down = state == KEYSTATE_PRESSED;
  if (code == KEY_ALT) { s_button = down; umac_mouse(0, 0, s_button); return; }
  if (code == KEY_F1 && down) {
    s_mouseArrows = !s_mouseArrows;
    s_arrows = 0;
    showNote(s_mouseArrows ? "Arrows: mouse (Alt = button)  F1: keys" : "Arrows: Mac arrow keys  F1: mouse");
    return;
  }
  if (const uint8_t a = arrowBit(code)) {
    if (!down) s_arrows &= ~a;                      // either mode: the mode may have changed
    if (s_mouseArrows && !s_down[code]) {
      if (down) { s_arrows |= a; if (s_arrows == a) s_arrowFrames = 0; } else s_arrows &= ~a;
      return;
    }
  }
  if (code == KEY_SHIFT_L || code == KEY_SHIFT_R) s_shiftHeld = down;
  if (down) {
    if (s_down[code]) return;
    bool shift;
    const uint8_t k = macKey(code, &shift);
    if (k == 0xFF) return;
    if (shift && !s_shiftHeld) umac_kbd_event(MKC_Shift, 1);
    s_downShift[code] = shift;
    umac_kbd_event(k, 1);
    s_down[code] = k + 1;
  } else {
    if (!s_down[code]) return;
    umac_kbd_event(s_down[code] - 1, 0);
    if (s_downShift[code] && !s_shiftHeld) umac_kbd_event(MKC_Shift, 0);
    s_down[code] = 0;
    s_downShift[code] = false;
  }
}

// The held arrows move the pointer, faster the longer they are held.
void moveMouse() {
  if (!s_arrows) return;
  int step = 1 + s_arrowFrames / 6;
  if (step > 8) step = 8;
  s_arrowFrames++;
  const int dx = ((s_arrows & 2) ? step : 0) - ((s_arrows & 1) ? step : 0);
  const int dy = ((s_arrows & 4) ? step : 0) - ((s_arrows & 8) ? step : 0);   // umac: +y is up
  umac_mouse(dx, dy, s_button);
}

// --- power --------------------------------------------------------------------------------------
int16_t s_mix[UMAC_SND_SAMPLES];
uint32_t s_secFrac = 0;             // 1 Hz interrupt: 60.15 frames a second, in 1/1000 frames
uint32_t s_frames = 0;
uint32_t s_speedT0 = 0, s_speedF0 = 0;
char s_speed[48];
uint32_t s_lastFrameMs = 0;

// Loads and patches the ROM, sizes the screen and RAM, and starts the 68000. The disk stays open.
bool powerOn() {
  const std::string romPath = sdPath(g_settings.romPath.c_str());
  FILE *f = fopen(romPath.c_str(), "rb");
  if (!f) { snprintf(s_error, sizeof s_error, "No ROM: put it at /%s", g_settings.romPath.c_str()); return false; }
  const size_t got = fread(s_rom, 1, ROM_SIZE, f);
  fclose(f);
  if (got != ROM_SIZE) { snprintf(s_error, sizeof s_error, "The ROM must be 128 KB (a Mac Plus ROM)"); return false; }

  s_screenLive = g_settings.screen;
  umac_disp_w = s_screenLive == SCREEN_NATIVE ? SCREEN_W : 512;
  umac_disp_h = s_screenLive == SCREEN_NATIVE ? SCREEN_H : 342;
  if (rom_patch(s_rom)) {
    snprintf(s_error, sizeof s_error, "Not a Mac Plus ROM (v3, 4D1F8172)");
    return false;
  }
  const uint32_t ram = kRamSizes[g_settings.ram];
  if (ram != s_ramSize) {
    hal_free(s_ram);
    s_ram = (uint8_t *)hal_alloc_big(ram + 4);      // +4: an unaligned long read at the very top
    s_ramSize = s_ram ? ram : 0;
    if (!s_ram) { snprintf(s_error, sizeof s_error, "Out of memory for %s of RAM", kRamNames[g_settings.ram]); return false; }
  }
  memset(s_ram, 0, s_ramSize + 4);
  s_fbBytes = (uint32_t)umac_disp_w * umac_disp_h / 8;

  disc_descr_t discs[DISC_NUM_DRIVES];
  memset(discs, 0, sizeof discs);
  if (!s_diskPath.empty() && !s_disk && !openDisk(s_diskPath, discs[0])) return false;
  if (s_disk) {
    // reopened by a restart: describe it again
    fseek(s_disk, 0, SEEK_END);
    discs[0].size = (unsigned)(ftell(s_disk) - (long)s_diskOffset);
    if (s_diskOffset) {
      uint8_t h[4];
      fseek(s_disk, 0x40, SEEK_SET);
      if (fread(h, 1, 4, s_disk) == 4) discs[0].size = be32(h);
    }
    discs[0].op_read = diskRead;
    discs[0].op_write = diskWrite;
  }
  umac_init(s_ram, s_rom, s_ramSize, discs);
  umac_set_speed(g_settings.speed);
  loadPram();
  s_redraw = true;
  s_panX = s_panY = 0;
  return true;
}

// --- EmuCore --------------------------------------------------------------------------------------
bool mac_init() {
  initTables();
  s_rom = (uint8_t *)hal_alloc_big(ROM_SIZE);
  s_prevFb = (uint8_t *)hal_alloc_big(512 * 342 / 8);
  s_error[0] = 0;
  return s_rom && s_prevFb;
}

// path: the boot disk image, or "" to start without one.
bool mac_loadRom(const char *path) {
  s_diskPath = path ? path : "";
  closeDisk();
  memset(s_down, 0, sizeof s_down);
  memset(s_downShift, 0, sizeof s_downShift);
  s_arrows = 0;
  s_button = false;
  s_shiftHeld = false;
  if (!powerOn()) { closeDisk(); return false; }
  hal_audio_start(kRate);
  s_running = true;
  s_frames = s_secFrac = 0;
  s_speedT0 = hal_micros();
  s_speedF0 = 0;
  s_noteUntil = 0;
  s_stripUsed = g_settings.showFps;
  hal_log("mac: started, %u KB RAM, screen %dx%d", (unsigned)(s_ramSize >> 10), umac_disp_w, umac_disp_h);
  return true;
}

// Reset is a power cycle: the ROM is patched again, so a new screen mode or RAM size applies.
void mac_reset() {
  if (!s_running) return;
  hal_job_wait();
  savePram();
  releaseAll();
  if (s_disk) fflush(s_disk);
  if (!powerOn()) {
    ui::message("Error", s_error, 2500);
    s_running = false;
  }
  hal_panel_fill(0, 0, PANEL_W, PANEL_H, 0);
}

void mac_runFrame(bool draw) {
  if (!s_running) {
    hal_delay(16);
    return;
  }
  // Back from the menu or the browser (they drew over the panel): a whole picture again.
  const uint32_t ms = hal_millis();
  if (ms - s_lastFrameMs > 200) s_redraw = true;
  s_lastFrameMs = ms;
  moveMouse();
  umac_run(kFrameCycles * (uint32_t)g_settings.speed);
  umac_vsync_event();
  s_secFrac += 1000;
  if (s_secFrac >= 60150) { s_secFrac -= 60150; umac_1hz_event(); }
  s_frames++;

  // sound: the buffer holds one word per scan line, the sample in its high byte
  const uint8_t *snd = umac_sound_buffer();
  const int vol = umac_sound_enabled() ? umac_sound_volume() + 1 : 0;   // 0..8
  for (int i = 0; i < UMAC_SND_SAMPLES; i++) s_mix[i] = (int16_t)(((int)snd[2 * i] - 128) * vol * 24);
  hal_audio_write(s_mix, UMAC_SND_SAMPLES);

  if (s_frames % 60 == 0) {
    if (s_diskDirty && s_disk) { fflush(s_disk); s_diskDirty = false; }
    if (umac_rtc_pram_dirty()) savePram();
  }
  if (s_noteUntil && (int32_t)(hal_millis() - s_noteUntil) >= 0) {
    s_noteUntil = 0;
    if (!g_settings.showFps) showNote(nullptr);
  }
  if (g_settings.showFps != s_stripUsed && !s_noteUntil) { s_stripUsed = g_settings.showFps; s_redraw = true; }
  if (draw && hal_video_ready()) hal_job_start(drawJob, nullptr);
}

void mac_setInput(int, uint32_t) {}
uint32_t mac_frameRate() { return 60; }

void mac_shutdown() {
  hal_job_wait();
  if (s_running) savePram();
  s_running = false;
  closeDisk();
  hal_free(s_ram);
  hal_free(s_rom);
  hal_free(s_prevFb);
  s_ram = s_rom = s_prevFb = nullptr;
  s_ramSize = 0;
}

// Speed as a share of a real Mac Plus.
const char *mac_fpsText() {
  const uint32_t now = hal_micros(), us = now - s_speedT0;
  if (us) {
    const uint64_t cyc = (uint64_t)(s_frames - s_speedF0) * kFrameCycles * g_settings.speed;
    snprintf(s_speed, sizeof s_speed, "%u%% of a Plus", (unsigned)(cyc * 100 * 1000000 / UMAC_CPU_HZ / us));
  }
  s_speedT0 = now;
  s_speedF0 = s_frames;
  return s_speed;
}

// --- in-game menu rows ------------------------------------------------------------------------
enum { ROW_SCREEN, ROW_SPEED, ROW_ARROWS, ROW_DISK, ROW_COUNT };
char s_speedName[8];
std::string s_diskName;

int mac_menuItems(CoreMenuItem *out, int max) {
  if (max < ROW_COUNT) return 0;
  snprintf(s_speedName, sizeof s_speedName, "%dx", g_settings.speed);
  const size_t sl = s_diskPath.find_last_of('/');
  s_diskName = s_diskPath.empty() ? "None" : s_diskPath.substr(sl == std::string::npos ? 0 : sl + 1);
  out[ROW_SCREEN] = { "Screen", kScreenNames[g_settings.screen], true, ui::ICON_PAD };
  out[ROW_SPEED] = { "CPU speed", s_speedName, true, ui::ICON_SKIP };
  out[ROW_ARROWS] = { "Arrows (F1)", s_mouseArrows ? "Mouse" : "Keys", true, ui::ICON_PAD };
  out[ROW_DISK] = { "Boot disk", s_diskName.c_str(), false, ui::ICON_FOLDER };
  return ROW_COUNT;
}

bool mac_menuAction(int index, uint8_t key) {
  const int d = key == KEY_LEFT ? -1 : 1;
  switch (index) {
    case ROW_SCREEN: {
      g_settings.screen = (g_settings.screen + d + SCREEN_COUNT) % SCREEN_COUNT;
      const bool wasNative = s_screenLive == SCREEN_NATIVE, native = g_settings.screen == SCREEN_NATIVE;
      if (wasNative == native) { s_screenLive = g_settings.screen; s_redraw = true; }
      else ui::message("Screen", "Applies at the next Reset", 900);
      return false;
    }
    case ROW_SPEED:
      g_settings.speed = (g_settings.speed - 1 + d + 4) % 4 + 1;
      umac_set_speed(g_settings.speed);
      return false;
    case ROW_ARROWS:
      s_mouseArrows = !s_mouseArrows;
      return false;
    case ROW_DISK: {
      if (key != KEY_ENTER) return false;
      std::string path;
      if (!browseRom(kMacPlus, path)) return false;
      g_settings.disk = path;
      settingsSave();
      ui::message("Boot disk", "Restarting the Mac", 600);
      hal_job_wait();
      savePram();
      closeDisk();
      s_diskPath = path;
      releaseAll();
      if (!powerOn()) {
        ui::message("Error", s_error, 2500);
        s_running = false;
      }
      return true;
    }
  }
  return false;
}

const char *mac_lastError() { return s_error[0] ? s_error : "Cannot start the Mac"; }

} // namespace

// The desktop's bigshot (hal_sdl.cpp): the Mac's whole screen.
const uint16_t *maccore_fullshot(int *w, int *h) {
  static uint16_t *buf = nullptr;
  if (!s_running) return nullptr;
  if (!buf) buf = (uint16_t *)hal_alloc_big(512 * 342 * 2);
  const uint8_t *fb = s_ram + umac_get_fb_offset();
  *w = umac_disp_w;
  *h = umac_disp_h;
  for (int i = 0; i < *w * *h; i++) buf[i] = (fb[i >> 3] & (0x80 >> (i & 7))) ? 0x0000 : 0xFFFF;
  return buf;
}

const EmuCore kMacPlus = {
  "Macintosh Plus", "mac", ".dsk.img.image.hfv.dc42.diskcopy",
  mac_init, mac_loadRom, mac_reset, mac_runFrame, mac_setInput, mac_frameRate,
  nullptr, nullptr, mac_shutdown,
  mac_keyEvent, mac_menuItems, mac_menuAction, mac_lastError, true, mac_fpsText,
};
