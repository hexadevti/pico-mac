/* maciici_mmu.h -- the 68030's PMMU instructions, for the Mac IIci (m68kconf030.h's M68K_MMU_HEADER:
 * m68kcpu.c includes this in place of m68kmmu.h, so READ_EA_* and the EA macros are at hand).
 *
 * Only the instructions are here. The registers, the table walk and the translation cache live in
 * the core (macii_core.cpp, MACII_IICI), because the bus translates every access there; Musashi's
 * own M68K_EMULATE_PMMU stays off. The 68030 forms (from the MC68030 manual, and MAME's m68kmmu.h):
 *   PMOVE   (modes >> 13) 0: TT0 (sel 2) / TT1 (sel 3), 2: TC (0) / SRP (2) / CRP (3), 3: MMUSR;
 *           bit 9 = to the EA (a read of the register), bit 8 = FD (no flush; we flush anyway)
 *   PLOAD   (modes & 0xFDE0) == 0x2000      PFLUSH  (modes & 0xE200) == 0x2000
 *   PTEST   (modes & 0xE000) == 0x8000: level in bits 10-12, bit 9 = write, bit 8 = An gets the address
 */
#ifdef __cplusplus
extern "C" {
#endif
/* The core's side (macii_core.cpp). reg: MACIICI_MMU_*. set: 0, or 1 for a bad value (an MMU
 * configuration exception). ptest: the MMUSR, and the physical address in *pa. */
enum { MACIICI_MMU_TC, MACIICI_MMU_SRP, MACIICI_MMU_CRP, MACIICI_MMU_TT0, MACIICI_MMU_TT1, MACIICI_MMU_SR };
int                maciici_mmu_set(int reg, unsigned long long value);
unsigned long long maciici_mmu_get(int reg);
void               maciici_mmu_flush(void);
unsigned int       maciici_mmu_ptest(unsigned int addr, int fc, int write, int level, unsigned int *pa);
void               maciici_mmu_pload(unsigned int addr, int fc, int write);
#ifdef __cplusplus
}
#endif

#ifndef MACIICI_MMU_API_ONLY   /* the core wants the declarations above only */
#define MACIICI_EXCEPTION_MMU_CONFIG 56

/* The effective address of PLOAD/PTEST/PFLUSH (control alterable modes); 0 = not one. */
static int maciici_mmu_ea(int ea, uint *addr)
{
	switch ((ea >> 3) & 7)
	{
		case 2: *addr = REG_A[ea & 7]; return 1;
		case 5: *addr = EA_AY_DI_32(); return 1;
		case 6: *addr = EA_AY_IX_32(); return 1;
		case 7:
			if ((ea & 7) == 0) { *addr = EA_AW_32(); return 1; }
			if ((ea & 7) == 1) { *addr = EA_AL_32(); return 1; }
			break;
	}
	return 0;
}

/* The function code of PLOAD/PTEST/PFLUSH: SFC, DFC, Dn or an immediate. */
static int maciici_mmu_fc(uint modes)
{
	if ((modes & 0x1F) == 0) return REG_SFC & 7;
	if ((modes & 0x1F) == 1) return REG_DFC & 7;
	if (((modes >> 3) & 3) == 1) return REG_D[modes & 7] & 7;
	return modes & 7;
}

void m68881_mmu_ops(void)
{
	const uint ea = m68ki_cpu.ir & 0x3F;
	uint modes, addr;

	if ((m68ki_cpu.ir & 0xFF80) != 0xF000)   /* PBcc, PDBcc, PScc, PTRAPcc: the 68851 only */
	{
		m68ki_exception_1111();
		return;
	}
	modes = OPER_I_16();

	if ((modes & 0xFDE0) == 0x2000)          /* PLOAD */
	{
		if (!maciici_mmu_ea(ea, &addr)) { m68ki_exception_1111(); return; }
		maciici_mmu_pload(addr, maciici_mmu_fc(modes), !(modes & 0x200));
		return;
	}
	if ((modes & 0xE200) == 0x2000)          /* PFLUSH, PFLUSHA: everything goes */
	{
		if (((modes >> 10) & 7) >= 6) maciici_mmu_ea(ea, &addr);   /* the EA forms still consume it */
		maciici_mmu_flush();
		return;
	}
	if ((modes & 0xE000) == 0x8000)          /* PTEST */
	{
		uint pa = 0;
		if (!maciici_mmu_ea(ea, &addr)) { m68ki_exception_1111(); return; }
		maciici_mmu_set(MACIICI_MMU_SR, maciici_mmu_ptest(addr, maciici_mmu_fc(modes), !(modes & 0x200), (modes >> 10) & 7, &pa));
		if (modes & 0x100) REG_A[(modes >> 5) & 7] = pa;
		return;
	}

	switch ((modes >> 13) & 7)
	{
		case 0:                                  /* TT0, TT1 */
		case 2:                                  /* TC, SRP, CRP */
		{
			const uint sel = (modes >> 10) & 7;
			int reg = -1;
			if (((modes >> 13) & 7) == 0) reg = sel == 2 ? MACIICI_MMU_TT0 : sel == 3 ? MACIICI_MMU_TT1 : -1;
			else reg = sel == 0 ? MACIICI_MMU_TC : sel == 2 ? MACIICI_MMU_SRP : sel == 3 ? MACIICI_MMU_CRP : -1;
			if (reg < 0) { m68ki_exception_1111(); return; }
			const int wide = reg == MACIICI_MMU_SRP || reg == MACIICI_MMU_CRP;
			if (modes & 0x200)
			{
				if (wide) WRITE_EA_64(ea, maciici_mmu_get(reg));
				else WRITE_EA_32(ea, (uint32)maciici_mmu_get(reg));
			}
			else
			{
				const unsigned long long v = wide ? READ_EA_64(ea) : READ_EA_32(ea);
				if (maciici_mmu_set(reg, v)) m68ki_exception_trap(MACIICI_EXCEPTION_MMU_CONFIG);
			}
			return;
		}
		case 3:                                  /* MMUSR */
			if (modes & 0x200) WRITE_EA_16(ea, (uint16)maciici_mmu_get(MACIICI_MMU_SR));
			else maciici_mmu_set(MACIICI_MMU_SR, READ_EA_16(ea));
			return;
	}
	m68ki_exception_1111();
}
#endif /* MACIICI_MMU_API_ONLY */
