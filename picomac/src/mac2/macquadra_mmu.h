/* macquadra_mmu.h -- the 68040's own instructions, for the Quadra 700 (m68kconf040.h's M68K_MMU_HEADER:
 * m68kcpu.c includes this in place of m68kmmu.h, so the EA macros and Musashi's internals are at hand).
 *
 * Musashi's 040 leaves MOVEC's MMU registers as TODOs, has one MOVE16 form, no CINV/CPUSH/PTEST and a
 * 68881 FSAVE frame. The handlers here replace those opcodes in the core's dispatch table
 * (macii_core.cpp, MACII_QUADRA). The MMU itself (the walk, the TLB) is in the core's bus, as the IIci's.
 * From the MC68040 user's manual:
 *   MOVEC     0x003 TC, 0x004/5 ITT0/1, 0x006/7 DTT0/1, 0x805 MMUSR, 0x806 URP, 0x807 SRP
 *   CINV      0xF400 | cache << 6 | scope << 3 | An    CPUSH: the same | 0x20      (no caches here: no-ops)
 *   PFLUSH    0xF500 | opmode << 3 | An (0 PFLUSHN (An), 1 PFLUSH (An), 2 PFLUSHAN, 3 PFLUSHA)
 *   PTEST     0xF548 | An (write), 0xF568 | An (read); the function code is DFC's
 *   MOVE16    0xF600 | mode << 3 | Ay + a long address; 0xF620 | Ax + a word with Ay in bits 12-14
 *   FSAVE     0xF300 | ea: a NULL (0) or IDLE (0x41000000) frame, 4 bytes     FRESTORE 0xF340 | ea
 */
#ifdef __cplusplus
extern "C" {
#endif
/* The core's side (macii_core.cpp). ptest: the MMUSR. */
enum { MACQUADRA_MMU_TC, MACQUADRA_MMU_URP, MACQUADRA_MMU_SRP, MACQUADRA_MMU_ITT0, MACQUADRA_MMU_ITT1,
       MACQUADRA_MMU_DTT0, MACQUADRA_MMU_DTT1, MACQUADRA_MMU_SR };
void         macquadra_mmu_set(int reg, unsigned int value);
unsigned int macquadra_mmu_get(int reg);
void         macquadra_mmu_flush(void);
unsigned int macquadra_mmu_ptest(unsigned int addr, int fc, int write);
/* The handlers, for the core's dispatch table. */
void macquadra_op_movec_cr(void);
void macquadra_op_movec_rc(void);
void macquadra_op_cinv(void);
void macquadra_op_pflush(void);
void macquadra_op_ptest(void);
void macquadra_op_move16(void);
void macquadra_op_fsave(void);
void m68881_mmu_ops(void);
#ifdef __cplusplus
}
#endif

#ifndef MACQUADRA_MMU_API_ONLY   /* the core wants the declarations above only */

void m68881_mmu_ops(void) { m68ki_exception_1111(); }   /* the 030's PMMU forms: not on an 040 */

static int macquadra_cr(uint cr)
{
	switch (cr)
	{
		case 0x003: return MACQUADRA_MMU_TC;
		case 0x004: return MACQUADRA_MMU_ITT0;
		case 0x005: return MACQUADRA_MMU_ITT1;
		case 0x006: return MACQUADRA_MMU_DTT0;
		case 0x007: return MACQUADRA_MMU_DTT1;
		case 0x805: return MACQUADRA_MMU_SR;
		case 0x806: return MACQUADRA_MMU_URP;
		case 0x807: return MACQUADRA_MMU_SRP;
	}
	return -1;
}

void macquadra_op_movec_cr(void)   /* MOVEC Rc,Rn */
{
	uint word2, *r;
	if (!FLAG_S) { m68ki_exception_privilege_violation(); return; }
	word2 = OPER_I_16();
	r = &REG_DA[(word2 >> 12) & 15];
	m68ki_trace_t0();
	switch (word2 & 0xfff)
	{
		case 0x000: *r = REG_SFC; return;
		case 0x001: *r = REG_DFC; return;
		case 0x002: *r = REG_CACR & 0x80008000; return;
		case 0x800: *r = REG_USP; return;
		case 0x801: *r = REG_VBR; return;
		case 0x803: *r = FLAG_M ? REG_SP : REG_MSP; return;
		case 0x804: *r = FLAG_M ? REG_ISP : REG_SP; return;
	}
	{
		const int reg = macquadra_cr(word2 & 0xfff);
		if (reg < 0) { m68ki_exception_illegal(); return; }
		*r = macquadra_mmu_get(reg);
	}
}

