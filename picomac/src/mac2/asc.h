// asc.h -- the Apple Sound Chip (344S0053) as the Mac II's Sound Manager uses it. Written for
// pico-mac, after the register descriptions in MAME's asc.cpp (no code taken).
//
// 0x000-0x3FF FIFO A, 0x400-0x7FF FIFO B (FIFO mode: any write there queues a byte; wavetable
// mode: the four 512-byte wave tables, voices 0-1 in A's half and 2-3 in B's). Registers:
//   0x800 version (0x00: the Mac II's)  0x801 mode (0 off, 1 FIFO, 2 wavetable)  0x802 control
//   0x803 FIFO control (bit 7: empty both FIFOs)  0x804 FIFO status, cleared by a read:
//         bit 0 A half empty, bit 1 A full or empty, bit 2 B half empty, bit 3 B full or empty.
//         Both are levels, set again at every sample: below half sets the half bit (an empty FIFO
//         too), so the IRQ comes back after each read until the driver refills it. The Sound
//         Manager refills only on the half bit: flagging just the 512 -> 511 step, or an empty
//         FIFO as 0x0A, left it waiting with an empty FIFO (Prince of Persia's music cut out).
//   0x806 volume (bits 7-5)  0x807 clock rate  0x810 + 8 * voice: phase, +4: increment (32-bit)
// Samples are unsigned 8-bit at 22257 Hz (we play them at 22255: one frame = 370). A nonzero FIFO
// status pulls the IRQ line, which is VIA2 CB1 on the Mac II.
#pragma once
#include <stdint.h>
#include <string.h>

class Asc {
 public:
  void reset() {
    memset(ram_, 0x80, sizeof ram_);
    memset(regs_, 0, sizeof regs_);
    for (Fifo &f : fifo_) f = Fifo();
    last_ = 0;
  }

  // Returns true when the IRQ line has just gone active (the caller pulses CB1).
  bool write(uint32_t off, uint8_t v) {
    off &= 0xFFF;
    if (off < 0x800) {
      if (regs_[1] != 1) { ram_[off] = v; return false; }   // off or wavetable: the RAM, not the FIFO
      Fifo &f = fifo_[off >> 10];
      if (f.count < 1024) { f.data[(f.rd + f.count) & 1023] = v; f.count++; }
      if (f.count == 1024) return flag(off < 0x400 ? 0x02 : 0x08);
      return false;
    }
    // 0x800 (version) and 0x804 (FIFO status) are read-only. A read-modify-write of the status (the
    // Sound Manager's, at 0x3DDFC) wrote its bits back: the status never cleared, no IRQ came again,
    // and the sound stopped.
    if (off >= 0x840 || off == 0x800 || off == 0x804) return false;
    regs_[off - 0x800] = v;
    if (off == 0x803 && (v & 0x80)) for (Fifo &f : fifo_) f.count = 0;
    return false;
  }

  uint8_t read(uint32_t off) {
    off &= 0xFFF;
    if (off < 0x800) return ram_[off];
    if (off == 0x800) return 0x00;
    if (off == 0x804) { const uint8_t s = regs_[4]; regs_[4] = 0; return s; }
    return off < 0x840 ? regs_[off - 0x800] : 0;
  }

  // Plays n samples into out (signed 16-bit, mono). Returns true when the IRQ line went active.
  bool render(int16_t *out, int n) {
    bool irq = false;
    const int vol = regs_[6] >> 5;                   // 0..7
    for (int i = 0; i < n; i++) {
      int s = 0;
      if (regs_[1] == 1) {
        for (int c = 0; c < 2; c++) {
          Fifo &f = fifo_[c];
          if (f.count) {
            f.held = (int)f.data[f.rd] - 128;
            f.rd = (f.rd + 1) & 1023;
            f.count--;
          }
          s += f.held;                                 // empty: the last sample, not a step to 0
          if (f.count < 512) irq |= flag(c ? 0x04 : 0x01);   // a level: below half, empty too
          if (!f.count) irq |= flag(c ? 0x08 : 0x02);
        }
        s *= 128;                                      // two channels of +-128 -> +-32768
      } else if (regs_[1] == 2) {
        // The four voices add up to one 8-bit sample, 0x80 the centre (the ROM's chime tables are
        // 6-bit, 0x01-0x3F). Centring each voice on 0x80 gave a big DC step at every start and stop.
        for (int v = 0; v < 4; v++) {
          uint32_t ph = be32(0x10 + 8 * v);
          ph += be32(0x14 + 8 * v);
          setBe32(0x10 + 8 * v, ph);
          s += ram_[v << 9 | ((ph >> 15) & 0x1FF)];
        }
        s = (s - 128) * 256;
      } else {
        s = last_ - last_ / 64;                        // off: ease to the centre, not a click
      }
      last_ = s;
      s = s * vol / 7;
      out[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
    return irq;
  }

 private:
  struct Fifo { uint8_t data[1024]; int rd = 0, count = 0, held = 0; };
  Fifo fifo_[2];
  uint8_t ram_[0x800];                                 // what a read of 0x000-0x7FF sees
  uint8_t regs_[0x40];
  int last_ = 0;                                       // the last sample, before the volume


  bool flag(uint8_t bit) {
    const bool was = regs_[4] != 0;
    regs_[4] |= bit;
    return !was;
  }
  uint32_t be32(int r) const {
    return (uint32_t)regs_[r] << 24 | (uint32_t)regs_[r + 1] << 16 | (uint32_t)regs_[r + 2] << 8 | regs_[r + 3];
  }
  void setBe32(int r, uint32_t v) {
    regs_[r] = v >> 24; regs_[r + 1] = v >> 16; regs_[r + 2] = v >> 8; regs_[r + 3] = (uint8_t)v;
  }
};
