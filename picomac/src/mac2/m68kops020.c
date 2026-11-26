/* m68kops020.c -- Musashi's opcode handlers and jump table again, for the Mac II's 68020
 * (m68kcpu020.c). The handlers test the CPU type, which is fixed at 68020 here.
 */
#define MUSASHI_CNF "../../mac2/m68kconf020.h"
#include "m68k020_names.h"   /* first: m68kops.h is read before the config that would bring it */
#define MACII_OPS_TU
#include "macii_iram.h"     /* the hot code in IRAM on the P4 */
#include "../mac/umac/m68kops.c"
