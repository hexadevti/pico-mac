// rtc.h -- the Mac II's clock chip (343-0042-B): the Plus's serial RTC with 256 bytes of extended
// PRAM ("XPRAM"). Written for pico-mac.
//
// VIA1 port B: PB0 data, PB1 clock, PB2 /enable. Bits go in (MSB first) on the clock's rising
// edges; a read command's byte comes back the same way, one bit per rising edge after the command.
// Command byte (z = 1 read, 0 write), address in bits 6..2:
//   z00aa01  seconds, byte aa (also z01aa01 for writes)   z011001  test     z011101  write-protect
//   z10aa01  PRAM 0x10-0x13 (XPRAM 0x08-0x0B)             z1aaaa01 PRAM 0x00-0x0F (XPRAM 0x10-0x1F)
//   z0111abc + 0defgh00: XPRAM address abcdefgh (two command bytes)
// The old 20 bytes are views of XPRAM, as on the real chip.
//
// SPConfig (PRAM 0x13, XPRAM 0x13) always reads with both serial ports "async" (2) where it says
// free (0) or AppleTalk (1): there is no SCC, so no LocalTalk, and the AppleTalk driver would wait
// forever for its first frame to go out (System 7.5.5 hangs at "Starting up..."). Zapped PRAM
// says 0, and the Chooser can turn AppleTalk on.
#pragma once
#include <stdint.h>
#include <string.h>

class MacRtc {
 public:
  uint8_t xpram[256];
  uint32_t seconds = 0;
  bool dirty = false;                // XPRAM changed since the flag was cleared

  void reset() {                     // the chip's transfer state (the PRAM and clock stay)
    state_ = 0; bits_ = 0; last_ = 0x07; out_ = 1;
  }

  uint8_t dataOut() const { return out_; }    // PB0 when the CPU reads it

  // A write to VIA1 port B (the pins as driven).
  void portB(uint8_t pb) {
    const uint8_t old = last_;
    last_ = pb;
    if (pb & 4) { state_ = 0; bits_ = 0; return; }       // disabled: the transfer ends
    if ((old & 2) || !(pb & 2)) return;                  // only rising clock edges
    if (state_ == kRead) {
      out_ = (shift_ >> 7) & 1;
      shift_ <<= 1;
      if (++bits_ == 8) { state_ = 0; bits_ = 0; }
      return;
    }
    shift_ = (uint8_t)(shift_ << 1 | (pb & 1));
    if (++bits_ < 8) return;
    bits_ = 0;
    switch (state_) {
      case kCommand:
        cmd_ = shift_;
        if ((cmd_ & 0x78) == 0x38) { state_ = kExtAddr; break; }
        if (cmd_ & 0x80) startRead(readReg(cmd_));
        else state_ = kWrite;
        break;
      case kExtAddr:
        addr_ = (uint8_t)((cmd_ & 7) << 5 | ((shift_ >> 2) & 0x1F));
        if (cmd_ & 0x80) startRead(peek(addr_));
        else state_ = kExtWrite;
        break;
      case kWrite: writeReg(cmd_, shift_); state_ = 0; break;
      case kExtWrite:
        if (!(wp_ & 0x80) && xpram[addr_] != shift_) { xpram[addr_] = shift_; dirty = true; }
        state_ = 0;
        break;
    }
  }

 private:
  enum { kCommand, kWrite, kRead, kExtAddr, kExtWrite };
  int state_ = 0, bits_ = 0;
  uint8_t last_ = 0x07, shift_ = 0, cmd_ = 0, addr_ = 0, out_ = 1, wp_ = 0;

  void startRead(uint8_t v) { shift_ = v; state_ = kRead; }

  uint8_t peek(int x) const {
    uint8_t v = xpram[x];
    if (x == 0x13) {
      if ((v & 0x0F) < 2) v = (uint8_t)((v & 0xF0) | 2);
      if ((v >> 4) < 2) v = (uint8_t)((v & 0x0F) | 0x20);
    }
    return v;
  }

  // The XPRAM byte a classic command reaches, -1 for none (seconds etc.).
  static int classic(uint8_t cmd) {
    const int a = (cmd >> 2) & 0x1F;
    if (a >= 16) return 0x10 + (a & 15);
    if (a >= 8 && a < 12) return 0x08 + (a & 3);
    return -1;
  }

  uint8_t readReg(uint8_t cmd) const {
    const int a = (cmd >> 2) & 0x1F;
    if (a < 8) return (uint8_t)(seconds >> (8 * (a & 3)));
    const int x = classic(cmd);
    return x >= 0 ? peek(x) : 0;
  }

  void writeReg(uint8_t cmd, uint8_t v) {
    const int a = (cmd >> 2) & 0x1F;
    if (a == 13) { wp_ = v; return; }
    if (wp_ & 0x80) return;
    if (a < 8) {
      const int b = 8 * (a & 3);
      seconds = (seconds & ~(0xFFu << b)) | (uint32_t)v << b;
      return;
    }
    const int x = classic(cmd);
    if (x >= 0 && xpram[x] != v) { xpram[x] = v; dirty = true; }
  }
};
