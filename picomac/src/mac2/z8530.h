// z8530.h -- the serial chip (Zilog 8530 SCC) of the Mac II family, with nothing on either port.
// Written for pico-mac. What goes out is lost and nothing comes in, as with an empty LocalTalk
// bus: a byte leaves the transmitter after kByteTicks, the buffer empties (Tx interrupt), and when
// the last one is gone the transmitter underruns (Tx underrun/EOM, an external/status interrupt).
// That is what AppleTalk waits for when it sends its first frames (Mac OS 8's Open Transport
// starts LocalTalk whatever SPConfig says); without it the boot stops at "Starting up".
//
// Registers: `ch` 0 = B, 1 = A. Control writes set the pointer (WR0) then write a register; data
// is WR8 / RR8. Time is in VIA ticks (783.36 kHz), as the core's other devices.
//   RR0: 0x04 Tx empty, 0x10 sync/hunt (nothing received), 0x40 Tx underrun/EOM
//   RR1: 0x01 all sent. RR2 (B): the vector with the status (WR9 bit 4: high bits). RR3 (A): the IPs.
#pragma once
#include <stdint.h>
#include <string.h>

class Z8530 {
 public:
  static constexpr uint64_t kByteTicks = 27;       // a byte at 230.4 kbit/s (LocalTalk)

  void reset() {
    memset(this, 0, sizeof *this);
    for (int c = 0; c < 2; c++) resetChannel(c);
  }

  bool irq() const { return (wr9_ & 0x08) && (pending() != 0); }

  uint8_t read(int ch, bool data, uint64_t now) {
    tick(now);
    Channel &c = ch_[ch & 1];
    if (data) return 0;                            // RR8: nothing ever arrives
    const int r = ptr_;
    ptr_ = 0;
    switch (r & 7) {                               // RR4-7 repeat RR0-3 on the 8530
      case 0: return rr0(c);
      case 1: return (!c.full && !c.busy) ? 0x07 : 0x06;
      case 2: return (ch & 1) ? wr2_ : vector();
      case 3: return (ch & 1) ? pending() : 0;
    }
    switch (r) {
      case 12: return c.wr[12];
      case 13: case 9: return c.wr[13];
      case 15: case 11: return c.wr[15] & 0xFA;
    }
    return 0;                                      // RR8 (as data), RR10, RR14
  }

  void write(int ch, bool data, uint8_t v, uint64_t now) {
    tick(now);
    const int n = ch & 1;
    Channel &c = ch_[n];
    if (data) { send(n, now); return; }
    const int r = ptr_;
    ptr_ = 0;
    if (r == 0) { wr0(n, v); return; }
    switch (r) {
      case 2: wr2_ = v; return;
      case 9: wr9(v); return;
      case 8: send(n, now); return;
      case 1:
        c.wr[1] = v;
        if (!(v & 0x02)) c.ip &= ~kIpTx;
        if (!(v & 0x01)) c.ip &= ~kIpExt;
        return;
      case 15:
        c.wr[15] = v;
        return;
    }
    c.wr[r & 15] = v;
  }

  // Ticks until the next transmitter event, UINT64_MAX for none.
  uint64_t nextEvent(uint64_t now) const {
    uint64_t best = UINT64_MAX;
    for (int i = 0; i < 2; i++)
      if (ch_[i].busy) best = ch_[i].doneAt > now ? (ch_[i].doneAt - now < best ? ch_[i].doneAt - now : best) : 0;
    return best;
  }

  // A byte left the shifter: the next one goes in, or the transmitter underruns.
  void tick(uint64_t now) {
    for (int i = 0; i < 2; i++) {
      Channel &c = ch_[i];
      while (c.busy && now >= c.doneAt) {
        if (c.full) {
          c.full = false;
          c.doneAt += kByteTicks;
          if (c.wr[1] & 0x02) c.ip |= kIpTx;
        } else {
          c.busy = false;
          if (!c.eom) {                            // the frame ends (CRC, flag) and EOM latches
            c.eom = true;
            if ((c.wr[1] & 0x01) && (c.wr[15] & 0x40)) c.ip |= kIpExt;
          }
        }
      }
    }
  }

 private:
  enum : uint8_t { kIpExt = 1, kIpTx = 2 };        // per channel; RR3 has B in bits 0-2, A in 3-5
  struct Channel {
    uint8_t wr[16];
    uint8_t ip;
    bool full, busy, eom;
    uint64_t doneAt;
  };
  Channel ch_[2];
  uint8_t ptr_, wr2_, wr9_;

  void resetChannel(int n) {
    Channel &c = ch_[n];
    memset(&c, 0, sizeof c);
    c.eom = true;                                  // RR0 bit 6 reads 1 after a reset
    c.wr[15] = 0xF8;
  }

  uint8_t rr0(const Channel &c) const {
    return (uint8_t)((c.full ? 0 : 0x04) | 0x10 | (c.eom ? 0x40 : 0));
  }

  uint8_t pending() const {
    return (uint8_t)(((ch_[0].ip & kIpExt) ? 0x01 : 0) | ((ch_[0].ip & kIpTx) ? 0x02 : 0) |
                     ((ch_[1].ip & kIpExt) ? 0x08 : 0) | ((ch_[1].ip & kIpTx) ? 0x10 : 0));
  }

  // RR2 of channel B: the vector with the highest interrupt's status (V3-V1, or V4-V6 reversed).
  uint8_t vector() const {
    int s = 3;                                     // none
    if (ch_[1].ip & kIpTx) s = 4;
    else if (ch_[1].ip & kIpExt) s = 5;
    else if (ch_[0].ip & kIpTx) s = 0;
    else if (ch_[0].ip & kIpExt) s = 1;
    if (wr9_ & 0x10)
      return (uint8_t)((wr2_ & 0x8F) | ((s & 1) << 6) | ((s & 2) << 4) | ((s & 4) << 2));
    return (uint8_t)((wr2_ & 0xF1) | (s << 1));
  }

  void send(int n, uint64_t now) {
    Channel &c = ch_[n];
    c.ip &= ~kIpTx;
    if (!(c.wr[5] & 0x08)) return;                 // the transmitter is off: the byte is lost
    if (!c.busy) {                                 // straight into the shifter: the buffer is free
      c.busy = true;
      c.doneAt = now + kByteTicks;
      if (c.wr[1] & 0x02) c.ip |= kIpTx;
    } else {
      c.full = true;
    }
  }

  void wr0(int n, uint8_t v) {
    Channel &c = ch_[n];
    ptr_ = v & 7;
    switch ((v >> 3) & 7) {
      case 1: ptr_ |= 8; break;                    // point high
      case 2: c.ip &= ~kIpExt; break;              // reset external/status interrupts
      case 5: c.ip &= ~kIpTx; break;               // reset the Tx interrupt pending
      default: break;                              // abort, Rx int, error reset, reset IUS
    }
    if ((v >> 6) == 3) c.eom = false;              // reset Tx underrun/EOM: the next underrun ends a frame
  }

  void wr9(uint8_t v) {
    switch (v >> 6) {
      case 1: resetChannel(0); break;
      case 2: resetChannel(1); break;
      case 3: resetChannel(0); resetChannel(1); wr9_ = 0; return;   // hardware reset
    }
    wr9_ = (uint8_t)(v & 0x3F);
  }
};
