// via6522.h -- a 6522 VIA for the Mac II core (two of them: VIA1 and VIA2). umac's via.c is the Plus's
// one VIA, wired to its globals; this one is an object, with the ports and the IRQ line as callbacks.
//
// Time is in VIA ticks (783.36 kHz on the Mac II, CPU clock / 20), passed in by the caller: the
// timers are kept as the tick at which they reach zero, so nothing needs to run per tick.
//
// What is modelled: ports A/B with DDRs, timer 1 (one-shot and free-run, latches), timer 2
// (one-shot), the shift register as a plain register (SR flag on access), CA1/CA2/CB1/CB2 as edge
// inputs, IFR/IER and the IRQ output. Not modelled: PB7 output from timer 1, timer 2 pulse counting,
// the handshake outputs (CA2/CB2 as outputs) and the shift register's clocking.
#pragma once

#include <stdint.h>

class Via6522 {
 public:
  enum Reg { ORB = 0, ORA = 1, DDRB = 2, DDRA = 3, T1CL = 4, T1CH = 5, T1LL = 6, T1LH = 7,
             T2CL = 8, T2CH = 9, SR = 10, ACR = 11, PCR = 12, IFR = 13, IER = 14, ORA_NH = 15 };
  enum Irq : uint8_t { CA2 = 0x01, CA1 = 0x02, SRF = 0x04, CB2 = 0x08, CB1 = 0x10, T2 = 0x20, T1 = 0x40 };

  // Port inputs (pins not driven by the VIA), port outputs (called with the pins as driven, after
  // a write to ORx or DDRx), and the IRQ line (true = asserted). All optional.
  void *ctx = nullptr;
  uint8_t (*inA)(void *ctx) = nullptr;
  uint8_t (*inB)(void *ctx) = nullptr;
  void (*outA)(void *ctx, uint8_t v) = nullptr;
  void (*outB)(void *ctx, uint8_t v) = nullptr;
  void (*irq)(void *ctx, bool on) = nullptr;
  // The shift register was read or written (the Mac II's ADB transceiver hangs off it).
  void (*srAccess)(void *ctx, bool write, uint8_t v) = nullptr;

  void reset() {
    orb_ = ora_ = ddrb_ = ddra_ = sr_ = acr_ = pcr_ = ifr_ = ier_ = 0;
    t1l_ = t2l_ = 0xFFFF;
    t1Armed_ = t2Armed_ = false;
    t1At_ = t2At_ = 0;
    irqOut_ = false;
    if (irq) irq(ctx, false);
  }

  // Bring the timers up to `now`: sets T1/T2 in IFR when they ran out.
  void tick(uint64_t now) {
    if (t1Armed_ && now >= t1At_) {
      setFlag(T1);
      if (acr_ & 0x40) {                                    // free-run: N + 2 ticks a period
        const uint64_t period = (uint64_t)t1l_ + 2;
        t1At_ += ((now - t1At_) / period + 1) * period;
      } else {
        t1Armed_ = false;                                  // one-shot: once per load
      }
    }
    if (t2Armed_ && now >= t2At_) {
      setFlag(T2);
      t2Armed_ = false;
    }
  }

  // Ticks from `now` until a timer runs out, or UINT64_MAX.
  uint64_t nextEvent(uint64_t now) const {
    uint64_t n = UINT64_MAX;
    if (t1Armed_) n = t1At_ > now ? t1At_ - now : 0;
    if (t2Armed_) { const uint64_t m = t2At_ > now ? t2At_ - now : 0; if (m < n) n = m; }
    return n;
  }

  // An active edge on a control line (the PCR's edge selection is not checked: the Mac's lines
  // are configured for the edges their sources make).
  void pulse(Irq line) { setFlag(line); }

  uint8_t read(int reg, uint64_t now) {
    tick(now);
    switch (reg & 15) {
      case ORB:
        clearFlag(CB1 | (cb2Independent() ? 0 : CB2));
        return (orb_ & ddrb_) | (pinsB() & ~ddrb_);
      case ORA:
        clearFlag(CA1 | (ca2Independent() ? 0 : CA2));
        return (ora_ & ddra_) | (pinsA() & ~ddra_);
      case ORA_NH: return (ora_ & ddra_) | (pinsA() & ~ddra_);
      case DDRB: return ddrb_;
      case DDRA: return ddra_;
      case T1CL: clearFlag(T1); return (uint8_t)t1Count(now);
      case T1CH: return (uint8_t)(t1Count(now) >> 8);
      case T1LL: return (uint8_t)t1l_;
      case T1LH: return (uint8_t)(t1l_ >> 8);
      case T2CL: clearFlag(T2); return (uint8_t)t2Count(now);
      case T2CH: return (uint8_t)(t2Count(now) >> 8);
      case SR:
        clearFlag(SRF);
        if (srAccess) srAccess(ctx, false, sr_);
        return sr_;
      case ACR: return acr_;
      case PCR: return pcr_;
      case IFR: return ifr_ | ((ifr_ & ier_ & 0x7F) ? 0x80 : 0);
      case IER: return ier_ | 0x80;
    }
    return 0;
  }

