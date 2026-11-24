// adb.h -- the Mac II's ADB: the transceiver on VIA1 (shift register, PB4/PB5 = state, PB3 = /INT)
// and two devices, a keyboard (address 2) and a mouse (address 3).
//
// The ROM drives it like this (MAME's macadb.cpp, and the ROM at 0x40806xxx-0x408074xx):
// - it writes the command byte to the shift register (shift out) and sets state 0: the
//   transceiver sends it, then the shift register's interrupt fires;
// - for a Talk it switches the shift register to input and steps states 1, 2, 1, 2...: each step
//   shifts one byte in. /INT low on the first byte = the device timed out (no device there); low on a
//   later byte = no more data;
// - for a Listen it writes each data byte and steps states 1, 2...; state 3 (idle) or the next
//   command (state 0) ends it;
// - /INT low after the command byte = a service request (another device has data);
// - in the idle state (3) the transceiver polls on its own (autoPoll, as MAME's macadb.cpp): when
//   the last device talked to (register 0) has data, a shift-register interrupt says so (the byte
//   is thrown away), and the Mac steps states 1, 2, 1... to read the data from its first byte;
//   another device with data gives an interrupt with /INT low (a service request);
// - the driver's auto-poll sends a Talk register 0 and goes idle at once: a reply waiting then
//   comes as an auto-poll's.
// A byte takes 800 us here, as eight 100 us ADB bit cells.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

class AdbBus {
 public:
  static constexpr uint32_t kByteTicks = 627;      // 800 us of VIA ticks (783.36 kHz)

  // State changes on PB4/PB5. `shiftOut` = the VIA's ACR shifts out (bit 4).
  // Returns true when a byte transfer starts: the caller calls done() kByteTicks later.
  bool setState(int state, bool shiftOut, uint8_t sr) {
    if (state == state_) return false;
    state_ = state;
    switch (state) {
      case 0:                                       // a new command, in the shift register
        if (listenDev_) applyListen();              // the ROM goes from a Listen straight to the next
        listenDev_ = nullptr;                       // command, without the idle state
        poll_ = 0;                                  // a command cancels an auto-poll in progress
        out_ = true;
        cmd_ = sr;
        return true;
      case 1: case 2:
        out_ = shiftOut;
        if (out_) outByte_ = sr;
        return true;
      default:                                      // idle: a Listen's data is complete
        if (listenDev_) applyListen();
        listenDev_ = nullptr;
        // A Talk whose data the Mac has not read yet (System 7.5's ADB driver goes idle right after
        // the command byte): the transceiver hands it over as an auto-poll would.
        if (talkDev_ && talkPos_ == 0 && talkLen_ > 0) { poll_ = 3; return true; }
        int_ = !srqPending(nullptr);
        return false;
    }
  }

  // The transfer started by setState is over. Returns the byte the Mac reads (shift in), and sets
  // the /INT line for the ROM to look at.
  uint8_t done() {
    if (poll_ == 3) {                               // a Talk's data, after going idle
      poll_ = 0;
      int_ = true;
      return talkBuf_[0];                           // only says "data": states 1, 2 read it
    }
    if (poll_) {                                    // an auto-poll (autoPoll)
      const bool data = poll_ == 1;
      poll_ = 0;
      // The device's data leaves its queue only now: a command could have cancelled the poll.
      Device *last = at(lastTalk_);
      if (data && last && hasData(last)) {
        listenDev_ = nullptr;
        talkDev_ = last;
        talkLen_ = talkPos_ = 0;
        talk(last, 0);
        if (talkLen_) { int_ = true; return talkBuf_[0]; }   // states 1, 2 read the data
      }
      int_ = !srqPending(nullptr);
      return 0;
    }
    if (state_ == 0) {
      command();
      int_ = !srqPending(talkDev_ ? talkDev_ : listenDev_);
      return 0xFF;
    }
    if (out_) {                                     // a Listen data byte
      if (listenDev_ && listenLen_ < 8) listenBuf_[listenLen_++] = outByte_;
      int_ = true;
      return 0xFF;
    }
    if (talkPos_ < talkLen_) {
      int_ = true;
      return talkBuf_[talkPos_++];
    }
    int_ = false;                                   // timeout, or the end of the data
    return 0xFF;
  }

