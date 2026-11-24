/* m68kcpu020.c -- Musashi's CPU core again, built as the Mac II's 68020 + 68881 (m68kconf020.h).
 * The path in MUSASHI_CNF is relative to umac/m68k.h, which includes it.
 */
#define MUSASHI_CNF "../../mac2/m68kconf020.h"
#include "m68k020_names.h"   /* first: m68kops.h is read before the config that would bring it */
#include "macii_iram.h"     /* the hot code in IRAM on the P4 */
#include "../mac/umac/m68kcpu.c"
