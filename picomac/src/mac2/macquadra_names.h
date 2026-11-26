/* macquadra_names.h -- the Quadra 700 core is macii_core.cpp compiled a third time (macquadra_core.cpp,
 * MACII_QUADRA) against the 68040 (m68kcpu040.c). As maciici_names.h: its C symbols get their own
 * names so that all the cores link. MACII_PMMU: the paged bus (a TLB in front of the MMU's walk),
 * shared with the IIci. Included first by macquadra_core.cpp and m68kconf040.h.
 */
#ifndef MACQUADRA_NAMES_H
#define MACQUADRA_NAMES_H
#define MACII_QUADRA 1
#define MACII_PMMU 1

#define macii_read8          macquadra_read8
#define macii_read16         macquadra_read16
#define macii_read32         macquadra_read32
#define macii_write8         macquadra_write8
#define macii_write16        macquadra_write16
#define macii_write32        macquadra_write32
#define macii_irq_ack        macquadra_irq_ack
#define macii_pulse_reset    macquadra_pulse_reset
#define macii_illegal        macquadra_illegal
#define macii_instr_hook     macquadra_instr_hook
#define macii_fast           macquadra_fast
#define macii_fetch16_slow   macquadra_fetch16_slow
#define macii_fetch32_slow   macquadra_fetch32_slow
#define macii_op_index       macquadra_op_index
#define macii_op_handlers    macquadra_op_handlers
#define opCountDump          macquadra_opCountDump
#define maciicore_fullshot   macquadracore_fullshot
#define maciicore_mouse      macquadracore_mouse
#define maciicore_disk2      macquadracore_disk2
#define kMacII               kMacQuadra
#endif