void macquadra_op_movec_rc(void)   /* MOVEC Rn,Rc */
{
	uint word2, v;
	if (!FLAG_S) { m68ki_exception_privilege_violation(); return; }
	word2 = OPER_I_16();
	v = REG_DA[(word2 >> 12) & 15];
	m68ki_trace_t0();
	switch (word2 & 0xfff)
	{
		case 0x000: REG_SFC = v & 7; return;
		case 0x001: REG_DFC = v & 7; return;
		case 0x002: REG_CACR = v & 0x80008000; return;   /* DE, IE */
		case 0x800: REG_USP = v; return;
		case 0x801: REG_VBR = v; return;
		case 0x803: if (!FLAG_M) REG_MSP = v; else REG_SP = v; return;
		case 0x804: if (!FLAG_M) REG_SP = v; else REG_ISP = v; return;
	}
	{
		const int reg = macquadra_cr(word2 & 0xfff);
		if (reg < 0) { m68ki_exception_illegal(); return; }
		macquadra_mmu_set(reg, v);
	}
}

void macquadra_op_cinv(void)       /* CINV, CPUSH: no caches to keep */
{
	if (!FLAG_S) { m68ki_exception_privilege_violation(); return; }
	if (((REG_IR >> 3) & 3) == 0) m68ki_exception_illegal();
}

void macquadra_op_pflush(void)     /* every form flushes the whole TLB */
{
	if (!FLAG_S) { m68ki_exception_privilege_violation(); return; }
	macquadra_mmu_flush();
}

void macquadra_op_ptest(void)
{
	if (!FLAG_S) { m68ki_exception_privilege_violation(); return; }
	macquadra_mmu_set(MACQUADRA_MMU_SR, macquadra_mmu_ptest(REG_A[REG_IR & 7], REG_DFC & 7, !(REG_IR & 0x20)));
}

static void macquadra_line(uint src, uint dst)
{
	uint v[4], i;
	src &= ~15u;
	dst &= ~15u;
	for (i = 0; i < 4; i++) v[i] = m68ki_read_32(src + i * 4);
	for (i = 0; i < 4; i++) m68ki_write_32(dst + i * 4, v[i]);
}

void macquadra_op_move16(void)
{
	const uint ir = REG_IR;
	if ((ir & 0xFFF8) == 0xF620)                         /* (Ax)+,(Ay)+ */
	{
		const uint word2 = OPER_I_16();
		const uint ax = ir & 7, ay = (word2 >> 12) & 7;
		if ((word2 & 0x8FFF) != 0x8000) { m68ki_exception_1111(); return; }
		macquadra_line(REG_A[ax], REG_A[ay]);
		REG_A[ax] += 16;
		if (ay != ax) REG_A[ay] += 16;
		return;
	}
	if ((ir & 0xFFE0) == 0xF600)                         /* with an absolute long address */
	{
		const uint abs = OPER_I_32();
		uint *ay = &REG_A[ir & 7];
		switch ((ir >> 3) & 3)
		{
			case 0: macquadra_line(*ay, abs); *ay += 16; return;   /* (Ay)+,(xxx).L */
			case 1: macquadra_line(abs, *ay); *ay += 16; return;   /* (xxx).L,(Ay)+ */
			case 2: macquadra_line(*ay, abs); return;              /* (Ay),(xxx).L */
			case 3: macquadra_line(abs, *ay); return;              /* (xxx).L,(Ay) */
		}
	}
	m68ki_exception_1111();
}

/* FSAVE and FRESTORE with the 040's frames (4 bytes + the size byte's). Others go to Musashi's. */
static int macquadra_ea(int ea, uint *addr, int pcrel)
{
	switch ((ea >> 3) & 7)
	{
		case 2: *addr = REG_A[ea & 7]; return 1;
		case 5: *addr = EA_AY_DI_32(); return 1;
		case 6: *addr = EA_AY_IX_32(); return 1;
		case 7:
			if ((ea & 7) == 0) { *addr = EA_AW_32(); return 1; }
			if ((ea & 7) == 1) { *addr = EA_AL_32(); return 1; }
			if (pcrel && (ea & 7) == 2) { *addr = EA_PCDI_32(); return 1; }
			if (pcrel && (ea & 7) == 3) { *addr = EA_PCIX_32(); return 1; }
			break;
	}
	return 0;
}

void macquadra_op_fsave(void)
{
	const int ea = REG_IR & 0x3F, reg = ea & 7, mode = (ea >> 3) & 7;
	uint addr, frame;
	if (!FLAG_S) { m68ki_exception_privilege_violation(); return; }
	if (!(REG_IR & 0x40))                                 /* FSAVE */
	{
		frame = m68ki_cpu.fpu_just_reset ? 0 : 0x41000000;   /* NULL, or IDLE */
		if (mode == 4) { REG_A[reg] -= 4; addr = REG_A[reg]; }
		else if (mode == 3 || !macquadra_ea(ea, &addr, 0)) { m68ki_exception_1111(); return; }
		m68ki_write_32(addr, frame);
		return;
	}
	/* FRESTORE */
	if (mode == 3) addr = REG_A[reg];
	else if (mode == 4 || !macquadra_ea(ea, &addr, 1)) { m68ki_exception_1111(); return; }
	frame = m68ki_read_32(addr);
	if (mode == 3) REG_A[reg] += (frame >> 24) ? 4 + ((frame >> 16) & 0xFF) : 4;
	if (frame >> 24)
		m68ki_cpu.fpu_just_reset = 0;
	else
		do_frestore_null();
}
#endif /* MACQUADRA_MMU_API_ONLY */
