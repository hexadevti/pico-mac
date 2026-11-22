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
// Keys: Ctrl = Command, Sym = Option, Alt = the mouse button. The arrows move the pointer, are the
// Mac's arrow keys, or both (as pico-386: both Shifts together, Ctrl+Tab or F1 switch). Ctrl+Caps
// Lock switches the screen between Fit and 1:1. The frontend keeps Ctrl+Esc (menu) and its other
// hotkeys.
//
// Snapshots (mac_snapshot.cpp): saved and loaded from the in-game menu, once it has closed.
#include <stdio.h>
#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <string>
#include "../hal/hal.h"
#include "../frontend/ui.h"
#include "../frontend/settings.h"
#include "../frontend/browser.h"
#include "../../core.h"
#include "mac_snapshot.h"

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
std::string s_diskPath;             // drive 1: the boot disk
std::string s_disk2Path;            // drive 2: a second disk, mounted once the Mac is up
int s_screenLive = SCREEN_FIT;      // the mode the ROM was patched for (native or not)

// --- the disk images: read and written in place on the card ------------------------------------
struct Disk {
  FILE *f = nullptr;
  uint32_t offset = 0;              // 84 for a DiskCopy 4.2 image
  uint32_t size = 0;                // the disk's data, in bytes
  bool readOnly = false;
  bool dirty = false;
  bool touched = false;             // written since the Mac started or the last snapshot
  std::string path;
};
Disk s_disks[DISC_NUM_DRIVES];

int diskRead(void *ctx, uint8_t *data, unsigned offset, unsigned len) {
  Disk &d = *(Disk *)ctx;
  if (!d.f || fseek(d.f, (long)(d.offset + offset), SEEK_SET)) return -1;
  return fread(data, 1, len, d.f) == len ? 0 : -1;
}

int diskWrite(void *ctx, uint8_t *data, unsigned offset, unsigned len) {
  Disk &d = *(Disk *)ctx;
  if (!d.f || fseek(d.f, (long)(d.offset + offset), SEEK_SET)) return -1;
  d.dirty = true;
  if (!d.touched) { d.touched = true; macsnap_touch(d.path); }   // snapshots before it no longer fit
  return fwrite(data, 1, len, d.f) == len ? 0 : -1;
}

uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// Opens the image (read-write if it can). A DiskCopy 4.2 image's 84-byte header is skipped (its
// checksum goes stale once the Mac writes; Mini vMac and BasiliskII don't mind). Anything else
// (.dsk, .img, .hfs...) is taken as the raw disk. `what` names the disk in errors.
bool openDisk(const std::string &path, Disk &d, const char *what) {
  d = Disk();
  d.path = path;
  d.f = fopen(path.c_str(), "r+b");
  if (!d.f) { d.f = fopen(path.c_str(), "rb"); d.readOnly = true; }
  if (!d.f) { snprintf(s_error, sizeof s_error, "Cannot open the %s", what); return false; }
  fseek(d.f, 0, SEEK_END);
  const long size = ftell(d.f);
  uint8_t h[84];
  uint32_t data = (uint32_t)size;
  fseek(d.f, 0, SEEK_SET);
  if (size > 84 && fread(h, 1, 84, d.f) == 84 && h[0] < 64 && h[0x52] == 1 && h[0x53] == 0 &&
      84 + (long)be32(h + 0x40) + (long)be32(h + 0x44) == size) {
    d.offset = 84;
    data = be32(h + 0x40);
  }
  if (data < 400 * 1024 || (data & 511)) {
    snprintf(s_error, sizeof s_error, "The %s is not a Mac disk image (%ld bytes)", what, size);
    fclose(d.f);
    d = Disk();
    return false;
  }
  d.size = data;
  hal_log("mac: %s %s, %u KB%s%s", what, path.c_str(), (unsigned)(data >> 10), d.offset ? ", DiskCopy 4.2" : "",
          d.readOnly ? ", read-only" : "");
  return true;
}

