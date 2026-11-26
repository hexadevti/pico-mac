/* ======================================================================== */
/* ========================= LICENSING & COPYRIGHT ======================== */
/* ======================================================================== */
/*
 *                                  MUSASHI
 *                                Version 3.32
 *
 * A portable Motorola M680x0 processor emulation engine.
 * Copyright Karl Stenerud.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* pico-mac: the Mac IIci's 68030 + FPU (src/mac2). A copy of m68kconf020.h with the CPU type
 * fixed at 68030 and the PMMU instructions in maciici_mmu.h (M68K_MMU_HEADER). The translation
 * itself is in the bus (macii_bus.h, MACII_IICI: a TLB; the walk in macii_core.cpp). m68k030_names.h
 * renames Musashi's globals, maciici_names.h the core's, so all three CPUs link together.
 * Desktop and web only: the P4 is too slow for a IIci (m68kcpu030.c is empty there).
 */


#ifndef M68KCONF030__HEADER
#define M68KCONF030__HEADER

#include "m68k030_names.h"
#include "maciici_names.h"


/* Configuration switches.
 * Use OPT_SPECIFY_HANDLER for configuration options that allow callbacks.
 * OPT_SPECIFY_HANDLER causes the core to link directly to the function
 * or macro you specify, rather than using callback functions whose pointer
 * must be passed in using m68k_set_xxx_callback().
 */
#define OPT_OFF             0
#define OPT_ON              1
#define OPT_SPECIFY_HANDLER 2


/* ======================================================================== */
/* ============================== MAME STUFF ============================== */
/* ======================================================================== */

/* If you're compiling this for MAME, only change M68K_COMPILE_FOR_MAME
 * to OPT_ON and use m68kmame.h to configure the 68k core.
 */
#ifndef M68K_COMPILE_FOR_MAME
#define M68K_COMPILE_FOR_MAME      OPT_OFF
#endif /* M68K_COMPILE_FOR_MAME */


#if M68K_COMPILE_FOR_MAME == OPT_OFF


/* ======================================================================== */
/* ============================= CONFIGURATION ============================ */
/* ======================================================================== */

/* Turn ON if you want to use the following M68K variants */
/* pico-mac: 010 and EC020 are ON although the CPU is a fixed 68020. With them OFF, m68kcpu.h's
 * CPU_TYPE_IS_010_LESS falls back to CPU_TYPE_IS_020_LESS, which is 1 without the 030: the 68020
 * then decoded indexed addressing (and more) as a 68000. ON, they are exact bit tests on the fixed
 * type, folded at compile time. */
#define M68K_EMULATE_010            OPT_ON
#define M68K_EMULATE_EC020          OPT_ON
#define M68K_EMULATE_020            OPT_ON
#define M68K_EMULATE_FPU            OPT_ON    /* pico-mac: the Mac II's 68881 */
#define M68K_EMULATE_040            OPT_OFF


/* If ON, the CPU will call m68k_read_immediate_xx() for immediate addressing
 * and m68k_read_pcrelative_xx() for PC-relative addressing.
 * If off, all read requests from the CPU will be redirected to m68k_read_xx()
 */
#define M68K_SEPARATE_READS         OPT_ON    /* pico-mac: fetches through macii_fetch16/32 */

/* If ON, the CPU will call m68k_write_32_pd() when it executes move.l with a
 * predecrement destination EA mode instead of m68k_write_32().
 * To simulate real 68k behavior, m68k_write_32_pd() must first write the high
 * word to [address+2], and then write the low word to [address].
 */
#define M68K_SIMULATE_PD_WRITES     OPT_OFF

/* If ON, CPU will call the interrupt acknowledge callback when it services an
 * interrupt.
 * If off, all interrupts will be autovectored and all interrupt requests will
 * auto-clear when the interrupt is serviced.
 */
#define M68K_EMULATE_INT_ACK        OPT_SPECIFY_HANDLER
#define M68K_INT_ACK_CALLBACK(A)    macii_irq_ack(A)


/* If ON, CPU will call the breakpoint acknowledge callback when it encounters
 * a breakpoint instruction and it is running a 68010+.
 */
