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
#ifdef __cplusplus
}
#endif
