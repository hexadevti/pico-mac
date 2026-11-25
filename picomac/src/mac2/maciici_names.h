/* maciici_names.h -- the Mac IIci core is macii_core.cpp compiled a second time (maciici_core.cpp,
 * MACII_IICI) against the 68030 (m68kcpu030.c). Its C symbols, which the 68030 calls, get their own
 * names here so that both cores link. Included first by maciici_core.cpp and m68kconf030.h.
 */
#ifndef MACIICI_NAMES_H
#define MACIICI_NAMES_H
#define MACII_IICI 1

#define macii_read8          maciici_read8
#define macii_read16         maciici_read16
#define macii_read32         maciici_read32
#define macii_write8         maciici_write8
#define macii_write16        maciici_write16
#define macii_write32        maciici_write32
#define macii_irq_ack        maciici_irq_ack
#define macii_pulse_reset    maciici_pulse_reset
#define macii_illegal        maciici_illegal
#define macii_instr_hook     maciici_instr_hook
#define macii_fast           maciici_fast
#define macii_fetch16_slow   maciici_fetch16_slow
#define macii_fetch32_slow   maciici_fetch32_slow
#define macii_op_index       maciici_op_index
#define macii_op_handlers    maciici_op_handlers
#define opCountDump          maciici_opCountDump
#define maciicore_fullshot   maciicicore_fullshot
#define maciicore_mouse      maciicicore_mouse
#define maciicore_disk2      maciicicore_disk2
#define kMacII               kMacIIci
#endif
