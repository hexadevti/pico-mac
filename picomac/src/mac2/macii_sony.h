// macii_sony.h -- disks for the Mac II: the ROM's .Sony (floppy) driver is replaced by ours, which
// hands Open, Prime (read/write), Control and Status to the emulator through opcodes 0x7120-0x7123
// (as the video driver does, macii_slotrom.h). The disk images are files on the card, read and
// written in place, of any size (a bare HFS volume such as a System 7 image works as it is).
//
// The C++ side is our version of umac's disc.c, which is a cut-down Basilisk II sony.cpp
// (Basilisk II (C) 1997-2008 Christian Bauer, GPLv2; umac (C) 2024 Matt Evans). The 68k driver is
// umac's sonydrv.S (docs/umac) with opcodes instead of its magic address, hand-assembled.
//
// Open adds both drives, empty or not: drive 1 (the boot disk) and drive 2, whose disk is mounted
// once the Mac is up (accRun posts a diskEvt for it). A disk can go into drive 2 at any time while
// it is empty (insert()); the Mac ejects it (Control 7) when it is dragged to the Trash.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace sony {

constexpr uint16_t kOpOpen = 0x7120, kOpPrime = 0x7121, kOpControl = 0x7122, kOpStatus = 0x7123;
constexpr int kDrives = 2;

// The driver: header, name, code. Branches are byte displacements from the word after them.
const uint16_t kDriver[] = {
  0x6F00, 0x0000, 0x0000, 0x0000,   // flags: read, write, control, status, needs time, locked
  24 + 2 * 0, 24 + 2 * 31, 24 + 2 * 33, 24 + 2 * 52, 24 + 2 * 71,   // Open Prime Control Status Close
  0x052E, 0x536F, 0x6E79,           // "\p.Sony"
  // 0 Open: one drive per index (d3); kOpOpen returns its number in d0, or 0 = no more.
  0x48E7, 0x10F0,                   //  0 movem.l d3/a0-a3,-(sp)
  0x7600,                           //  2 moveq   #0,d3
  0x701E,                           //  3 moveq   #30,d0          the drive status record
  0xA71E,                           //  4 _NewPtrSysClear
  0x2208,                           //  5 move.l  a0,d1
  0x6728,                           //  6 beq.s   Fail (27)
  0x2448,                           //  7 movea.l a0,a2
  0x4CEF, 0x0300, 0x0004,           //  8 movem.l 4(sp),a0-a1     the pb and DCE again
  0x2203,                           // 11 move.l  d3,d1
  kOpOpen,                          // 12                         a2 = the record, d1 = index
  0x4A80,                           // 13 tst.l   d0
  0x6F10,                           // 14 ble.s   Done (23)
  0x4840,                           // 15 swap    d0              the drive number, high word
  0x303C, 0xFFFB,                   // 16 move.w  #-5,d0          .Sony's refNum
  0x41EA, 0x0006,                   // 18 lea     6(a2),a0        the drive queue element
  0xA04E,                           // 20 _AddDrive
  0x5283,                           // 21 addq.l  #1,d3
  0x60D8,                           // 22 bra.s   3
  0x4CDF, 0x0F08,                   // 23 Done: movem.l (sp)+,d3/a0-a3
  0x7000,                           // 25 moveq   #0,d0
  0x4E75,                           // 26 rts
  0x4CDF, 0x0F08,                   // 27 Fail: movem.l (sp)+,d3/a0-a3
  0x70E9,                           // 29 moveq   #-23,d0         openErr
  0x4E75,                           // 30 rts
  // 31 Prime:
  kOpPrime,                         // 31
  0x6028,                           // 32 bra.s   IOReturn (53)
  // 33 Control: accRun (65) returns a drive to mount, which is posted as a diskEvt.
  kOpControl,                       // 33
  0x0C68, 0x0041, 0x001A,           // 34 cmpi.w  #65,csCode(a0)
  0x6612,                           // 37 bne.s   47
  0x4A80,                           // 38 tst.l   d0
  0x6F1A,                           // 39 ble.s   IOReturn (53)
  0x2F08,                           // 40 move.l  a0,-(sp)
  0x307C, 0x0007,                   // 41 movea.w #7,a0           diskEvt
  0xA02F,                           // 43 _PostEvent              message = the drive number
  0x205F,                           // 44 movea.l (sp)+,a0
  0x7000,                           // 45 moveq   #0,d0
  0x600C,                           // 46 bra.s   IOReturn (53)
  0x0C68, 0x0001, 0x001A,           // 47 cmpi.w  #1,csCode(a0)   KillIO: no completion
  0x6604,                           // 50 bne.s   IOReturn (53)
  0x4E75,                           // 51 rts
  // 52 Status:
  kOpStatus,                        // 52
  // 53 IOReturn:
  0x3228, 0x0006,                   // 53 move.w  ioTrap(a0),d1
  0x0801, 0x0009,                   // 55 btst    #9,d1           noQueue (immediate)?
  0x670C,                           // 57 beq.s   64
  0x4A40,                           // 58 tst.w   d0
  0x6F02,                           // 59 ble.s   61
  0x4240,                           // 60 clr.w   d0
  0x3140, 0x0010,                   // 61 move.w  d0,ioResult(a0)
  0x4E75,                           // 63 rts
  0x4A40,                           // 64 tst.w   d0              queued
  0x6F04,                           // 65 ble.s   68
  0x4240,                           // 66 clr.w   d0
  0x4E75,                           // 67 rts
  0x2F38, 0x08FC,                   // 68 move.l  jIODone,-(sp)
  0x4E75,                           // 70 rts
  // 71 Close:
  0x70E8,                           // 71 moveq   #-24,d0         closErr
  0x4E75,                           // 72 rts
};

