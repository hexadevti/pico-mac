/* macii_bus.h -- what the Mac II's 68020 (Musashi, m68kconf020.h) calls: the bus and the
 * interrupt acknowledge. Implemented in macii_core.cpp.
 */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
unsigned int macii_read8(unsigned int address);
unsigned int macii_read16(unsigned int address);
unsigned int macii_read32(unsigned int address);
void         macii_write8(unsigned int address, unsigned int value);
void         macii_write16(unsigned int address, unsigned int value);
void         macii_write32(unsigned int address, unsigned int value);
int          macii_irq_ack(int level);
void         macii_pulse_reset(void);
int          macii_illegal(int opcode);
#if defined(MACII_PC_HISTORY) || defined(MACII_OPCOUNT)
void         macii_instr_hook(unsigned int pc);   /* every instruction (a debugging build only) */
#endif   /* an illegal instruction: logged; 0 = take the exception */

/* The Mac IIci and the Quadra (MACII_PMMU): PMMUs map every access, so the fast path is a
 * translation cache of 4 KB pages, direct-mapped by the logical page number. rtag/wtag hold the
 * logical page (a & ~0xFFF; 1 = empty) whose host bytes are rhost/whost; a write entry is there only
 * once a write has gone through the table walk (it sets the page's M bit). The core empties it on
 * any change of the MMU registers, PFLUSH or the overlay. An access that would cross a page goes
 * the slow way. The fetch window is one cached page. */
#if defined(MACII_PMMU)
#define MACIICI_TLB 1024
typedef struct {
  unsigned int rtag[MACIICI_TLB], wtag[MACIICI_TLB];
  unsigned char *rhost[MACIICI_TLB], *whost[MACIICI_TLB];
  const unsigned char *fbase;  /* the fetch window: fbase[a - flo] for a - flo < flen */
  unsigned int flo, flen;
} MaciiFast;
extern MaciiFast macii_fast;
unsigned int macii_fetch16_slow(unsigned int address);
unsigned int macii_fetch32_slow(unsigned int address);
#define MACII_FAST_INLINE static inline
#define MACIICI_SLOT(a) (((a) >> 12) & (MACIICI_TLB - 1))

MACII_FAST_INLINE unsigned int macii_fr8(unsigned int a) {
  const unsigned int i = MACIICI_SLOT(a);
  if (macii_fast.rtag[i] == (a & ~0xFFFu)) return macii_fast.rhost[i][a & 0xFFF];
  return macii_read8(a);
}
MACII_FAST_INLINE unsigned int macii_fr16(unsigned int a) {
  const unsigned int i = MACIICI_SLOT(a);
  if (macii_fast.rtag[i] == (a & ~0xFFFu) && (a & 0xFFF) != 0xFFF) {
    const unsigned char *m = macii_fast.rhost[i] + (a & 0xFFF);
    return (unsigned int)m[0] << 8 | m[1];
  }
  return macii_read16(a);
}
MACII_FAST_INLINE unsigned int macii_fr32(unsigned int a) {
  const unsigned int i = MACIICI_SLOT(a);
  if (macii_fast.rtag[i] == (a & ~0xFFFu) && (a & 0xFFF) <= 0xFFC) {
    const unsigned char *m = macii_fast.rhost[i] + (a & 0xFFF);
    return (unsigned int)m[0] << 24 | (unsigned int)m[1] << 16 | (unsigned int)m[2] << 8 | m[3];
  }
  return macii_read32(a);
}
MACII_FAST_INLINE void macii_fw8(unsigned int a, unsigned int v) {
  const unsigned int i = MACIICI_SLOT(a);
  if (macii_fast.wtag[i] == (a & ~0xFFFu)) { macii_fast.whost[i][a & 0xFFF] = (unsigned char)v; return; }
  macii_write8(a, v);
}
MACII_FAST_INLINE void macii_fw16(unsigned int a, unsigned int v) {
  const unsigned int i = MACIICI_SLOT(a);
  if (macii_fast.wtag[i] == (a & ~0xFFFu) && (a & 0xFFF) != 0xFFF) {
    unsigned char *m = macii_fast.whost[i] + (a & 0xFFF);
    m[0] = (unsigned char)(v >> 8); m[1] = (unsigned char)v;
    return;
  }
  macii_write16(a, v);
}
MACII_FAST_INLINE void macii_fw32(unsigned int a, unsigned int v) {
  const unsigned int i = MACIICI_SLOT(a);
  if (macii_fast.wtag[i] == (a & ~0xFFFu) && (a & 0xFFF) <= 0xFFC) {
    unsigned char *m = macii_fast.whost[i] + (a & 0xFFF);
    m[0] = (unsigned char)(v >> 24); m[1] = (unsigned char)(v >> 16); m[2] = (unsigned char)(v >> 8); m[3] = (unsigned char)v;
    return;
  }
  macii_write32(a, v);
}
MACII_FAST_INLINE unsigned int macii_fetch16(unsigned int a) {
  const unsigned int d = a - macii_fast.flo;
  if (d < macii_fast.flen) { const unsigned char *m = macii_fast.fbase + d; return (unsigned int)m[0] << 8 | m[1]; }
  return macii_fetch16_slow(a);
}
MACII_FAST_INLINE unsigned int macii_fetch32(unsigned int a) {
  const unsigned int d = a - macii_fast.flo;
  if (d < macii_fast.flen) {
    const unsigned char *m = macii_fast.fbase + d;
    return (unsigned int)m[0] << 24 | (unsigned int)m[1] << 16 | (unsigned int)m[2] << 8 | m[3];
  }
  return macii_fetch32_slow(a);
}
MACII_FAST_INLINE unsigned int macii_fetch8(unsigned int a) { return macii_fetch16(a & ~1u) >> (a & 1 ? 0 : 8) & 0xFF; }
#else
/* The fast path: RAM reads and writes, and instruction fetches from RAM or ROM, without a call.
 * macii_core.cpp keeps it up to date (maciiFastUpdate) when the overlay or the 24/32-bit mode
 * changes; anything outside it goes to the functions above. */
