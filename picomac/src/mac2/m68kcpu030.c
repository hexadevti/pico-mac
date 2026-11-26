/* m68kcpu030.c -- Musashi's CPU core a third time, as the Mac IIci's 68030 + FPU (m68kconf030.h).
 * Not on the P4 (too slow for a IIci there): the file is empty in that build.
 */
#if !defined(ESP_PLATFORM)
#define MUSASHI_CNF "../../mac2/m68kconf030.h"
#include "m68k030_names.h"   /* first: m68kops.h is read before the config that would bring it */
#include "maciici_names.h"
#include "../mac/umac/m68kcpu.c"
#endif