// Replaces the ROM's .Sony (its 18-byte header, then "\p.Sony") with ours, and fixes the ROM's
// checksum (the first long: the sum of the words after it). False if it is not there.
inline bool patchRom(uint8_t *rom, uint32_t size) {
  static const uint8_t name[] = { 5, '.', 'S', 'o', 'n', 'y' };
  for (uint32_t i = 18; i + sizeof name <= size; i += 2) {
    if (memcmp(rom + i, name, sizeof name) != 0) continue;
    uint8_t *d = rom + i - 18;
    // A DRVR header: flags (low byte 0), then five even routine offsets past the name, under 16 KB.
    // ("\p.Sony" is also a string in the ROM's code.)
    bool header = d[0] != 0 && d[1] == 0;
    for (int k = 8; k < 18 && header; k += 2) {
      const uint16_t off = (uint16_t)(d[k] << 8 | d[k + 1]);
      header = !(off & 1) && off >= 24 && off < 0x4000;
    }
    if (!header) continue;
    if (d - rom + sizeof kDriver > size) return false;
    for (size_t w = 0; w < sizeof kDriver / 2; w++) { d[2 * w] = (uint8_t)(kDriver[w] >> 8); d[2 * w + 1] = (uint8_t)kDriver[w]; }
    uint32_t sum = 0;
    for (uint32_t a = 4; a + 1 < size; a += 2) sum += (uint32_t)(rom[a] << 8 | rom[a + 1]);
    rom[0] = (uint8_t)(sum >> 24); rom[1] = (uint8_t)(sum >> 16); rom[2] = (uint8_t)(sum >> 8); rom[3] = (uint8_t)sum;
    return true;
  }
  return false;
}

// Guest memory, as the CPU sees it (24- or 32-bit), given by the core.
struct Bus {
  uint32_t (*rd8)(uint32_t), (*rd16)(uint32_t), (*rd32)(uint32_t);
  void (*wr8)(uint32_t, uint32_t), (*wr16)(uint32_t, uint32_t), (*wr32)(uint32_t, uint32_t);
  uint8_t *(*host)(uint32_t addr, uint32_t len);    // a RAM pointer for [addr, addr+len), or null
};

// A disk image: read and written in place.
struct Image {
  FILE *f = nullptr;
  uint32_t offset = 0;                              // 84 for a DiskCopy 4.2 image
  uint32_t size = 0;
  bool readOnly = false, dirty = false;
  bool written = false;                             // written since opened (or the flag was cleared)

  // Opens it (read-write if it can); false with `err` set if it is not a disk image.
  bool open(const char *path, char *err, size_t errLen) {
    close();
    f = fopen(path, "r+b");
    readOnly = false;
    if (!f) { f = fopen(path, "rb"); readOnly = true; }
    if (!f) { snprintf(err, errLen, "Cannot open the disk"); return false; }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    uint8_t h[84];
    uint32_t data = (uint32_t)n;
    offset = 0;
    fseek(f, 0, SEEK_SET);
    auto be32 = [](const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; };
    if (n > 84 && fread(h, 1, 84, f) == 84 && h[0] < 64 && h[0x52] == 1 && h[0x53] == 0 &&
        84 + (long)be32(h + 0x40) + (long)be32(h + 0x44) == n) {
      offset = 84;
      data = be32(h + 0x40);
    }
    if (data < 400 * 1024 || (data & 511)) {
      snprintf(err, errLen, "Not a Mac disk image (%ld bytes)", n);
      close();
      return false;
    }
    size = data;
    written = false;
    return true;
  }
  void close() { if (f) fclose(f); f = nullptr; size = 0; dirty = false; }
  void flush() { if (f && dirty) { fflush(f); dirty = false; } }
  bool read(uint8_t *p, uint32_t pos, uint32_t len) {
    return f && !fseek(f, (long)(offset + pos), SEEK_SET) && fread(p, 1, len, f) == len;
  }
  bool write(const uint8_t *p, uint32_t pos, uint32_t len) {
    dirty = written = true;
    return f && !fseek(f, (long)(offset + pos), SEEK_SET) && fwrite(p, 1, len, f) == len;
  }
};

