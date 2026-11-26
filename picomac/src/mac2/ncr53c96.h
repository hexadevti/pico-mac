// ncr53c96.h -- the Quadra 700's SCSI controller, an NCR 53C96, with nothing on the bus yet: every
// selection times out (a disconnect interrupt), as with an empty bus. The registers are at
// 0x5000F000 + reg * 0x10. Its interrupt goes to VIA2 CB2 (as the Mac II's 5380).
//   read:  0 TC low, 1 TC mid, 2 FIFO, 3 command, 4 status, 5 interrupt, 6 sequence step,
//          7 FIFO flags, 8 config 1, 0xB config 2, 0xC config 3, 0xE TC high (the chip ID)
//   write: 0/1 TC, 2 FIFO, 3 command, 4 bus ID, 5 select timeout, 6 sync period, 7 sync offset,
//          8 config 1, 9 clock factor, 0xA test, 0xB/0xC config 2/3
// Status: 0x80 INT, 0x10 TC zero. Interrupt: 0x80 bus reset, 0x40 illegal command, 0x20 disconnect,
// 0x08 function complete. Reading the interrupt register clears it, the status and the step.
#pragma once

#include <stdint.h>
#include <string.h>

class Ncr53c96 {
 public:
  void reset() { memset(this, 0, sizeof *this); }

  // A selection's timeout ends here (the core calls it once a frame). true: raise the interrupt.
  bool tick() {
    if (!pending_) return false;
    if (--delay_ > 0) return false;
    pending_ = 0;
    return raise(0x20);                             // disconnect: nobody answered
  }

  // true: the interrupt line rose (pulse VIA2 CB2).
  uint8_t read(int reg, bool *irq) {
    *irq = false;
    switch (reg & 15) {
      case 0: return (uint8_t)tc_;
      case 1: return (uint8_t)(tc_ >> 8);
      case 2: {
        if (!fifoN_) return 0;
        const uint8_t v = fifo_[0];
        memmove(fifo_, fifo_ + 1, --fifoN_);
        return v;
      }
      case 3: return cmd_;
      case 4: return status_ | (tc_ == 0 ? 0x10 : 0);
      case 5: {
        const uint8_t v = intr_;
        intr_ = 0;
        status_ &= ~0x80;
        step_ = 0;
        return v;
      }
      case 6: return step_;
      case 7: return fifoN_ & 0x1F;
      case 8: return cfg1_;
      case 0xB: return cfg2_;
      case 0xC: return cfg3_;
      case 0xE: return 0x12;                        // TC high: the 53CF96's ID when the feature is on
    }
    return 0;
  }

  bool write(int reg, uint8_t v) {
    switch (reg & 15) {
      case 0: tcLoad_ = (tcLoad_ & 0xFF00) | v; break;
      case 1: tcLoad_ = (tcLoad_ & 0x00FF) | (uint16_t)(v << 8); break;
      case 2: if (fifoN_ < sizeof fifo_) fifo_[fifoN_++] = v; break;
      case 3: return command(v);
      case 8: cfg1_ = v; break;
      case 0xB: cfg2_ = v; break;
      case 0xC: cfg3_ = v; break;
      default: break;                               // bus ID, timeouts, sync, clock, test
    }
    return false;
  }

 private:
  bool raise(uint8_t why) {
    const bool rose = !(status_ & 0x80);
    intr_ |= why;
    status_ |= 0x80;
    return rose;
  }

  bool command(uint8_t v) {
    cmd_ = v;
    if (v & 0x80) tc_ = tcLoad_;                    // DMA: the counter loads
    switch (v & 0x7F) {
      case 0x00: return false;                      // NOP
      case 0x01: fifoN_ = 0; return false;          // flush the FIFO
      case 0x02: {                                  // reset the chip
        const uint8_t keep = cfg1_ & 7;
        reset();
        cfg1_ = keep;
        return false;
      }
      case 0x03:                                    // reset the bus (reported unless config 1 bit 6)
        return (cfg1_ & 0x40) ? false : raise(0x80);
      case 0x41: case 0x42: case 0x43: case 0x46:   // select: nobody answers, after a while
        fifoN_ = 0;
        step_ = 0;
        pending_ = 1;
        delay_ = 2;
        return false;
      case 0x44: case 0x45: return false;           // enable / disable reselection
      case 0x10: case 0x11: case 0x12: case 0x18: case 0x1A: case 0x1B:   // transfers: no target
        return raise(0x20);
    }
    return raise(0x40);                             // anything else: illegal here
  }

  uint8_t fifo_[16];
  uint8_t fifoN_, cmd_, status_, intr_, step_, cfg1_, cfg2_, cfg3_, pending_;
  int8_t delay_;
  uint16_t tc_, tcLoad_;
};