// The open disk as umac sees it; no file = no disk in that drive.
void describeDisk(Disk &d, disc_descr_t &out) {
  memset(&out, 0, sizeof out);
  if (!d.f) return;
  out.size = d.size;
  out.read_only = d.readOnly;
  out.op_ctx = &d;
  out.op_read = diskRead;
  out.op_write = diskWrite;
}

void flushDisks() {
  for (Disk &d : s_disks)
    if (d.f && d.dirty) { fflush(d.f); d.dirty = false; }
}

void closeDisks() {
  for (Disk &d : s_disks) {
    if (d.f) fclose(d.f);
    d = Disk();
  }
}

// --- PRAM and the clock: <sd>/picomac/pram.bin = 20 PRAM bytes + the clock (LE) -----------------
std::string pramPath() { return sdPath("picomac/pram.bin"); }

// The host's time in the Mac's terms (local time, from 1904), when it has a clock: the desktop
// does, a P4 without network time starts in 1970. false = none, *secs unchanged.
bool hostClock(uint32_t *secs) {
  const time_t now = time(nullptr);
  if (now <= 1577836800) return false;              // before 2020: no real clock
  // the Mac keeps local time: add the zone's offset (UTC read back as local time)
  struct tm g = *gmtime(&now);
  g.tm_isdst = -1;
  const long off = (long)(now - mktime(&g));
  *secs = (uint32_t)(now + off) + kMacEpochOffset;
  return true;
}