class Drives {
 public:
  Image image[kDrives];

  void setBus(const Bus &b) { bus_ = b; }
  // At power-on: no drive is in the Mac's queue until the driver's Open.
  void reset() { for (Drive &d : drive_) d = Drive(); }
  // Snapshots: the drives' state as bytes (the images stay as they are).
  static constexpr size_t kStateSize = 9 * kDrives;    // per drive: num, status, toMount
  void saveState(uint8_t *out) const {
    for (const Drive &d : drive_) {
      memcpy(out, &d.num, sizeof d.num); memcpy(out + 4, &d.status, 4); out[8] = d.toMount; out += 9;
    }
  }
  void loadState(const uint8_t *in) {
    for (Drive &d : drive_) {
      memcpy(&d.num, in, sizeof d.num); memcpy(&d.status, in + 4, 4); d.toMount = in[8] != 0; in += 9;
    }
  }

  // An opcode from the driver (A0 = the pb, A1 = the DCE, A2 = Open's status record, D1 = Open's
  // index). Returns the value for D0.
  // Drive i is in the Mac's queue (its driver has opened), and it holds a disk the Mac knows of.
  bool installed(int i) const { return drive_[i].status != 0; }
  bool hasDisk(int i) { return installed(i) && bus_.rd8(drive_[i].status + dsDiskInPlace) != 0; }
  // A disk just put into the empty drive i (image[i] open): the Mac mounts it at the next accRun.
  void insert(int i) {
    Drive &d = drive_[i];
    bus_.wr8(d.status + dsDiskInPlace, 1);
    bus_.wr8(d.status + dsWriteProt, image[i].readOnly ? 0xFF : 0);
    d.toMount = true;
  }

  int32_t op(uint16_t opcode, uint32_t pb, uint32_t dce, uint32_t a2, uint32_t d1) {
    switch (opcode) {
      case kOpOpen: return open(dce, a2, d1);
      case kOpPrime: return prime(pb, dce);
      case kOpControl: return control(pb);
      case kOpStatus: return status(pb);
    }
    return -1;
  }

 private:
  // Basilisk's constants (b2_macos_util.h)
  enum { qLink = 0, qHead = 2, ioTrap = 6, ioVRefNum = 22, ioBuffer = 32, ioReqCount = 36, ioActCount = 40,
         csCode = 26, csParam = 28, dsWriteProt = 2, dsDiskInPlace = 3, dsInstalled = 4, dsSides = 5,
         dsQLink = 6, dsQType = 10, dsQDrive = 12, dsTwoSideFmt = 18, dsMFMDrive = 22, dsMFMDisk = 23,
         dCtlQHdr = 6, dCtlPosition = 16, aRdCmd = 2 };
  enum : int16_t { noErr = 0, controlErr = -17, statusErr = -18, wPrErr = -44, paramErr = -50,
                   nsDrvErr = -56, offLinErr = -65, ioErr = -36 };

  struct Drive {
    int num = 0;                                    // the Mac's drive number
    uint32_t status = 0;                            // the drive status record (0: not installed)
    bool toMount = false;
  };
  Drive drive_[kDrives];
  Bus bus_ {};

  int16_t err(int16_t e) { bus_.wr16(0x142, (uint32_t)(uint16_t)e); return e; }   // DskErr

  int find(int num) {
    for (int i = 0; i < kDrives; i++) if (drive_[i].status && drive_[i].num == num) return i;
    return -1;
  }

  bool numberFree(int num) {
    for (uint32_t e = bus_.rd32(0x308 + qHead); e; e = bus_.rd32(e + qLink))
      if ((int)(int16_t)bus_.rd16(e - dsQLink + dsQDrive) == num) return false;
    return true;
  }

  int32_t open(uint32_t dce, uint32_t st, uint32_t index) {
    if (index >= (uint32_t)kDrives) return 0;
    if (index == 0) {
      bus_.wr32(dce + dCtlPosition, 0);
      bus_.wr16(dce + dCtlQHdr, (bus_.rd16(dce + dCtlQHdr) & 0xFF00) | 3);   // version >= 3
      bus_.wr32(0x134, 0xDEADBEEF);               // SonyVars: something, not 0
      err(noErr);
    }
    Drive &d = drive_[index];
    int num = 1;
    while (!numberFree(num)) num++;
    d.num = num;
    d.status = st;
    const Image &im = image[index];
    bus_.wr16(st + dsQType, 0);                     // sony
    bus_.wr8(st + dsInstalled, 1);
    bus_.wr8(st + dsSides, 0xFF);
    bus_.wr8(st + dsTwoSideFmt, 0xFF);
    bus_.wr8(st + dsMFMDrive, 0);
    bus_.wr8(st + dsMFMDisk, 0);
    bus_.wr8(st + dsDiskInPlace, im.f ? 1 : 0);
    bus_.wr8(st + dsWriteProt, im.readOnly ? 0xFF : 0);
    d.toMount = index > 0 && im.f;                  // the boot drive is mounted by the boot
    return num;
  }

