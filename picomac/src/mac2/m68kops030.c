/* m68kops030.c -- Musashi's opcode handlers and jump table again, for the Mac IIci's 68030
 * (m68kcpu030.c). Not on the P4.
 */
#if !defined(ESP_PLATFORM)
#define MUSASHI_CNF "../../mac2/m68kconf030.h"
#include "m68k030_names.h"   /* first: m68kops.h is read before the config that would bring it */
#include "maciici_names.h"
#include "../mac/umac/m68kops.c"
#endif