#define M68K_EMULATE_BKPT_ACK       OPT_OFF
#define M68K_BKPT_ACK_CALLBACK()    your_bkpt_ack_handler_function()


/* If ON, the CPU will monitor the trace flags and take trace exceptions
 */
#define M68K_EMULATE_TRACE          OPT_OFF


/* If ON, CPU will call the output reset callback when it encounters a reset
 * instruction.
 */
#define M68K_EMULATE_RESET          OPT_SPECIFY_HANDLER
#define M68K_RESET_CALLBACK()       macii_pulse_reset()

/* If ON, CPU will call the callback when it encounters a cmpi.l #v, dn
 * instruction.
 */
#define M68K_CMPILD_HAS_CALLBACK     OPT_OFF
#define M68K_CMPILD_CALLBACK(v,r)    your_cmpild_handler_function(v,r)


/* If ON, CPU will call the callback when it encounters a rte
 * instruction.
 */
#define M68K_RTE_HAS_CALLBACK       OPT_OFF
#define M68K_RTE_CALLBACK()         your_rte_handler_function()

/* If ON, CPU will call the callback when it encounters a tas
 * instruction.
 */
#define M68K_TAS_HAS_CALLBACK       OPT_OFF
#define M68K_TAS_CALLBACK()         your_tas_handler_function()

/* If ON, CPU will call the callback when it encounters an illegal instruction
 * passing the opcode as argument. If the callback returns 1, then it's considered
 * as a normal instruction, and the illegal exception in canceled. If it returns 0,
 * the exception occurs normally.
 * The callback looks like int callback(int opcode)
 * You should put OPT_SPECIFY_HANDLER here if you cant to use it, otherwise it will
 * use a dummy default handler and you'll have to call m68k_set_illg_instr_callback explicitely
 */
#define M68K_ILLG_HAS_CALLBACK	    OPT_SPECIFY_HANDLER   /* pico-mac: logged by the Mac II core */
#define M68K_ILLG_CALLBACK(opcode)  macii_illegal(opcode)

/* If ON, CPU will call the set fc callback on every memory access to
 * differentiate between user/supervisor, program/data access like a real
 * 68000 would.  This should be enabled and the callback should be set if you
 * want to properly emulate the m68010 or higher. (moves uses function codes
 * to read/write data from different address spaces)
 */
#define M68K_EMULATE_FC             OPT_OFF
#define M68K_SET_FC_CALLBACK(A)     cpu_set_fc(A)

/* If ON, CPU will call the pc changed callback when it changes the PC by a
 * large value.  This allows host programs to be nicer when it comes to
 * fetching immediate data and instructions on a banked memory system.
 */
#define M68K_MONITOR_PC             OPT_OFF
#define M68K_SET_PC_CALLBACK(A)     your_pc_changed_handler_function(A)


/* If ON, CPU will call the instruction hook callback before every
 * instruction.
 */
#if defined(MACII_PC_HISTORY) || defined(MACII_OPCOUNT)   /* pico-mac: the Mac II core's PC history, for debugging the ROM */
#define M68K_INSTRUCTION_HOOK       OPT_SPECIFY_HANDLER
#define M68K_INSTRUCTION_CALLBACK(pc) macii_instr_hook(pc)
#else
#define M68K_INSTRUCTION_HOOK       OPT_OFF
#endif

/* If ON, the CPU will emulate the 4-byte prefetch queue of a real 68000 */
#define M68K_EMULATE_PREFETCH       OPT_OFF


/* If ON, the CPU will generate address error exceptions if it tries to
 * access a word or longword at an odd address.
 * NOTE: This is only emulated properly for 68000 mode.
 */
#define M68K_EMULATE_ADDRESS_ERROR  OPT_OFF


/* Turn ON to enable logging of illegal instruction calls.
 * M68K_LOG_FILEHANDLE must be #defined to a stdio file stream.
 * Turn on M68K_LOG_1010_1111 to log all 1010 and 1111 calls.
 */
#define M68K_LOG_ENABLE             OPT_OFF
#define M68K_LOG_1010_1111          OPT_OFF
#define M68K_LOG_FILEHANDLE         some_file_handle