void loadPram() {
  uint8_t b[24] = {};
  FILE *f = fopen(pramPath().c_str(), "rb");
  bool have = f && fread(b, 1, 24, f) == 24;
  if (f) fclose(f);
  if (have) memcpy(umac_rtc_pram(), b, 20);
  // The host's clock when it has one, else where the Mac's clock was when it last stopped.
  uint32_t secs = have ? (uint32_t)b[20] | b[21] << 8 | b[22] << 16 | (uint32_t)b[23] << 24 : 0;
  hostClock(&secs);
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
int s_wholeLeft = 0;                 // whole pictures still owed (drawJob: one per buffer)
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
      // The last output row reaches past row 341: repeat that row, or the missing weight
      // leaves a grey line under the picture (2026-11-22).
      int sy = (dy / 5) * 8 + t.src;
      if (sy > 341) sy = 341;
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
  if (!s_redraw && !s_wholeLeft && px == s_panX && py == s_panY && !memcmp(fb, s_prevFb, s_fbBytes)) return;
  memcpy(s_prevFb, fb, s_fbBytes);
  // A whole picture goes to both buffers in turn: the in-game menu shows the front buffer
  // whole as its backdrop, and the Fit bands of a buffer that only got partial frames still held
  // whatever was there before the boot.
  if (s_redraw) s_wholeLeft = 2;
  const bool whole = s_wholeLeft > 0;
  if (whole) s_wholeLeft--;
  s_redraw = false;
  uint16_t *back = hal_video_back();
  // The strip's rows are never drawn here, but the menu's backdrop shows them too.
  if (whole && visH < SCREEN_H) memset(back + visH * SCREEN_W, 0, (SCREEN_H - visH) * SCREEN_W * 2);
  switch (s_screenLive) {
    case SCREEN_FIT:
      drawFit(s_prevFb, back);
      if (whole) {
        // The bands above and below the picture: black, over whatever the menu or the browser
        // left there (they stayed on the P4's panel, 2026-11-22).
        memset(back, 0, kFitY * SCREEN_W * 2);
        memset(back + (kFitY + kFitH) * SCREEN_W, 0, (visH - kFitY - kFitH) * SCREEN_W * 2);
        hal_video_submit(0, visH);
      } else {
        hal_video_submit(kFitY, kFitH);
      }
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
enum { ARROWS_KEYS, ARROWS_MOUSE, ARROWS_BOTH, ARROWS_COUNT };   // g_settings.arrows
const char *const kArrowNames[ARROWS_COUNT] = { "Keys", "Mouse", "Both" };
bool s_shiftL = false, s_shiftR = false, s_ctrlHeld = false;   // for the hotkeys
uint8_t s_arrows = 0;               // held arrows: 1 left, 2 right, 4 up, 8 down
uint32_t s_arrowSince = 0;          // when the arrows started to be held (the acceleration)
uint32_t s_arrowLast = 0;           // the last frame an arrow was held
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

// The arrows' mode (ARROWS_*), kept for the next start, with a note.
void setArrows(int mode) {
  g_settings.arrows = mode;
  s_arrows = 0;
  settingsSave();
  showNote(mode == ARROWS_KEYS    ? "Arrows: Mac arrow keys   (both Shifts: next)"
           : mode == ARROWS_MOUSE ? "Arrows: mouse, Alt = button   (both Shifts: next)"
                                  : "Arrows: mouse + arrow keys   (both Shifts: next)");
}

// Ctrl+Caps Lock: the 512x342 screen scaled to fit, or 1:1 following the pointer. A Native screen
// is the Mac's own size and has nothing to zoom.
void toggleZoom() {
  if (s_screenLive == SCREEN_NATIVE) { showNote("Zoom: not with the Native screen"); return; }
  hal_job_wait();
  s_screenLive = s_screenLive == SCREEN_FIT ? SCREEN_PAN : SCREEN_FIT;
  hal_video_wait();
  hal_panel_fill(0, 0, PANEL_W, PANEL_H, 0);
  showNote(s_screenLive == SCREEN_PAN ? "Zoom 1:1 (Ctrl+Caps Lock: off)" : "Zoom off");
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
  if (code == KEY_RELEASE_ALL) {
    releaseAll();
    s_shiftL = s_shiftR = s_ctrlHeld = false;
    return;
  }
  const int mode = g_settings.arrows;
  // A held arrow keeps the pointer going even if its press was lost (a KEY_RELEASE_ALL).
  if (state == KEYSTATE_HELD && !(mode != ARROWS_KEYS && arrowBit(code) && !s_down[code])) return;   // the Mac repeats keys itself
  const bool down = state != KEYSTATE_RELEASED;
  const bool pressed = state == KEYSTATE_PRESSED;
  if (code == KEY_SHIFT_L) s_shiftL = down;
  if (code == KEY_SHIFT_R) s_shiftR = down;
  if (code == KEY_CTRL) s_ctrlHeld = down;
  // Both Shifts together, Ctrl+Tab or F1 (as pico-386): what the arrows do next. The first Shift
  // (or Ctrl) has already reached the Mac; releaseAll lets go of it.
  if (pressed && ((code == KEY_SHIFT_L && s_shiftR) || (code == KEY_SHIFT_R && s_shiftL) ||
                  (code == KEY_TAB && s_ctrlHeld) || code == KEY_F1)) {
    releaseAll();
    setArrows((mode + 1) % ARROWS_COUNT);
    return;
  }
  // Ctrl+Caps Lock (as pico-386's zoom): Fit <-> 1:1, until the menu or a Reset sets it again.
  if (code == KEY_CAPS && s_ctrlHeld) {
    if (pressed) {
      releaseAll();
      toggleZoom();
    }
    return;
  }
  if (code == KEY_ALT) { s_button = down; umac_mouse(0, 0, s_button); return; }
  if (const uint8_t a = arrowBit(code)) {
    if (!down) s_arrows &= ~a;                      // any mode: the mode may have changed
    else if (mode != ARROWS_KEYS && !s_down[code]) {
      // The keyboard may send a release and a new press while the key is held, or lose an
      // event: only a real pause (no arrow for 250 ms) starts the acceleration again.
      if (!s_arrows && hal_millis() - s_arrowLast > 250) s_arrowSince = hal_millis();
      s_arrows |= a;
    }
    // Mouse: the pointer only (Both: the Mac's arrow key too). A key pressed in another mode
    // still gets its release.
    if (mode == ARROWS_MOUSE && !s_down[code]) return;
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

uint16_t rd16(uint32_t a) { return (uint16_t)(s_ram[a] << 8 | s_ram[a + 1]); }
void wr16(uint32_t a, int v) { s_ram[a] = (uint8_t)(v >> 8); s_ram[a + 1] = (uint8_t)v; }

// The held arrows move the pointer, faster the longer they are held.
// The pointer is set through the Mac's own globals (as Mini vMac does): MTemp (0x828) and
// RawMouse (0x82C), then CrsrNew (0x8CE) = CrsrCouple (0x8CF) so the VBL task redraws it. The
// SCC quadrature path (umac_mouse) delivers one pixel per interrupt, and with a step queued on
// every slice the ROM can read the direction of the next step mid-handler.
void moveMouse() {
  if (!s_arrows) return;
  // The step grows with the time held, not with frames: the P4 may not run every frame at 60 Hz.
  const uint32_t now = hal_millis();
  s_arrowLast = now;
  // Speed in 1/16 pixel per frame: 1/2 pixel for the first 250 ms (fine positioning), then up
  // steadily to 6 pixels at 2 s. The fractions add up in s_arrowFrac.
  const uint32_t held = now - s_arrowSince;
  int speed = 8;
  if (held > 250) speed = held >= 2000 ? 96 : 8 + (int)((held - 250) * 88 / 1750);
  static int s_arrowFrac = 0;
  if (held < 20) s_arrowFrac = 0;
  s_arrowFrac += speed;
  const int step = s_arrowFrac >> 4;
  s_arrowFrac &= 15;
  if (!step) return;
  const int dx = ((s_arrows & 2) ? step : 0) - ((s_arrows & 1) ? step : 0);
  const int dy = ((s_arrows & 8) ? step : 0) - ((s_arrows & 4) ? step : 0);   // Mac: +v is down
  const int top = (int16_t)rd16(0x834), left = (int16_t)rd16(0x836);         // CrsrPin
  const int bottom = (int16_t)rd16(0x838), right = (int16_t)rd16(0x83A);
  if (bottom <= top || right <= left || bottom > 1024 || right > 1024) {     // not set up yet
    umac_mouse(dx, -dy, s_button);                                           // umac: +y is up
    return;
  }
  // From where the pointer is (Mouse, 0x830), unless the VBL task has not yet taken the last
  // move (CrsrNew still set): then from MTemp. Building on MTemp alone drifted behind Mouse, and
  // the ROM's own acceleration then cut each later step short.
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

// --- power --------------------------------------------------------------------------------------
int16_t s_mix[UMAC_SND_SAMPLES];
int s_sndStart = 0;                 // where the driver's ring starts (see app_loop)

// Sample k of the sound buffer now, at the Mac's volume (0 when sound is off).
static int16_t soundSample(int k) {
  const int vol = umac_sound_enabled() ? umac_sound_volume() + 1 : 0;   // 0..8
  return (int16_t)(((int)umac_sound_buffer()[2 * k] - 128) * vol * 16);
}

// The coupling capacitor of the Plus's output stage (one-pole high-pass, ~20 Hz). The 8-bit
// samples carry a DC offset (the driver's "silence" need not be 128, and the level jumps as
// sound is switched on and off), which the PicoCalc's small amp would otherwise get.
// No low-pass: a 6 kHz one made the sound muffled (2026-11-22).
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
static SoundFilter s_soundFilter;
uint32_t s_secFrac = 0;             // 1 Hz interrupt: 60.15 frames a second, in 1/1000 frames
uint32_t s_frames = 0;
uint32_t s_speedT0 = 0, s_speedF0 = 0;
char s_speed[48];
uint32_t s_underrunsShown = 0;
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
  // A Reset keeps the disks open.
  if (!s_diskPath.empty() && !s_disks[0].f && !openDisk(s_diskPath, s_disks[0], "boot disk")) return false;
  if (!s_disk2Path.empty() && !s_disks[1].f) {
    if (s_disk2Path == s_diskPath) {
      snprintf(s_error, sizeof s_error, "The second disk is the boot disk");
      return false;
    }
    if (!openDisk(s_disk2Path, s_disks[1], "second disk")) return false;
  }
  for (int i = 0; i < DISC_NUM_DRIVES; i++) {
    s_disks[i].touched = false;                     // a new start: the next write is a new generation
    describeDisk(s_disks[i], discs[i]);
  }
  umac_init(s_ram, s_rom, s_ramSize, discs);
  umac_set_speed(g_settings.speed);
  loadPram();
  s_redraw = true;
  s_panX = s_panY = 0;
  return true;
}

// --- snapshots (mac_snapshot.cpp) ---------------------------------------------------------------
// Saved and loaded once the menu has closed (mac_runFrame), as pico-386 does.
std::string s_snapSave, s_snapReplace, s_snapLoad;  // the save's name, the snapshot it overwrites

void saveSnapshot() {
  hal_job_wait();
  showNote("Saving snapshot...");
  flushDisks();
  MacSnapMachine m;
  m.disk = s_diskPath;
  m.disk2 = s_disk2Path;
  m.rom = g_settings.romPath;
  for (int i = 0; i < RAM_COUNT; i++) if (kRamSizes[i] == s_ramSize) m.ram = i;
  m.screen = s_screenLive == SCREEN_NATIVE ? SCREEN_NATIVE : g_settings.screen == SCREEN_NATIVE ? SCREEN_FIT : g_settings.screen;
  const char *e = macsnap_save(s_snapSave, s_snapReplace, m, s_ram + umac_get_fb_offset(), umac_disp_w, umac_disp_h);
  // The disks as they are now go with it: their next write is a new generation.
  if (!e) for (Disk &d : s_disks) d.touched = false;
  showNote(e ? e : ("Saved: " + s_snapSave).c_str());
  s_snapSave.clear();
  s_snapReplace.clear();
}

// Starts the Mac the snapshot needs (its disks, ROM, RAM and screen become the settings) and
// loads its state. A snapshot that cannot be loaded leaves a fresh start with those disks.
bool startFromSnapshot(const std::string &snap) {
  MacSnapMachine m;
  if (!macsnap_machine(snap, m)) { snprintf(s_error, sizeof s_error, "Cannot read the snapshot"); return false; }
  g_settings.disk = m.disk;
  g_settings.disk2 = m.disk2;
  g_settings.romPath = m.rom;
  g_settings.ram = m.ram;
  g_settings.screen = m.screen;
  settingsSave();
  s_diskPath = m.disk;
  s_disk2Path = m.disk2;
  closeDisks();
  releaseAll();
  if (!powerOn()) return false;
  bool broken;
  if (const char *e = macsnap_restore(snap, &broken)) {
    ui::message("Snapshot", e, 2500);
    hal_panel_fill(0, 0, PANEL_W, PANEL_H, 0);
    return broken ? powerOn() : true;               // not "broken": nothing was loaded
  }
  umac_set_speed(g_settings.speed);                 // the speed is the menu's, not the snapshot's
  // The clock moves on to now when there is one: the RTC and the System's copy (Time, 0x20C).
  uint32_t secs;
  if (hostClock(&secs)) {
    umac_rtc_set_seconds(secs);
    s_ram[0x20C] = (uint8_t)(secs >> 24); s_ram[0x20D] = (uint8_t)(secs >> 16);
    s_ram[0x20E] = (uint8_t)(secs >> 8); s_ram[0x20F] = (uint8_t)secs;
  }
  s_redraw = true;
  return true;
}

// --- debugging a hung Mac: "@mac" on the P4's console ("dump" in MAC_KEYS) -----------------------
// The PC after each umac_run slice, for 30 frames; then the registers, the VIA, the most frequent
// PCs and the code at the top one, in hal_log lines.
constexpr int kDbgPcs = 8192;
volatile bool s_dbgWant = false;
uint32_t *s_dbgPcs = nullptr;
int s_dbgFrames = 0;

void dbgCommand(const char *line) {
  if (!strcmp(line, "@mac")) s_dbgWant = true;
}

uint32_t ramLong(uint32_t a) {
  a &= s_ramSize - 1;
  return (uint32_t)s_ram[a] << 24 | s_ram[a + 1] << 16 | s_ram[a + 2] << 8 | s_ram[a + 3];
}

// A word of RAM or ROM (0x400000), without side effects.
unsigned macWord(uint32_t a) {
  a &= 0xFFFFFE;
  if (a < 0x400000) { a &= s_ramSize - 1; return s_ram[a] << 8 | s_ram[a + 1]; }
  if (a < 0x600000) { a &= ROM_SIZE - 1; return s_rom[a] << 8 | s_rom[a + 1]; }
  return 0;
}

void dbgDump() {
  char b[400];
  hal_log("@mac frame %u, %d PCs", (unsigned)s_frames, umac_pc_sampled());
  umac_dump_regs(b, sizeof b);
  for (char *l = strtok(b, "\n"); l; l = strtok(nullptr, "\n")) hal_log("@mac %s", l);
  hal_log("@mac VIA ORA %02x ORB %02x DDRA %02x DDRB %02x ACR %02x PCR %02x IFR %02x IER %02x",
          via_peek(15), via_peek(0), via_peek(3), via_peek(2), via_peek(11), via_peek(12),
          via_peek(13), via_peek(14));
  hal_log("@mac Ticks %08x  Time %08x  MBState %02x",
          (unsigned)ramLong(0x16A), (unsigned)ramLong(0x20C), s_ram[0x172]);
  // The most frequent PCs.
  const int n = umac_pc_sampled();
  umac_pc_sample(nullptr, 0);                       // no more, while they are sorted
  for (int i = 0; i < n; i++) s_dbgPcs[i] &= 0xFFFFFF;   // 24-bit addresses
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
  for (int k = 0; k < 12 && topN[k]; k++) hal_log("@mac pc %06x x%d", (unsigned)top[k], topN[k]);
  // The code at the three top PCs, as words (the disassembler is not built in).
  for (int k = 0; k < 3 && topN[k]; k++) {
    const uint32_t from = (top[k] - 0x20) & 0xFFFFFE;
    for (uint32_t a = from; a < from + 0x60; a += 16) {
      char *o = b;
      o += sprintf(o, "@mac %06x:", (unsigned)a);
      for (int i = 0; i < 16; i += 2) o += sprintf(o, " %04x", macWord(a + i));
      hal_log("%s", b);
    }
  }
  hal_log("@mac end");
}

// Before a frame: start or finish a "@mac".
void dbgStep() {
  if (s_dbgWant) {
    s_dbgWant = false;
    if (!s_dbgPcs) s_dbgPcs = (uint32_t *)hal_alloc_big(kDbgPcs * 4);
    if (!s_dbgPcs) { hal_log("@mac no memory"); hal_log("@mac end"); return; }
    umac_pc_sample(s_dbgPcs, kDbgPcs);
    s_dbgFrames = 30;
  } else if (s_dbgFrames && --s_dbgFrames == 0) {
    dbgDump();
  }
}

// --- EmuCore --------------------------------------------------------------------------------------
bool mac_init() {
  initTables();
  hal_serial_cmd = dbgCommand;
  s_rom = (uint8_t *)hal_alloc_big(ROM_SIZE);
  s_prevFb = (uint8_t *)hal_alloc_big(512 * 342 / 8);
  s_error[0] = 0;
  return s_rom && s_prevFb;
}

// path: the boot disk image, or "" to start without one. The second disk is g_settings.disk2.
// A snapshot set with macsnap_set_pending is started from instead.
bool mac_loadRom(const char *path) {
  const std::string snap = macsnap_take_pending();
  s_snapSave.clear();
  s_snapLoad.clear();
  s_diskPath = path ? path : "";
  s_disk2Path = g_settings.disk2;
  closeDisks();
  memset(s_down, 0, sizeof s_down);
  memset(s_downShift, 0, sizeof s_downShift);
  s_arrows = 0;
  s_button = false;
  s_shiftHeld = false;
  s_shiftL = s_shiftR = s_ctrlHeld = false;
  if (!(snap.empty() ? powerOn() : startFromSnapshot(snap))) { closeDisks(); return false; }
  hal_audio_start(kRate);
  s_running = true;
  s_frames = s_secFrac = 0;
  s_speedT0 = hal_micros();
  s_speedF0 = 0;
  s_noteUntil = 0;
  s_stripUsed = g_settings.showFps;
  s_redraw = true;                                  // the whole panel: the menu is still on it
  hal_log("mac: started, %u KB RAM, screen %dx%d", (unsigned)(s_ramSize >> 10), umac_disp_w, umac_disp_h);
  return true;
}

// Reset is a power cycle: the ROM is patched again, so a new screen mode or RAM size applies.
void mac_reset() {
  if (!s_running) return;
  hal_job_wait();
  savePram();
  releaseAll();
  flushDisks();
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
  if (!s_snapSave.empty()) saveSnapshot();
  if (!s_snapLoad.empty()) {
    std::string snap;
    snap.swap(s_snapLoad);
    ui::message("Snapshot", "Restarting from it", 600);
    hal_job_wait();
    savePram();
    flushDisks();
    if (!startFromSnapshot(snap)) {
      ui::message("Error", s_error, 2500);
      s_running = false;
    }
    hal_panel_fill(0, 0, PANEL_W, PANEL_H, 0);
    return;
  }
  // Back from the menu or the browser (they drew over the panel): a whole picture again.
  const uint32_t ms = hal_millis();
  if (ms - s_lastFrameMs > 200) s_redraw = true;
  s_lastFrameMs = ms;
  moveMouse();
  // Sound. The hardware reads one sample of the buffer per scan line, sample 0 at the VBL.
  // The driver refills it from the VBL interrupt as a ring: the ROM's boot beep from sample 0,
  // the Sound Manager from wherever it left off (37 in a PoP trace), and stays ahead of the
  // beam. Reading the buffer in plain order (0..369, by line or at the frame's end) put two
  // seams in every frame, at 0 and at the ring's start: a hiss and a "blown speaker" sound,
  // worse at higher speeds (2026-11-22). So umac notes the first sample written in the frame
  // (the ring's start, P), and the frame plays P..369, 0..P-1: the samples in the order they
  // were written, and next frame's P follows this frame's P-1, whatever the speed.
  dbgStep();
  umac_sound_mark_reset();
  umac_run(kFrameCycles * (uint32_t)g_settings.speed);
  {
    static int fixes = 0;
    if (umac_vbl_fixes() != fixes) {
      fixes = umac_vbl_fixes();
      hal_log("mac: VBL queue loop cut (%d)", fixes);
    }
  }
  {
    const int p = umac_sound_first_write();
    if (p >= 0) s_sndStart = p;
    for (int i = 0; i < UMAC_SND_SAMPLES; i++) {
      int k = s_sndStart + i;
      if (k >= UMAC_SND_SAMPLES) k -= UMAC_SND_SAMPLES;
      s_mix[i] = soundSample(k);
    }
  }
  umac_vsync_event();
  s_secFrac += 1000;
  if (s_secFrac >= 60150) { s_secFrac -= 60150; umac_1hz_event(); }
  s_frames++;

  for (int i = 0; i < UMAC_SND_SAMPLES; i++) s_mix[i] = s_soundFilter.run(s_mix[i]);
  hal_audio_write(s_mix, UMAC_SND_SAMPLES);

  if (s_frames % 60 == 0) {
    flushDisks();
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
  closeDisks();
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
    // gaps: sound output buffers that ran dry since the last reading (each one is a click)
    const uint32_t under = hal_audio_underruns();
    snprintf(s_speed, sizeof s_speed, "%u%% of a Plus  %u gaps", (unsigned)(cyc * 100 * 1000000 / UMAC_CPU_HZ / us),
             (unsigned)(under - s_underrunsShown));
    s_underrunsShown = under;
  }
  s_speedT0 = now;
  s_speedF0 = s_frames;
  return s_speed;
}

// --- in-game menu rows ------------------------------------------------------------------------
// The snapshots first, right below Resume (as pico-386).
enum { ROW_SNAP_LOAD, ROW_SNAP_SAVE, ROW_SCREEN, ROW_SPEED, ROW_ARROWS, ROW_DISK, ROW_DISK2, ROW_COUNT };
char s_speedName[8];
std::string s_diskName, s_disk2Name;

std::string baseName(const std::string &path) {
  if (path.empty()) return "None";
  const size_t sl = path.find_last_of('/');
  return path.substr(sl == std::string::npos ? 0 : sl + 1);
}

// A new boot or second disk: the Mac restarts with it (as a power cycle).
void restartWithDisks(const char *title) {
  ui::message(title, "Restarting the Mac", 600);
  hal_job_wait();
  savePram();
  closeDisks();
  releaseAll();
  if (!powerOn()) {
    ui::message("Error", s_error, 2500);
    s_running = false;
  }
}

int mac_menuItems(CoreMenuItem *out, int max) {
  if (max < ROW_COUNT) return 0;
  snprintf(s_speedName, sizeof s_speedName, "%dx", g_settings.speed);
  s_diskName = baseName(s_diskPath);
  s_disk2Name = baseName(s_disk2Path);
  out[ROW_SNAP_LOAD] = { "Load snapshot", nullptr, false, ui::ICON_LOAD };
  out[ROW_SNAP_SAVE] = { "Save snapshot", nullptr, false, ui::ICON_SAVE };
  out[ROW_SCREEN] = { "Screen", kScreenNames[g_settings.screen], true, ui::ICON_PAD };
  out[ROW_SPEED] = { "CPU speed", s_speedName, true, ui::ICON_SKIP };
  out[ROW_ARROWS] = { "Arrows (both Shifts)", kArrowNames[g_settings.arrows], true, ui::ICON_PAD };
  out[ROW_DISK] = { "Boot disk", s_diskName.c_str(), false, ui::ICON_FOLDER };
  // Enter picks the second disk, </> eject it
  out[ROW_DISK2] = { "Second disk", s_disk2Name.c_str(), !s_disk2Path.empty(), ui::ICON_FOLDER };
  return ROW_COUNT;
}

bool mac_menuAction(int index, uint8_t key) {
  const int d = key == KEY_LEFT ? -1 : 1;
  switch (index) {
    case ROW_SNAP_SAVE: {
      if (key != KEY_ENTER) return false;
      std::string name, replace;
      if (!macsnap_pick_save(replace, name)) return false;
      s_snapSave = name;                            // saved once the menu has closed (mac_runFrame)
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
      g_settings.arrows = (g_settings.arrows + d + ARROWS_COUNT) % ARROWS_COUNT;
      s_arrows = 0;
      return false;
    case ROW_DISK: {
      if (key != KEY_ENTER) return false;
      std::string path;
      if (!browseRom(kMacPlus, path)) return false;
      g_settings.disk = path;
      settingsSave();
      s_diskPath = path;
      restartWithDisks("Boot disk");
      return true;
    }
    case ROW_DISK2: {
      std::string path;
      if (key == KEY_ENTER) {
        if (!browseRom(kMacPlus, path)) return false;
      } else if (s_disk2Path.empty()) {
        return false;
      }
      g_settings.disk2 = path;
      settingsSave();
      s_disk2Path = path;
      restartWithDisks("Second disk");
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
  "Macintosh Plus", "mac", ".dsk.img.image.hfv.hfs.dc42.diskcopy",
  mac_init, mac_loadRom, mac_reset, mac_runFrame, mac_setInput, mac_frameRate,
  nullptr, nullptr, mac_shutdown,
  mac_keyEvent, mac_menuItems, mac_menuAction, mac_lastError, true, mac_fpsText,
};
