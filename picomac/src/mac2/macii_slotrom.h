// macii_slotrom.h -- the declaration ROM of our video card (slot 9), built at start-up.
//
// A NuBus card describes itself in a ROM at the top of its slot space (Designing Cards and Drivers
// for the Macintosh Family, ch. 8): a format block in the last 20 bytes points at the sResource
// directory; the board sResource names the card; the video sResource gives the driver and one
// list of parameters per depth (1, 2, 4 and 8 bits, spIDs 0x80-0x83). On all four byte lanes.
//
// The driver (a DRVR, 68000 code) does the Mac side only: Open allocates a slot interrupt
// element and calls SIntInstall; the interrupt routine calls DoVBLTask (jVBLTask) for the slot.
// Everything else goes to the emulator through opcodes 0x71xx (illegal on a real 68020, as in
// Basilisk II), which macii_core.cpp handles: kEmulOpen, kEmulControl, kEmulStatus, kEmulClose
// (A0 = the parameter block, A1 = the DCE, the result in D0) and kEmulVblAck.
#pragma once

#include <stdint.h>
#include <string.h>

namespace slotrom {

constexpr uint16_t kEmulOpen = 0x7110, kEmulControl = 0x7111, kEmulStatus = 0x7112,
                   kEmulClose = 0x7113, kEmulVblAck = 0x7114;
constexpr uint8_t kModeFirst = 0x80;                // spIDs 0x80..0x83 = 1, 2, 4, 8 bits a pixel
constexpr int kSize = 2048;                         // bytes at the top of the slot

// The driver, hand-assembled (no 68k assembler here) and checked with Musashi's disassembler.
// Offsets in the header are from the DRVR's start.
const uint16_t kDriver[] = {
  0x4C00, 0x0000, 0x0000, 0x0000,                   // flags: needs lock, Control, Status; no delay
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000,           // Open, Prime, Control, Status, Close (patched)
};
// The code after the header and name, as words; labels are word indices into kCode.
const uint16_t kCode[] = {
  // 0 Open:
  0x48E7, 0x00C0,           //  0 movem.l a0-a1,-(sp)
  0x7010,                   //  2 moveq   #16,d0
  0xA71E,                   //  3 _NewPtrSysClear
  0x317C, 0x0006, 0x0004,   //  4 move.w  #6,4(a0)          sqType = sIQType
  0x43FA, 0x0000,           //  7 lea     Int(pc),a1        (patched)
  0x2149, 0x0008,           //  9 move.l  a1,8(a0)          sqAddr
  0x226F, 0x0004,           // 11 movea.l 4(sp),a1          the DCE
  0x7000,                   // 13 moveq   #0,d0
  0x1029, 0x0028,           // 14 move.b  $28(a1),d0        dCtlSlot
  0xA075,                   // 16 _SIntInstall
  0x4CDF, 0x0300,           // 17 movem.l (sp)+,a0-a1
  kEmulOpen,                // 19                           D0 = result
  0x4E75,                   // 20 rts
  // 21 Prime:
  0x70EF,                   // 21 moveq   #-17,d0           controlErr
  0x6008,                   // 22 bra.s   Done (from word 23: + 8 bytes)
  // 23 Control:
  kEmulControl,             // 23
  0x6004,                   // 24 bra.s   Done (from word 25: + 4 bytes)
  // 25 Status:
  kEmulStatus,              // 25
  0x4E71,                   // 26 nop
  // 27 Done:
  0x0828, 0x0009, 0x0006,   // 27 btst    #9,6(a0)          ioTrap: noQueue (immediate)?
  0x6702,                   // 30 beq.s   +2
  0x4E75,                   // 31 rts
  0x2F38, 0x08FC,           // 32 move.l  jIODone,-(sp)
  0x4E75,                   // 34 rts
  // 35 Close:
  kEmulClose,               // 35
  0x4E75,                   // 36 rts
  // 37 Int (the slot interrupt):
  kEmulVblAck,              // 37
  0x7009,                   // 38 moveq   #9,d0             the slot
  0x2078, 0x0D28,           // 39 movea.l jVBLTask,a0
  0x4E90,                   // 41 jsr     (a0)
  0x7001,                   // 42 moveq   #1,d0             handled
  0x4E75,                   // 43 rts
};
constexpr int kOpen = 0, kPrime = 21, kControl = 23, kStatus = 25, kClose = 35, kInt = 37;
constexpr int kLeaWord = 7;                         // the lea's displacement is word 8

class Builder {
 public:
  uint8_t b[kSize];
  int n = 0;

