/* m68kops040.c -- Musashi's opcode handlers and jump table again, for the Quadra 700's 68040
 * (m68kcpu040.c). Not on the P4.
 */
#if !defined(ESP_PLATFORM)
#define MUSASHI_CNF "../../mac2/m68kconf040.h"
#include "m68k040_names.h"   /* first: m68kops.h is read before the config that would bring it */
#include "macquadra_names.h"
#include "../mac/umac/m68kops.c"
#endif
