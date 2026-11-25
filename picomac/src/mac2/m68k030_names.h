/* m68k030_names.h -- every global symbol of Musashi gets a "m68k030_" twin, so that the Mac IIci's
 * 68030 copy (m68kcpu030.c, m68kops030.c) links next to the Mac Plus's 68000 and the Mac II's 68020.
 * The list is `nm -g --defined-only` of umac's m68kcpu.o and m68kops.o. softfloat is shared.
 */
#ifndef M68K030_NAMES_H
#define M68K030_NAMES_H

#define instruction_jump_table              m68k030_instruction_jump_table
#define m68040_fpu_op0                      m68k030_m68040_fpu_op0
#define m68040_fpu_op1                      m68k030_m68040_fpu_op1
#define m68881_mmu_ops                      m68k030_m68881_mmu_ops
#define pmmu_translate_addr                 m68k030_pmmu_translate_addr
#define m68k_context_size                   m68k030_context_size
#define m68k_cycles_remaining               m68k030_cycles_remaining
#define m68k_cycles_run                     m68k030_cycles_run
#define m68k_end_timeslice                  m68k030_end_timeslice
#define m68k_execute                        m68k030_execute
#define m68k_get_context                    m68k030_get_context
#define m68k_get_reg                        m68k030_get_reg
#define m68k_get_virq                       m68k030_get_virq
#define m68k_init                           m68k030_init
#define m68k_modify_timeslice               m68k030_modify_timeslice
#define m68k_pulse_bus_error                m68k030_pulse_bus_error
#define m68k_pulse_halt                     m68k030_pulse_halt
#define m68k_pulse_reset                    m68k030_pulse_reset
#define m68k_set_bkpt_ack_callback          m68k030_set_bkpt_ack_callback
#define m68k_set_cmpild_instr_callback      m68k030_set_cmpild_instr_callback
#define m68k_set_context                    m68k030_set_context
#define m68k_set_context_keep_hooks         m68k030_set_context_keep_hooks
#define m68k_set_cpu_type                   m68k030_set_cpu_type
#define m68k_set_fc_callback                m68k030_set_fc_callback
#define m68k_set_illg_instr_callback        m68k030_set_illg_instr_callback
#define m68k_set_instr_hook_callback        m68k030_set_instr_hook_callback
#define m68k_set_int_ack_callback           m68k030_set_int_ack_callback
#define m68k_set_irq                        m68k030_set_irq
#define m68k_set_pc_changed_callback        m68k030_set_pc_changed_callback
#define m68k_set_reg                        m68k030_set_reg
#define m68k_set_reset_instr_callback       m68k030_set_reset_instr_callback
#define m68k_set_rte_instr_callback         m68k030_set_rte_instr_callback
#define m68k_set_tas_instr_callback         m68k030_set_tas_instr_callback
#define m68k_set_virq                       m68k030_set_virq
#define m68k_state_register                 m68k030_state_register
#define m68k_disassemble                    m68k030_disassemble
#define m68k_disassemble_raw                m68k030_disassemble_raw
#define m68k_is_valid_instruction           m68k030_is_valid_instruction
#define m68ki_address_space                 m68k030i_address_space
#define m68ki_aerr_address                  m68k030i_aerr_address
#define m68ki_aerr_fc                       m68k030i_aerr_fc
#define m68ki_aerr_write_mode               m68k030i_aerr_write_mode
#define m68ki_cpu                           m68k030i_cpu
#define m68ki_cpu_names                     m68k030i_cpu_names
#define m68ki_ea_idx_cycle_table            m68k030i_ea_idx_cycle_table
#define m68ki_exception_cycle_table         m68k030i_exception_cycle_table
#define m68ki_initial_cycles                m68k030i_initial_cycles
#define m68ki_remaining_cycles              m68k030i_remaining_cycles
#define m68ki_shift_16_table                m68k030i_shift_16_table
#define m68ki_shift_32_table                m68k030i_shift_32_table
#define m68ki_shift_8_table                 m68k030i_shift_8_table
#define m68ki_tracing                       m68k030i_tracing
#define m68ki_static_instruction_jump_table m68k030i_static_instruction_jump_table
#define m68ki_instruction_jump_table        m68k030i_instruction_jump_table
#define m68ki_cycles                        m68k030i_cycles
#define m68ki_build_opcode_table            m68k030i_build_opcode_table

#endif