  void u8(uint8_t v) { b[n++] = v; }
  void u16(uint16_t v) { u8((uint8_t)(v >> 8)); u8((uint8_t)v); }
  void u32(uint32_t v) { u16((uint16_t)(v >> 16)); u16((uint16_t)v); }
  void cstr(const char *s) { do u8((uint8_t)*s); while (*s++); align(); }
  void align() { if (n & 1) u8(0); }
  // An sResource entry: id + a 24-bit offset to `target` (patched when the target is placed), or
  // id + 24 bits of data.
  int entry(uint8_t id) { const int at = n; u32((uint32_t)id << 24); return at; }
  void data(uint8_t id, uint32_t v) { u32((uint32_t)id << 24 | (v & 0xFFFFFF)); }
  void end() { u32(0xFF000000u); }
  void point(int at, int target) {             // the entry at `at` points at `target`
    const uint32_t off = (uint32_t)(target - at) & 0xFFFFFF;
    b[at + 1] = (uint8_t)(off >> 16); b[at + 2] = (uint8_t)(off >> 8); b[at + 3] = (uint8_t)off;
  }
};

// Builds the ROM; `out` receives kSize bytes, the last at slot offset 0xFFFFFF.
inline void build(uint8_t *out) {
  static Builder r;
  r.n = 0;
  memset(r.b, 0, sizeof r.b);

  const int dir = r.n;
  const int dBoard = r.entry(1), dVideo = r.entry(0x80);
  r.end();

  const int board = r.n;
  const int bType = r.entry(1), bName = r.entry(2);
  r.data(32, 0x0123);                               // BoardId: ours, made up
  const int bVendor = r.entry(36);
  r.end();

  const int video = r.n;
  const int vType = r.entry(1), vName = r.entry(2), vDrvr = r.entry(4);
  r.data(8, 1);                                     // sRsrcHWDevId
  const int vBase = r.entry(10), vLen = r.entry(11);
  int vMode[4];
  for (int i = 0; i < 4; i++) vMode[i] = r.entry((uint8_t)(kModeFirst + i));
  r.end();

  r.point(dBoard, board);
  r.point(dVideo, video);

  r.point(bType, r.n); r.u16(1); r.u16(0); r.u16(0); r.u16(0);              // catBoard
  r.point(bName, r.n); r.cstr("pico-mac video card");
  r.point(bVendor, r.n);
  { const int vId = r.entry(1); r.end(); r.point(vId, r.n); r.cstr("pico-mac"); }

  r.point(vType, r.n); r.u16(3); r.u16(1); r.u16(1); r.u16(0x0123);        // catDisplay, typVideo, drSwApple
  r.point(vName, r.n); r.cstr("Display_Video_Apple_PicoMac");
  r.point(vBase, r.n); r.u32(0);                    // the frame buffer at the slot's start
  r.point(vLen, r.n); r.u32(512 * 1024);

  r.point(vDrvr, r.n);
  const int drvDir = r.n;
  const int dd = r.entry(1);                        // sMacOS68000
  const int dd2 = r.entry(2);                       // sMacOS68020: the one a Mac II looks for
  r.end();
  r.point(dd, r.n);
  r.point(dd2, r.n);
  const int lenAt = r.n;
  r.u32(0);                                         // the block's length, patched below
  const int drvr = r.n;
  for (uint16_t w : kDriver) r.u16(w);
  const char name[] = ".Display_Video_Apple_PicoMac";
  r.u8((uint8_t)(sizeof name - 1));
  for (size_t i = 0; i < sizeof name - 1; i++) r.u8((uint8_t)name[i]);
  r.align();
  const int code = r.n;
  for (uint16_t w : kCode) r.u16(w);
  auto off = [&](int word) { return (uint16_t)(code + 2 * word - drvr); };
  auto put16 = [&](int at, uint16_t v) { r.b[at] = (uint8_t)(v >> 8); r.b[at + 1] = (uint8_t)v; };
  put16(drvr + 8, off(kOpen));
  put16(drvr + 10, off(kPrime));
  put16(drvr + 12, off(kControl));
  put16(drvr + 14, off(kStatus));
  put16(drvr + 16, off(kClose));
  put16(code + 2 * (kLeaWord + 1), (uint16_t)(2 * (kInt - kLeaWord - 1)));   // lea: from the extension word
  const uint32_t blockLen = (uint32_t)(r.n - lenAt);
  r.b[lenAt] = (uint8_t)(blockLen >> 24); r.b[lenAt + 1] = (uint8_t)(blockLen >> 16);
  r.b[lenAt + 2] = (uint8_t)(blockLen >> 8); r.b[lenAt + 3] = (uint8_t)blockLen;
  (void)drvDir;

  for (int i = 0; i < 4; i++) {                     // the modes: 640x480 at 1, 2, 4, 8 bits
    const int bpp = 1 << i;
    r.point(vMode[i], r.n);
    const int mParams = r.entry(1);
    r.data(3, 1);                                   // mPageCnt
    r.data(4, 0);                                   // mDevType: clutType
    r.end();
    r.point(mParams, r.n);
    r.u32(46);                                      // the block's length
    r.u32(0);                                       // vpBaseOffset
    r.u16((uint16_t)(640 * bpp / 8));               // vpRowBytes
    r.u16(0); r.u16(0); r.u16(480); r.u16(640);     // vpBounds
    r.u16(0); r.u16(0); r.u32(0);                   // vpVersion, vpPackType, vpPackSize
    r.u32(0x00480000); r.u32(0x00480000);           // 72 dpi
    r.u16(0);                                       // vpPixelType: indexed
    r.u16((uint16_t)bpp); r.u16(1); r.u16((uint16_t)bpp);   // vpPixelSize, vpCmpCount, vpCmpSize
    r.u32(0);                                       // vpPlaneBytes
  }

  // The format block, the last 20 bytes of the slot.
  const int used = r.n + 20;
  const int start = kSize - used;                   // where the ROM starts in `out`
  memset(out, 0, kSize);
  memcpy(out + start, r.b, (size_t)r.n);
  uint8_t *fh = out + kSize - 20;
  const uint32_t dirOff = (uint32_t)((start + dir) - (kSize - 20)) & 0xFFFFFF;
  auto w32 = [](uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; };
  w32(fh, dirOff);                                  // fhDirOffset
  w32(fh + 4, (uint32_t)used);                      // fhLength
  w32(fh + 8, 0);                                   // fhCRC (below)
  fh[12] = 1;                                       // fhROMRev
  fh[13] = 1;                                       // fhFormat: Apple
  w32(fh + 14, 0x5A932BC7);                         // fhTstPat
  fh[18] = 0;                                       // fhReserved
  fh[19] = 0x0F;                                    // fhByteLanes: all four
  uint32_t crc = 0;
  for (int i = start; i < kSize; i++) {
    crc = (crc << 1) | (crc >> 31);
    if (i < kSize - 12 || i >= kSize - 8) crc += out[i];
  }
  w32(fh + 8, crc);
}

}  // namespace slotrom
