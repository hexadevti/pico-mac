/* m68k040_names.h -- every global symbol of Musashi gets a "m68k040_" twin, so that the Quadra 700's
 * 68040 copy (m68kcpu040.c, m68kops040.c) links next to the Plus's 68000, the Mac II's 68020 and the IIci's 68030.
 * The list is `nm -g --defined-only` of umac's m68kcpu.o and m68kops.o. softfloat is shared.
 */
#ifndef M68K040_NAMES_H
#define M68K040_NAMES_H

#define instruction_jump_table              m68k040_instruction_jump_table
#define m68040_fpu_op0                      m68k040_m68040_fpu_op0
#define m68040_fpu_op1                      m68k040_m68040_fpu_op1
#define m68881_mmu_ops                      m68k040_m68881_mmu_ops
#define pmmu_translate_addr                 m68k040_pmmu_translate_addr
#define m68k_context_size                   m68k040_context_size
#define m68k_cycles_remaining               m68k040_cycles_remaining
#define m68k_cycles_run                     m68k040_cycles_run
#define m68k_end_timeslice                  m68k040_end_timeslice
#define m68k_execute                        m68k040_execute
#define m68k_get_context                    m68k040_get_context
#define m68k_get_reg                        m68k040_get_reg
#define m68k_get_virq                       m68k040_get_virq
#define m68k_init                           m68k040_init
#define m68k_modify_timeslice               m68k040_modify_timeslice
#define m68k_pulse_bus_error                m68k040_pulse_bus_error
#define m68k_pulse_halt                     m68k040_pulse_halt
#define m68k_pulse_reset                    m68k040_pulse_reset
#define m68k_set_bkpt_ack_callback          m68k040_set_bkpt_ack_callback
#define m68k_set_cmpild_instr_callback      m68k040_set_cmpild_instr_callback
#define m68k_set_context                    m68k040_set_context
#define m68k_set_context_keep_hooks         m68k040_set_context_keep_hooks
#define m68k_set_cpu_type                   m68k040_set_cpu_type
#define m68k_set_fc_callback                m68k040_set_fc_callback
#define m68k_set_illg_instr_callback        m68k040_set_illg_instr_callback
#define m68k_set_instr_hook_callback        m68k040_set_instr_hook_callback
#define m68k_set_int_ack_callback           m68k040_set_int_ack_callback
#define m68k_set_irq                        m68k040_set_irq
#define m68k_set_pc_changed_callback        m68k040_set_pc_changed_callback
#define m68k_set_reg                        m68k040_set_reg
#define m68k_set_reset_instr_callback       m68k040_set_reset_instr_callback
#define m68k_set_rte_instr_callback         m68k040_set_rte_instr_callback
#define m68k_set_tas_instr_callback         m68k040_set_tas_instr_callback
#define m68k_set_virq                       m68k040_set_virq
#define m68k_state_register                 m68k040_state_register
#define m68k_disassemble                    m68k040_disassemble
#define m68k_disassemble_raw                m68k040_disassemble_raw
#define m68k_is_valid_instruction           m68k040_is_valid_instruction
#define m68ki_address_space                 m68k040i_address_space
#define m68ki_aerr_address                  m68k040i_aerr_address
#define m68ki_aerr_fc                       m68k040i_aerr_fc
#define m68ki_aerr_write_mode               m68k040i_aerr_write_mode
#define m68ki_cpu                           m68k040i_cpu
#define m68ki_cpu_names                     m68k040i_cpu_names
#define m68ki_ea_idx_cycle_table            m68k040i_ea_idx_cycle_table
#define m68ki_exception_cycle_table         m68k040i_exception_cycle_table
#define m68ki_initial_cycles                m68k040i_initial_cycles
#define m68ki_remaining_cycles              m68k040i_remaining_cycles
#define m68ki_shift_16_table                m68k040i_shift_16_table
#define m68ki_shift_32_table                m68k040i_shift_32_table
#define m68ki_shift_8_table                 m68k040i_shift_8_table
#define m68ki_tracing                       m68k040i_tracing
#define m68ki_static_instruction_jump_table m68k040i_static_instruction_jump_table
#define m68ki_instruction_jump_table        m68k040i_instruction_jump_table
#define m68ki_cycles                        m68k040i_cycles
#define m68ki_build_opcode_table            m68k040i_build_opcode_table

#endif