  int32_t prime(uint32_t pb, uint32_t dce) {
    bus_.wr32(pb + ioActCount, 0);
    const int i = find((int16_t)bus_.rd16(pb + ioVRefNum));
    if (i < 0) return err(nsDrvErr);
    Drive &d = drive_[i];
    Image &im = image[i];
    if (!bus_.rd8(d.status + dsDiskInPlace) || !im.f) return err(offLinErr);
    bus_.wr8(d.status + dsDiskInPlace, 2);          // accessed
    const uint32_t buf = bus_.rd32(pb + ioBuffer), len = bus_.rd32(pb + ioReqCount);
    const uint32_t pos = bus_.rd32(dce + dCtlPosition);
    if ((len & 511) || (pos & 511) || pos + len > im.size || pos + len < pos) return err(paramErr);
    const bool reading = (bus_.rd16(pb + ioTrap) & 0xFF) == aRdCmd;
    if (!reading && im.readOnly) return err(wPrErr);
    uint8_t blk[512];
    for (uint32_t done = 0; done < len;) {
      uint8_t *host = bus_.host(buf + done, len - done);   // the whole rest in RAM: one go
      const uint32_t n = host ? len - done : 512;
      uint8_t *p = host ? host : blk;
      if (reading) {
        if (!im.read(p, pos + done, n)) return err(ioErr);
        if (!host) for (uint32_t k = 0; k < n; k++) bus_.wr8(buf + done + k, p[k]);
      } else {
        if (!host) for (uint32_t k = 0; k < n; k++) p[k] = (uint8_t)bus_.rd8(buf + done + k);
        if (!im.write(p, pos + done, n)) return err(ioErr);
      }
      done += n;
    }
    if (reading) { bus_.wr32(0x2FC, 0); bus_.wr32(0x300, 0); bus_.wr32(0x304, 0); }   // TagBuf
    bus_.wr32(pb + ioActCount, len);
    bus_.wr32(dce + dCtlPosition, pos + len);
    return err(noErr);
  }

  int32_t control(uint32_t pb) {
    const int code = (int16_t)bus_.rd16(pb + csCode);
    if (code == 1) return err(-1);                  // KillIO
    if (code == 9) return err(noErr);               // track cache
    if (code == 65) {                               // accRun: a drive to mount
      for (Drive &d : drive_)
        if (d.status && d.toMount) { d.toMount = false; return d.num; }
      return err(noErr);
    }
    const int i = find((int16_t)bus_.rd16(pb + ioVRefNum));
    if (i < 0) return err(nsDrvErr);
    Drive &d = drive_[i];
    switch (code) {
      case 5: return err(bus_.rd8(d.status + dsDiskInPlace) ? noErr : offLinErr);   // verify
      case 6: return err(image[i].readOnly ? wPrErr : offLinErr);                     // format
      case 7: bus_.wr8(d.status + dsDiskInPlace, 0); return err(noErr);              // eject
      case 8: return err(noErr);                                                      // tag buffer
      case 23: bus_.wr32(pb + csParam, d.num == 1 ? 0x0004 : 0x0104); return err(noErr);   // drive info
    }
    return err(controlErr);
  }

  int32_t status(uint32_t pb) {
    const int code = (int16_t)bus_.rd16(pb + csCode);
    const int i = find((int16_t)bus_.rd16(pb + ioVRefNum));
    if (i < 0) return err(nsDrvErr);
    Drive &d = drive_[i];
    switch (code) {
      case 6:                                       // the formats: one
        if ((int16_t)bus_.rd16(pb + csParam) <= 0) return err(paramErr);
        bus_.wr16(pb + csParam, 1);
        bus_.wr32(bus_.rd32(pb + csParam + 2), image[i].size / 512);
        bus_.wr32(bus_.rd32(pb + csParam + 2) + 4, 0xD2120050);
        return err(noErr);
      case 8:                                       // the drive status
        for (int k = 0; k < 22; k++) bus_.wr8(pb + csParam + k, bus_.rd8(d.status + k));
        return err(noErr);
      case 10: bus_.wr32(pb + csParam, 0xFE); return err(noErr);   // disk type
      case 0x5343: bus_.wr8(pb + csParam, 0x02); return err(noErr);
    }
    return err(statusErr);
  }
};

}  // namespace sony