  bool intLine() const { return int_; }             // true = high (inactive)

  // Idle (state 3, no transfer going on): an auto-poll. True when a byte transfer starts, as with
  // setState (the caller calls done() kByteTicks later).
  bool autoPoll() {
    if (state_ != 3 || poll_) return false;
    Device *last = at(lastTalk_);
    if (last && hasData(last)) { poll_ = 1; return true; }
    if (srqPending(nullptr)) { poll_ = 2; return true; }
    return false;
  }

  // Host input. Keys are ADB key codes (0x00-0x7F); the mouse deltas add up until it is polled.
  void key(uint8_t code, bool down) {
    if (keyCount_ < (int)sizeof keyQueue_) keyQueue_[keyCount_++] = (uint8_t)(code | (down ? 0 : 0x80));
  }
  void mouse(int dx, int dy, bool button) { mouseDx_ += dx; mouseDy_ += dy; mouseBtn_ = button; }

  // Snapshots: the bus as bytes, its device pointers as indices (0 none, 1 keyboard, 2 mouse).
  void saveState(uint8_t *out) const {
    AdbBus c = *this;
    c.talkDev_ = (Device *)(uintptr_t)index(talkDev_);
    c.listenDev_ = (Device *)(uintptr_t)index(listenDev_);
    memcpy(out, (const void *)&c, sizeof c);
  }
  void loadState(const uint8_t *in) {
    memcpy((void *)this, in, sizeof *this);
    talkDev_ = byIndex((uintptr_t)talkDev_);
    listenDev_ = byIndex((uintptr_t)listenDev_);
  }

  void reset() {
    state_ = 3;
    int_ = true;
    keyCount_ = 0;
    mouseDx_ = mouseDy_ = 0;
    mouseBtn_ = mouseBtnSent_ = false;
    listenDev_ = talkDev_ = nullptr;
    talkLen_ = talkPos_ = listenLen_ = 0;
    lastTalk_ = -1;
    poll_ = 0;
    resetDevices();
  }

 private:
  struct Device {
    uint8_t addr, defAddr, handler, defHandler;
    bool srqEnable;
  };
  Device kbd_ { 2, 2, 1, 1, true }, mouse_ { 3, 3, 1, 1, true };

  int state_ = 3;
  bool out_ = true, int_ = true;
  uint8_t cmd_ = 0, outByte_ = 0;
  Device *talkDev_ = nullptr, *listenDev_ = nullptr;
  uint8_t talkBuf_[8], listenBuf_[8];
  int talkLen_ = 0, talkPos_ = 0, listenLen_ = 0, listenReg_ = 0;
  int lastTalk_ = -1;                               // the address of the last Talk register 0
  int poll_ = 0;                                    // an auto-poll in progress: 1 data, 2 service request

  uint8_t keyQueue_[32];
  int keyCount_ = 0;
  int mouseDx_ = 0, mouseDy_ = 0;
  bool mouseBtn_ = false, mouseBtnSent_ = false;

  void resetDevices() {
    kbd_.addr = kbd_.defAddr; kbd_.handler = kbd_.defHandler; kbd_.srqEnable = true;
    mouse_.addr = mouse_.defAddr; mouse_.handler = mouse_.defHandler; mouse_.srqEnable = true;
  }

  uintptr_t index(const Device *d) const { return d == &kbd_ ? 1 : d == &mouse_ ? 2 : 0; }
  Device *byIndex(uintptr_t i) { return i == 1 ? &kbd_ : i == 2 ? &mouse_ : nullptr; }