  void write(int reg, uint8_t v, uint64_t now) {
    tick(now);
    switch (reg & 15) {
      case ORB:
        clearFlag(CB1 | (cb2Independent() ? 0 : CB2));
        orb_ = v;
        if (outB) outB(ctx, portB());
        break;
      case ORA:
        clearFlag(CA1 | (ca2Independent() ? 0 : CA2));
        ora_ = v;
        if (outA) outA(ctx, portA());
        break;
      case ORA_NH:
        ora_ = v;
        if (outA) outA(ctx, portA());
        break;
      case DDRB: ddrb_ = v; if (outB) outB(ctx, portB()); break;
      case DDRA: ddra_ = v; if (outA) outA(ctx, portA()); break;
      case T1CL: case T1LL: t1l_ = (t1l_ & 0xFF00) | v; break;
      case T1CH:
        t1l_ = (uint16_t)((t1l_ & 0x00FF) | v << 8);
        clearFlag(T1);
        t1At_ = now + t1l_ + 1;
        t1Armed_ = true;
        break;
      case T1LH:
        t1l_ = (uint16_t)((t1l_ & 0x00FF) | v << 8);
        clearFlag(T1);
        break;
      case T2CL: t2l_ = (t2l_ & 0xFF00) | v; break;
      case T2CH:
        t2l_ = (uint16_t)((t2l_ & 0x00FF) | v << 8);
        clearFlag(T2);
        t2At_ = now + t2l_ + 1;
        t2Armed_ = true;
        break;
      case SR:
        sr_ = v;
        clearFlag(SRF);
        if (srAccess) srAccess(ctx, true, v);
        break;
      case ACR: acr_ = v; break;
      case PCR: pcr_ = v; break;
      case IFR: ifr_ &= ~(v & 0x7F); update(); break;
      case IER:
        if (v & 0x80) ier_ |= v & 0x7F;
        else ier_ &= ~(v & 0x7F);
        update();
        break;
    }
  }

  // The shift register from the outside (ADB): load a byte the Mac will read, and flag it.
  void shiftIn(uint8_t v) { sr_ = v; setFlag(SRF); }
  uint8_t shiftReg() const { return sr_; }

  uint8_t portA() const { return (ora_ & ddra_) | (pinsA() & ~ddra_); }
  uint8_t portB() const { return (orb_ & ddrb_) | (pinsB() & ~ddrb_); }
  uint8_t acr() const { return acr_; }
  uint8_t ifr() const { return ifr_; }
  uint8_t ier() const { return ier_; }
  bool irqOut() const { return irqOut_; }

 private:
  uint8_t orb_ = 0, ora_ = 0, ddrb_ = 0, ddra_ = 0, sr_ = 0, acr_ = 0, pcr_ = 0, ifr_ = 0, ier_ = 0;
  uint16_t t1l_ = 0xFFFF, t2l_ = 0xFFFF;
  bool t1Armed_ = false, t2Armed_ = false;
  uint64_t t1At_ = 0, t2At_ = 0;   // the tick at which the counter passes zero
  bool irqOut_ = false;

  uint8_t pinsA() const { return inA ? inA(ctx) : 0xFF; }
  uint8_t pinsB() const { return inB ? inB(ctx) : 0xFF; }
  bool ca2Independent() const { return (pcr_ & 0x0A) == 0x02; }   // PCR 3..1 = 001 or 011
  bool cb2Independent() const { return (pcr_ & 0xA0) == 0x20; }

  // The counter value at `now`. After zero it keeps counting down (one-shot) or reloads (free-run).
  uint16_t t1Count(uint64_t now) const {
    if (now < t1At_) return (uint16_t)(t1At_ - now - 1);
    const uint64_t past = now - t1At_;
    if (acr_ & 0x40) {
      const uint64_t period = (uint64_t)t1l_ + 2;
      const uint64_t into = past % period;
      return into == 0 ? 0xFFFF : (uint16_t)(t1l_ - (into - 1));
    }
    return (uint16_t)(0xFFFF - past);
  }
  uint16_t t2Count(uint64_t now) const {
    if (now < t2At_) return (uint16_t)(t2At_ - now - 1);
    return (uint16_t)(0xFFFF - (now - t2At_));
  }

  void setFlag(uint8_t f) { ifr_ |= f; update(); }
  void clearFlag(uint8_t f) { if (ifr_ & f) { ifr_ &= ~f; update(); } }
  void update() {
    const bool on = (ifr_ & ier_ & 0x7F) != 0;
    if (on != irqOut_) {
      irqOut_ = on;
      if (irq) irq(ctx, on);
    }
  }
};