/* Build in the disassembler.  If disabled, the API still exists
 * but does nothing.  Turn off to save text/bss space.
 */
#define M68K_DASM_ENABLE            ENABLE_DASM

/* Use dynamically-created tables for decode and cyclecounts (if
 * enabled by CYCLE_COUNTING), as opposed to tables created at build
 * time.  Using the dynamic version of the decode jumptable adds 256KB
 * to RAM usage but makes the binary smaller.  Using the static
 * version adds 256KB to the binary and no RAM, but doesn't currently
 * support instruction cycle counts.
 */
#define M68K_DYNAMIC_INSTR_TABLES   OPT_OFF

/* Count instruction cycles.  This costs a table (created at runtime
 * in RAM if DYNAMIC_INSTR_TABLES is on).
 *
 * NOTE: This is not currently supported when DYNAMIC_INSTR_TABLES is
 * off.
 */
#define M68K_CYCLE_COUNTING         OPT_OFF

#define M68K_FIXED_CPU_TYPE         CPU_TYPE_030
#define M68K_FIXED_CPU_API          M68K_CPU_TYPE_68030
/* pico-mac: the cost of an instruction. A 68020 takes 5-8 clocks for most of them, and the Mac II
 * (no external cache) ran about 2.6 MIPS, ~6 clocks each. 4 (the first guess) made "100%" a Mac II
 * about 50% faster than the real one, which the P4 couldn't reach (91% in Prince of Persia); 5 still
 * errs on the fast side. The ROM times its loops against the VIA, so nothing else depends on it. */
#define M68K_STATIC_INSTR_CYCLES    5   /* the 030 runs from its caches at about the 020's clocks per instruction */

#define M68K_BUS_ERR_ENABLE         OPT_OFF

/* ----------------------------- COMPATIBILITY ---------------------------- */

/* The following options set optimizations that violate the current ANSI
 * standard, but will be compliant under the forthcoming C9X standard.
 */


/* If ON, the enulation core will use 64-bit integers to speed up some
 * operations.
*/
#define M68K_USE_64_BIT  OPT_ON

/* pico-mac: PMOVE/PFLUSH/PLOAD/PTEST, ours (m68kcpu.c includes it in place of m68kmmu.h) */
#define M68K_MMU_HEADER "../../mac2/maciici_mmu.h"

#include "macii_bus.h"

#define m68k_read_memory_8(A) macii_fr8(A)
#define m68k_read_memory_16(A) macii_fr16(A)
#define m68k_read_memory_32(A) macii_fr32(A)
#define m68k_read_instr_16(A) macii_fetch16(A)
#define m68k_read_immediate_16(A) macii_fetch16(A)
#define m68k_read_immediate_32(A) macii_fetch32(A)
#define m68k_read_pcrelative_8(A) macii_fetch8(A)
#define m68k_read_pcrelative_16(A) macii_fetch16(A)
#define m68k_read_pcrelative_32(A) macii_fetch32(A)

#define m68k_read_disassembler_16(A) macii_read16(A)
#define m68k_read_disassembler_32(A) macii_read32(A)

#define m68k_write_memory_8(A, V) macii_fw8(A, V)
#define m68k_write_memory_16(A, V) macii_fw16(A, V)
#define m68k_write_memory_32(A, V) macii_fw32(A, V)

#ifdef PICO
#include "pico.h"
#define M68K_FAST_FUNC(x)       __not_in_flash_func(x)
#endif
/* pico-mac: the opcode dispatch through a 16-bit index into the ~2000 distinct handlers, both in
 * internal RAM (macii_core.cpp, jumpTableToRam): 136 KB instead of the 256 KB pointer table, which
 * no longer fit beside the code in IRAM (macii_iram.h). */
extern unsigned short *macii_op_index;
extern void (**macii_op_handlers)(void);
#define M68K_DISPATCH(ir)       macii_op_handlers[macii_op_index[ir]]()



#endif /* M68K_COMPILE_FOR_MAME */

/* ======================================================================== */
/* ============================== END OF FILE ============================= */
/* ======================================================================== */

#endif /* M68KCONF030__HEADER */