  Device *at(int addr) {
    if (kbd_.addr == addr) return &kbd_;
    if (mouse_.addr == addr) return &mouse_;
    return nullptr;
  }

  bool hasData(const Device *d) const {
    if (d == &kbd_) return keyCount_ > 0;
    return mouseDx_ || mouseDy_ || mouseBtn_ != mouseBtnSent_;
  }

  // A device other than `except` wants service.
  bool srqPending(const Device *except) const {
    for (const Device *d : { &kbd_, &mouse_ })
      if (d != except && d->srqEnable && hasData(d)) return true;
    return false;
  }

  void command() {
    talkDev_ = listenDev_ = nullptr;
    talkLen_ = talkPos_ = listenLen_ = 0;
    const int addr = cmd_ >> 4, type = (cmd_ >> 2) & 3, reg = cmd_ & 3;
    if (type == 0) {                                 // SendReset (x0) or Flush (x1)
      if ((cmd_ & 0x0F) == 0) { resetDevices(); keyCount_ = 0; mouseDx_ = mouseDy_ = 0; }
      else if ((cmd_ & 0x0F) == 1 && at(addr) == &kbd_) keyCount_ = 0;
      return;
    }
    Device *d = at(addr);
    if (!d) return;                                 // nobody there: Talk times out
    if (type == 2) { listenDev_ = d; listenReg_ = reg; return; }
    if (type == 3) { talkDev_ = d; talk(d, reg); if (reg == 0) lastTalk_ = addr; }
  }

  void talk(Device *d, int reg) {
    if (reg == 3) {                                 // address and handler; exceptional event set
      talkBuf_[0] = (uint8_t)(0x40 | (d->srqEnable ? 0x20 : 0) | d->addr);
      talkBuf_[1] = d->handler;
      talkLen_ = 2;
      return;
    }
    if (d == &kbd_) {
      if (reg == 0 && keyCount_) {
        talkBuf_[0] = keyQueue_[0];
        talkBuf_[1] = keyCount_ > 1 ? keyQueue_[1] : 0xFF;
        const int n = keyCount_ > 1 ? 2 : 1;
        memmove(keyQueue_, keyQueue_ + n, (size_t)(keyCount_ - n));
        keyCount_ -= n;
        talkLen_ = 2;
      } else if (reg == 2) {                        // modifiers and LEDs: none held
        talkBuf_[0] = 0xFF;
        talkBuf_[1] = 0xFF;
        talkLen_ = 2;
      }
      return;
    }
    if (reg == 0 && hasData(d)) {                   // mouse: button up = 1, 7-bit deltas
      const int dx = clamp7(mouseDx_), dy = clamp7(mouseDy_);
      mouseDx_ -= dx;
      mouseDy_ -= dy;
      mouseBtnSent_ = mouseBtn_;
      talkBuf_[0] = (uint8_t)((mouseBtn_ ? 0 : 0x80) | (dy & 0x7F));
      talkBuf_[1] = (uint8_t)(0x80 | (dx & 0x7F));
      talkLen_ = 2;
    }
  }

  static int clamp7(int v) { return v > 63 ? 63 : v < -64 ? -64 : v; }

  void applyListen() {
    if (listenReg_ != 3 || listenLen_ < 2) return;
    Device *d = listenDev_;
    const uint8_t b0 = listenBuf_[0], id = listenBuf_[1];
    if (id == 0xFE || id == 0x00) {                 // a new address (0x00: and the SRQ enable)
      if (id == 0x00) d->srqEnable = (b0 & 0x20) != 0;
      d->addr = b0 & 0x0F;
    } else if (id == 0xFD || id == 0xFF) {
      // activator / self-test: nothing to do
    } else if ((d == &kbd_ && id >= 1 && id <= 3) || (d == &mouse_ && id >= 1 && id <= 2)) {
      d->handler = id;
    }
  }
};
