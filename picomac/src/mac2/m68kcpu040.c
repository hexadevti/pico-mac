/* m68kcpu040.c -- Musashi again, as the Quadra 700's 68040 + FPU (m68kconf040.h, the MMU and cache
 * instructions in macquadra_mmu.h). Not on the P4.
 */
#if !defined(ESP_PLATFORM)
#define MUSASHI_CNF "../../mac2/m68kconf040.h"
#include "m68k040_names.h"
#include "macquadra_names.h"
#include "../mac/umac/m68kcpu.c"
#endif