typedef struct {
  unsigned char *ram;
  unsigned int mask;           /* 0xFFFFFF in 24-bit mode, else all ones */
  unsigned int limit;          /* masked addresses below it are RAM, room for a long (0: none) */
  const unsigned char *fbase;  /* the fetch window: fbase[a - flo] for a - flo < flen */
  unsigned int flo, flen;
} MaciiFast;
extern MaciiFast macii_fast;
unsigned int macii_fetch16_slow(unsigned int address);
unsigned int macii_fetch32_slow(unsigned int address);
/* On the P4 GCC left these out of line, a call per access (~20% of a frame in them): forced in. */
#if defined(ESP_PLATFORM)
#define MACII_FAST_INLINE static inline __attribute__((always_inline))
#else
#define MACII_FAST_INLINE static inline
#endif

MACII_FAST_INLINE unsigned int macii_fr8(unsigned int a) {
  const unsigned int p = a & macii_fast.mask;
  if (p < macii_fast.limit) return macii_fast.ram[p];
  return macii_read8(a);
}
MACII_FAST_INLINE unsigned int macii_fr16(unsigned int a) {
  const unsigned int p = a & macii_fast.mask;
  if (p < macii_fast.limit) { const unsigned char *m = macii_fast.ram + p; return (unsigned int)m[0] << 8 | m[1]; }
  return macii_read16(a);
}
MACII_FAST_INLINE unsigned int macii_fr32(unsigned int a) {
  const unsigned int p = a & macii_fast.mask;
  if (p < macii_fast.limit) {
    const unsigned char *m = macii_fast.ram + p;
    return (unsigned int)m[0] << 24 | (unsigned int)m[1] << 16 | (unsigned int)m[2] << 8 | m[3];
  }
  return macii_read32(a);
}
MACII_FAST_INLINE void macii_fw8(unsigned int a, unsigned int v) {
  const unsigned int p = a & macii_fast.mask;
  if (p < macii_fast.limit) { macii_fast.ram[p] = (unsigned char)v; return; }
  macii_write8(a, v);
}
MACII_FAST_INLINE void macii_fw16(unsigned int a, unsigned int v) {
  const unsigned int p = a & macii_fast.mask;
  if (p < macii_fast.limit) { unsigned char *m = macii_fast.ram + p; m[0] = (unsigned char)(v >> 8); m[1] = (unsigned char)v; return; }
  macii_write16(a, v);
}
MACII_FAST_INLINE void macii_fw32(unsigned int a, unsigned int v) {
  const unsigned int p = a & macii_fast.mask;
  if (p < macii_fast.limit) {
    unsigned char *m = macii_fast.ram + p;
    m[0] = (unsigned char)(v >> 24); m[1] = (unsigned char)(v >> 16); m[2] = (unsigned char)(v >> 8); m[3] = (unsigned char)v;
    return;
  }
  macii_write32(a, v);
}
MACII_FAST_INLINE unsigned int macii_fetch16(unsigned int a) {
  const unsigned int d = a - macii_fast.flo;
  if (d < macii_fast.flen) { const unsigned char *m = macii_fast.fbase + d; return (unsigned int)m[0] << 8 | m[1]; }
  return macii_fetch16_slow(a);
}
MACII_FAST_INLINE unsigned int macii_fetch32(unsigned int a) {
  const unsigned int d = a - macii_fast.flo;
  if (d < macii_fast.flen) {
    const unsigned char *m = macii_fast.fbase + d;
    return (unsigned int)m[0] << 24 | (unsigned int)m[1] << 16 | (unsigned int)m[2] << 8 | m[3];
  }
  return macii_fetch32_slow(a);
}
MACII_FAST_INLINE unsigned int macii_fetch8(unsigned int a) { return macii_fetch16(a & ~1u) >> (a & 1 ? 0 : 8) & 0xFF; }
#endif   /* MACII_PMMU */
#ifdef __cplusplus
}
#endif
