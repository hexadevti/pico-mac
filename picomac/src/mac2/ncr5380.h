// ncr5380.h -- the Mac II's SCSI controller, an NCR 5380, with nothing on the bus yet: arbitration
// is won at once, and a selection is never answered (no target raises BSY), so the ROM's SCSI
// Manager times out and moves on. Registers are at base + reg * 0x10 (0x50F10000).
#pragma once

#include <stdint.h>

class Ncr5380 {
 public:
  void reset() { odr_ = icr_ = mr_ = tcr_ = ser_ = 0; }

  uint8_t read(int reg) {
    switch (reg & 7) {
      case 0: return (icr_ & 0x01) || (mr_ & 0x01) ? odr_ : 0;   // the bus: only what we drive
      case 1: return (uint8_t)((icr_ & 0x9F) | ((mr_ & 0x01) ? 0x40 : 0));   // AIP while arbitrating
      case 2: return mr_;
      case 3: return tcr_;
      case 4: {                                     // bus status: SEL and RST as driven, no BSY
        uint8_t v = 0;
        if (icr_ & 0x80) v |= 0x80;
        if (icr_ & 0x04) v |= 0x02;
        return v;
      }
      case 5: {                                     // bus and status: phase match when TCR = bus (0)
        uint8_t v = 0;
        if ((tcr_ & 7) == 0) v |= 0x08;
        if (icr_ & 0x02) v |= 0x02;
        if (icr_ & 0x10) v |= 0x01;
        return v;
      }
      case 6: return 0;                             // input data
      default: return 0;                            // reset parity/interrupt
    }
  }

  void write(int reg, uint8_t v) {
    switch (reg & 7) {
      case 0: odr_ = v; break;
      case 1: icr_ = v; break;
      case 2: mr_ = v; break;
      case 3: tcr_ = v; break;
      case 4: ser_ = v; break;
      default: break;                               // the DMA starts: no target, nothing moves
    }
  }

 private:
  uint8_t odr_ = 0, icr_ = 0, mr_ = 0, tcr_ = 0, ser_ = 0;
};
