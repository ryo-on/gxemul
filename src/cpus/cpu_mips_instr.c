/*
 *  Copyright (C) 2005-2006  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE   
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 *
 *
 *  $Id: cpu_mips_instr.c,v 1.64 2006-04-29 07:01:55 debug Exp $
 *
 *  MIPS instructions.
 *
 *  Individual functions should keep track of cpu->n_translated_instrs.
 *  (If no instruction was executed, then it should be decreased. If, say, 4
 *  instructions were combined into one function and executed, then it should
 *  be increased by 3.)
 */


/*
 *  invalid:  For catching bugs.
 */
X(invalid)
{
	fatal("FATAL ERROR: An internal error occured in the MIPS"
	    " dyntrans code. Please contact the author with detailed"
	    " repro steps on how to trigger this bug.\n");
	exit(1);
}


/*
 *  reserved:  Attempt to execute a reserved instruction (e.g. a 64-bit
 *             instruction on an emulated 32-bit processor).
 */
X(reserved)
{
	/*  Synchronize the PC and cause an exception:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	mips_cpu_exception(cpu, EXCEPTION_RI, 0, 0, 0, 0, 0, 0);
}


/*
 *  cpu:  Cause a CoProcessor Unusable exception.
 *
 *  arg[0] = the number of the coprocessor
 */
X(cpu)
{
	/*  Synchronize the PC and cause an exception:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	mips_cpu_exception(cpu, EXCEPTION_CPU, 0, 0, ic->arg[0], 0, 0, 0);
}


/*
 *  nop:  Do nothing.
 */
X(nop)
{
}


/*
 *  beq:  Branch if equal
 *  bne:  Branch if not equal
 *  b:  Branch (comparing a register to itself, always true)
 *
 *  arg[0] = pointer to rs
 *  arg[1] = pointer to rt
 *  arg[2] = (int32_t) relative offset from the next instruction
 */
X(beq)
{
	MODE_int_t old_pc = cpu->pc;
	MODE_uint_t rs = reg(ic->arg[0]), rt = reg(ic->arg[1]);
	int x = rs == rt;
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		if (x) {
			old_pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
			    MIPS_INSTR_ALIGNMENT_SHIFT);
			cpu->pc = old_pc + (int32_t)ic->arg[2];
			quick_pc_to_pointers(cpu);
		} else
			cpu->cd.mips.next_ic ++;
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(beq_samepage)
{
	MODE_uint_t rs = reg(ic->arg[0]), rt = reg(ic->arg[1]);
	int x = rs == rt;
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		if (x)
			cpu->cd.mips.next_ic = (struct mips_instr_call *)
			    ic->arg[2];
		else
			cpu->cd.mips.next_ic ++;
	}
	cpu->delay_slot = NOT_DELAYED;
}
X(bne)
{
	MODE_int_t old_pc = cpu->pc;
	MODE_uint_t rs = reg(ic->arg[0]), rt = reg(ic->arg[1]);
	int x = rs != rt;
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		if (x) {
			old_pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
			    MIPS_INSTR_ALIGNMENT_SHIFT);
			cpu->pc = old_pc + (int32_t)ic->arg[2];
			quick_pc_to_pointers(cpu);
		} else
			cpu->cd.mips.next_ic ++;
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(bne_samepage)
{
	MODE_uint_t rs = reg(ic->arg[0]), rt = reg(ic->arg[1]);
	int x = rs != rt;
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		if (x)
			cpu->cd.mips.next_ic = (struct mips_instr_call *)
			    ic->arg[2];
		else
			cpu->cd.mips.next_ic ++;
	}
	cpu->delay_slot = NOT_DELAYED;
}
X(b)
{
	MODE_int_t old_pc = cpu->pc;
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		old_pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
		    MIPS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc = old_pc + (int32_t)ic->arg[2];
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(b_samepage)
{
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT))
		cpu->cd.mips.next_ic = (struct mips_instr_call *) ic->arg[2];
	cpu->delay_slot = NOT_DELAYED;
}


/*
 *  beql:  Branch if equal likely
 *  bnel:  Branch if not equal likely
 *
 *  arg[0] = pointer to rs
 *  arg[1] = pointer to rt
 *  arg[2] = (int32_t) relative offset from the next instruction
 */
X(beql)
{
	MODE_int_t old_pc = cpu->pc;
	MODE_uint_t rs = reg(ic->arg[0]), rt = reg(ic->arg[1]);
	int x = rs == rt;
	cpu->delay_slot = TO_BE_DELAYED;
	if (x)
		ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		if (x) {
			old_pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
			    MIPS_INSTR_ALIGNMENT_SHIFT);
			cpu->pc = old_pc + (int32_t)ic->arg[2];
			quick_pc_to_pointers(cpu);
		} else
			cpu->cd.mips.next_ic ++;
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(beql_samepage)
{
	MODE_uint_t rs = reg(ic->arg[0]), rt = reg(ic->arg[1]);
	int x = rs == rt;
	cpu->delay_slot = TO_BE_DELAYED;
	if (x)
		ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		if (x)
			cpu->cd.mips.next_ic = (struct mips_instr_call *)
			    ic->arg[2];
		else
			cpu->cd.mips.next_ic ++;
	}
	cpu->delay_slot = NOT_DELAYED;
}
X(bnel)
{
	MODE_int_t old_pc = cpu->pc;
	MODE_uint_t rs = reg(ic->arg[0]), rt = reg(ic->arg[1]);
	int x = rs != rt;
	cpu->delay_slot = TO_BE_DELAYED;
	if (x)
		ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		if (x) {
			old_pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
			    MIPS_INSTR_ALIGNMENT_SHIFT);
			cpu->pc = old_pc + (int32_t)ic->arg[2];
			quick_pc_to_pointers(cpu);
		} else
			cpu->cd.mips.next_ic ++;
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(bnel_samepage)
{
	MODE_uint_t rs = reg(ic->arg[0]), rt = reg(ic->arg[1]);
	int x = rs != rt;
	cpu->delay_slot = TO_BE_DELAYED;
	if (x)
		ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		if (x)
			cpu->cd.mips.next_ic = (struct mips_instr_call *)
			    ic->arg[2];
		else
			cpu->cd.mips.next_ic ++;
	}
	cpu->delay_slot = NOT_DELAYED;
}


/*
 *  blez:   Branch if less than or equal
 *  blezl:  Branch if less than or equal likely
 *
 *  arg[0] = pointer to rs
 *  arg[2] = (int32_t) relative offset from the next instruction
 */
X(blez)
{
	MODE_int_t old_pc = cpu->pc;
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs <= 0);
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		if (x) {
			old_pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
			    MIPS_INSTR_ALIGNMENT_SHIFT);
			cpu->pc = old_pc + (int32_t)ic->arg[2];
			quick_pc_to_pointers(cpu);
		} else
			cpu->cd.mips.next_ic ++;
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(blez_samepage)
{
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs <= 0);
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		if (x)
			cpu->cd.mips.next_ic = (struct mips_instr_call *)
			    ic->arg[2];
		else
			cpu->cd.mips.next_ic ++;
	}
	cpu->delay_slot = NOT_DELAYED;
}
X(blezl)
{
	MODE_int_t old_pc = cpu->pc;
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs <= 0);
	cpu->delay_slot = TO_BE_DELAYED;
	if (x)
		ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		if (x) {
			old_pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
			    MIPS_INSTR_ALIGNMENT_SHIFT);
			cpu->pc = old_pc + (int32_t)ic->arg[2];
			quick_pc_to_pointers(cpu);
		} else
			cpu->cd.mips.next_ic ++;
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(blezl_samepage)
{
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs <= 0);
	cpu->delay_slot = TO_BE_DELAYED;
	if (x)
		ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		if (x)
			cpu->cd.mips.next_ic = (struct mips_instr_call *)
			    ic->arg[2];
		else
			cpu->cd.mips.next_ic ++;
	}
	cpu->delay_slot = NOT_DELAYED;
}


/*
 *  bltz:   Branch if less than
 *  bltzl:  Branch if less than likely
 *
 *  arg[0] = pointer to rs
 *  arg[2] = (int32_t) relative offset from the next instruction
 */
X(bltz)
{
	MODE_int_t old_pc = cpu->pc;
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs < 0);
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		if (x) {
			old_pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
			    MIPS_INSTR_ALIGNMENT_SHIFT);
			cpu->pc = old_pc + (int32_t)ic->arg[2];
			quick_pc_to_pointers(cpu);
		} else
			cpu->cd.mips.next_ic ++;
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(bltz_samepage)
{
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs < 0);
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		if (x)
			cpu->cd.mips.next_ic = (struct mips_instr_call *)
			    ic->arg[2];
		else
			cpu->cd.mips.next_ic ++;
	}
	cpu->delay_slot = NOT_DELAYED;
}
X(bltzl)
{
	MODE_int_t old_pc = cpu->pc;
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs < 0);
	cpu->delay_slot = TO_BE_DELAYED;
	if (x)
		ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		if (x) {
			old_pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
			    MIPS_INSTR_ALIGNMENT_SHIFT);
			cpu->pc = old_pc + (int32_t)ic->arg[2];
			quick_pc_to_pointers(cpu);
		} else
			cpu->cd.mips.next_ic ++;
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(bltzl_samepage)
{
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs < 0);
	cpu->delay_slot = TO_BE_DELAYED;
	if (x)
		ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		if (x)
			cpu->cd.mips.next_ic = (struct mips_instr_call *)
			    ic->arg[2];
		else
			cpu->cd.mips.next_ic ++;
	}
	cpu->delay_slot = NOT_DELAYED;
}


/*
 *  bgez:   Branch if greater than or equal
 *  bgezl:  Branch if greater than or equal likely
 *
 *  arg[0] = pointer to rs
 *  arg[2] = (int32_t) relative offset from the next instruction
 */
X(bgez)
{
	MODE_int_t old_pc = cpu->pc;
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs >= 0);
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		if (x) {
			old_pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
			    MIPS_INSTR_ALIGNMENT_SHIFT);
			cpu->pc = old_pc + (int32_t)ic->arg[2];
			quick_pc_to_pointers(cpu);
		} else
			cpu->cd.mips.next_ic ++;
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(bgez_samepage)
{
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs >= 0);
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		if (x)
			cpu->cd.mips.next_ic = (struct mips_instr_call *)
			    ic->arg[2];
		else
			cpu->cd.mips.next_ic ++;
	}
	cpu->delay_slot = NOT_DELAYED;
}
X(bgezl)
{
	MODE_int_t old_pc = cpu->pc;
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs >= 0);
	cpu->delay_slot = TO_BE_DELAYED;
	if (x)
		ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		if (x) {
			old_pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
			    MIPS_INSTR_ALIGNMENT_SHIFT);
			cpu->pc = old_pc + (int32_t)ic->arg[2];
			quick_pc_to_pointers(cpu);
		} else
			cpu->cd.mips.next_ic ++;
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(bgezl_samepage)
{
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs >= 0);
	cpu->delay_slot = TO_BE_DELAYED;
	if (x)
		ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		if (x)
			cpu->cd.mips.next_ic = (struct mips_instr_call *)
			    ic->arg[2];
		else
			cpu->cd.mips.next_ic ++;
	}
	cpu->delay_slot = NOT_DELAYED;
}


/*
 *  bgezal:   Branch if greater than or equal (and link)
 *  bgezall:  Branch if greater than or equal (and link) likely
 *
 *  arg[0] = pointer to rs
 *  arg[2] = (int32_t) relative offset from the next instruction
 */
X(bgezal)
{
	MODE_int_t old_pc = cpu->pc;
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs >= 0), low_pc;

	cpu->delay_slot = TO_BE_DELAYED;
	low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.mips.gpr[MIPS_GPR_RA] = cpu->pc + 8;

	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		if (x) {
			old_pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
			    MIPS_INSTR_ALIGNMENT_SHIFT);
			cpu->pc = old_pc + (int32_t)ic->arg[2];
			quick_pc_to_pointers(cpu);
		} else
			cpu->cd.mips.next_ic ++;
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(bgezal_samepage)
{
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs >= 0), low_pc;

	cpu->delay_slot = TO_BE_DELAYED;
	low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.mips.gpr[MIPS_GPR_RA] = cpu->pc + 8;

	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		if (x)
			cpu->cd.mips.next_ic = (struct mips_instr_call *)
			    ic->arg[2];
		else
			cpu->cd.mips.next_ic ++;
	}
	cpu->delay_slot = NOT_DELAYED;
}
X(bgezall)
{
	MODE_int_t old_pc = cpu->pc;
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs >= 0), low_pc;

	cpu->delay_slot = TO_BE_DELAYED;
	low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.mips.gpr[MIPS_GPR_RA] = cpu->pc + 8;

	if (x)
		ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		if (x) {
			old_pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
			    MIPS_INSTR_ALIGNMENT_SHIFT);
			cpu->pc = old_pc + (int32_t)ic->arg[2];
			quick_pc_to_pointers(cpu);
		} else
			cpu->cd.mips.next_ic ++;
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(bgezall_samepage)
{
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs >= 0), low_pc;

	cpu->delay_slot = TO_BE_DELAYED;
	low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.mips.gpr[MIPS_GPR_RA] = cpu->pc + 8;

	if (x)
		ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		if (x)
			cpu->cd.mips.next_ic = (struct mips_instr_call *)
			    ic->arg[2];
		else
			cpu->cd.mips.next_ic ++;
	}
	cpu->delay_slot = NOT_DELAYED;
}


/*
 *  bltzal:   Branch if less than zero (and link)
 *  bltzall:  Branch if less than zero (and link) likely
 *
 *  arg[0] = pointer to rs
 *  arg[2] = (int32_t) relative offset from the next instruction
 */
X(bltzal)
{
	MODE_int_t old_pc = cpu->pc;
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs < 0), low_pc;

	cpu->delay_slot = TO_BE_DELAYED;
	low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.mips.gpr[MIPS_GPR_RA] = cpu->pc + 8;

	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		if (x) {
			old_pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
			    MIPS_INSTR_ALIGNMENT_SHIFT);
			cpu->pc = old_pc + (int32_t)ic->arg[2];
			quick_pc_to_pointers(cpu);
		} else
			cpu->cd.mips.next_ic ++;
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(bltzal_samepage)
{
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs < 0), low_pc;

	cpu->delay_slot = TO_BE_DELAYED;
	low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.mips.gpr[MIPS_GPR_RA] = cpu->pc + 8;

	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		if (x)
			cpu->cd.mips.next_ic = (struct mips_instr_call *)
			    ic->arg[2];
		else
			cpu->cd.mips.next_ic ++;
	}
	cpu->delay_slot = NOT_DELAYED;
}
X(bltzall)
{
	MODE_int_t old_pc = cpu->pc;
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs < 0), low_pc;

	cpu->delay_slot = TO_BE_DELAYED;
	low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.mips.gpr[MIPS_GPR_RA] = cpu->pc + 8;

	if (x)
		ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		if (x) {
			old_pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
			    MIPS_INSTR_ALIGNMENT_SHIFT);
			cpu->pc = old_pc + (int32_t)ic->arg[2];
			quick_pc_to_pointers(cpu);
		} else
			cpu->cd.mips.next_ic ++;
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(bltzall_samepage)
{
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs < 0), low_pc;

	cpu->delay_slot = TO_BE_DELAYED;
	low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.mips.gpr[MIPS_GPR_RA] = cpu->pc + 8;

	if (x)
		ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		if (x)
			cpu->cd.mips.next_ic = (struct mips_instr_call *)
			    ic->arg[2];
		else
			cpu->cd.mips.next_ic ++;
	}
	cpu->delay_slot = NOT_DELAYED;
}


/*
 *  bgtz:   Branch if greater than zero
 *  bgtzl:  Branch if greater than zero likely
 *
 *  arg[0] = pointer to rs
 *  arg[2] = (int32_t) relative offset from the next instruction
 */
X(bgtz)
{
	MODE_int_t old_pc = cpu->pc;
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs > 0);
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		if (x) {
			old_pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
			    MIPS_INSTR_ALIGNMENT_SHIFT);
			cpu->pc = old_pc + (int32_t)ic->arg[2];
			quick_pc_to_pointers(cpu);
		} else
			cpu->cd.mips.next_ic ++;
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(bgtz_samepage)
{
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs > 0);
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		if (x)
			cpu->cd.mips.next_ic = (struct mips_instr_call *)
			    ic->arg[2];
		else
			cpu->cd.mips.next_ic ++;
	}
	cpu->delay_slot = NOT_DELAYED;
}
X(bgtzl)
{
	MODE_int_t old_pc = cpu->pc;
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs > 0);
	cpu->delay_slot = TO_BE_DELAYED;
	if (x)
		ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		if (x) {
			old_pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
			    MIPS_INSTR_ALIGNMENT_SHIFT);
			cpu->pc = old_pc + (int32_t)ic->arg[2];
			quick_pc_to_pointers(cpu);
		} else
			cpu->cd.mips.next_ic ++;
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(bgtzl_samepage)
{
	MODE_int_t rs = reg(ic->arg[0]);
	int x = (rs > 0);
	cpu->delay_slot = TO_BE_DELAYED;
	if (x)
		ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		if (x)
			cpu->cd.mips.next_ic = (struct mips_instr_call *)
			    ic->arg[2];
		else
			cpu->cd.mips.next_ic ++;
	}
	cpu->delay_slot = NOT_DELAYED;
}


/*
 *  jr, jalr: Jump to a register [and link].
 *
 *  arg[0] = ptr to rs
 *  arg[1] = ptr to rd (for jalr)
 *  arg[2] = (int32_t) relative offset of the next instruction
 */
X(jr)
{
	MODE_int_t rs = reg(ic->arg[0]);
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		cpu->pc = rs;
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(jr_ra)
{
	MODE_int_t rs = cpu->cd.mips.gpr[MIPS_GPR_RA];
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		cpu->pc = rs;
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(jr_ra_trace)
{
	MODE_int_t rs = cpu->cd.mips.gpr[MIPS_GPR_RA];
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		cpu->pc = rs;
		cpu_functioncall_trace_return(cpu);
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(jalr)
{
	MODE_int_t rs = reg(ic->arg[0]), rd;
	cpu->delay_slot = TO_BE_DELAYED;
	rd = cpu->pc & ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
	    MIPS_INSTR_ALIGNMENT_SHIFT);
	rd += (int32_t)ic->arg[2];
	reg(ic->arg[1]) = rd;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		cpu->pc = rs;
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(jalr_trace)
{
	MODE_int_t rs = reg(ic->arg[0]), rd;
	cpu->delay_slot = TO_BE_DELAYED;
	rd = cpu->pc & ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
	    MIPS_INSTR_ALIGNMENT_SHIFT);
	rd += (int32_t)ic->arg[2];
	reg(ic->arg[1]) = rd;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		cpu->pc = rs;
		cpu_functioncall_trace(cpu, cpu->pc);
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}


/*
 *  j, jal:  Jump [and link].
 *
 *  arg[0] = lowest 28 bits of new pc.
 *  arg[1] = offset from start of page to the jal instruction + 8
 */
X(j)
{
	MODE_int_t old_pc = cpu->pc;
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		old_pc &= ~0x03ffffff;
		cpu->pc = old_pc | (uint32_t)ic->arg[0];
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(jal)
{
	MODE_int_t old_pc = cpu->pc;
	cpu->delay_slot = TO_BE_DELAYED;
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)<<MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.mips.gpr[31] = cpu->pc + ic->arg[1];
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		old_pc &= ~0x03ffffff;
		cpu->pc = old_pc | (int32_t)ic->arg[0];
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(jal_trace)
{
	MODE_int_t old_pc = cpu->pc;
	cpu->delay_slot = TO_BE_DELAYED;
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)<<MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->cd.mips.gpr[31] = cpu->pc + ic->arg[1];
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		old_pc &= ~0x03ffffff;
		cpu->pc = old_pc | (int32_t)ic->arg[0];
		cpu_functioncall_trace(cpu, cpu->pc);
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}


/*
 *  cache:  Cache operation.
 */
X(cache)
{
	/*  TODO. For now, just clear the rmw bit:  */
	cpu->cd.mips.rmw = 0;

/*  TODO: fix  */
cpu->invalidate_code_translation(cpu, 0, INVALIDATE_ALL);
cpu->invalidate_translation_caches(cpu, 0, INVALIDATE_ALL);
}


/*
 *  2-register + immediate:
 *
 *  arg[0] = pointer to rs
 *  arg[1] = pointer to rt
 *  arg[2] = uint32_t immediate value
 */
X(andi) { reg(ic->arg[1]) = reg(ic->arg[0]) & (uint32_t)ic->arg[2]; }
X(ori)  { reg(ic->arg[1]) = reg(ic->arg[0]) | (uint32_t)ic->arg[2]; }
X(xori) { reg(ic->arg[1]) = reg(ic->arg[0]) ^ (uint32_t)ic->arg[2]; }


/*
 *  2-register:
 *
 *  arg[0] = ptr to rs
 *  arg[1] = ptr to rt
 */
X(div)
{
	int32_t a = reg(ic->arg[0]), b = reg(ic->arg[1]);
	int32_t res, rem;
	if (b == 0)
		res = 0, rem = 0;
	else
		res = a / b, rem = a % b;
	reg(&cpu->cd.mips.lo) = res;
	reg(&cpu->cd.mips.hi) = rem;
}
X(divu)
{
	uint32_t a = reg(ic->arg[0]), b = reg(ic->arg[1]);
	uint32_t res, rem;
	if (b == 0)
		res = 0, rem = 0;
	else
		res = a / b, rem = a % b;
	reg(&cpu->cd.mips.lo) = (int32_t)res;
	reg(&cpu->cd.mips.hi) = (int32_t)rem;
}
X(ddiv)
{
	int64_t a = reg(ic->arg[0]), b = reg(ic->arg[1]);
	int64_t res, rem;
	if (b == 0)
		res = 0, rem = 0;
	else
		res = a / b, rem = a % b;
	reg(&cpu->cd.mips.lo) = res;
	reg(&cpu->cd.mips.hi) = rem;
}
X(ddivu)
{
	uint64_t a = reg(ic->arg[0]), b = reg(ic->arg[1]);
	uint64_t res, rem;
	if (b == 0)
		res = 0, rem = 0;
	else
		res = a / b, rem = a % b;
	reg(&cpu->cd.mips.lo) = res;
	reg(&cpu->cd.mips.hi) = rem;
}
X(mult)
{
	int32_t a = reg(ic->arg[0]), b = reg(ic->arg[1]);
	int64_t res = (int64_t)a * (int64_t)b;
	reg(&cpu->cd.mips.lo) = (int32_t)res;
	reg(&cpu->cd.mips.hi) = (int32_t)(res >> 32);
}
X(mult_xx)
{
	/*  Undocumented (?) R5900 multiplication  */
	int32_t a = reg(ic->arg[0]), b = reg(ic->arg[1]);
	int64_t res = (int64_t)a * (int64_t)b;
	reg(ic->arg[2]) = res;	/*  TODO: 32-bit or 64-bit?  */
}
X(multu)
{
	uint32_t a = reg(ic->arg[0]), b = reg(ic->arg[1]);
	uint64_t res = (uint64_t)a * (uint64_t)b;
	reg(&cpu->cd.mips.lo) = (int32_t)res;
	reg(&cpu->cd.mips.hi) = (int32_t)(res >> 32);
}
X(dmult)
{
	uint64_t a = reg(ic->arg[0]), b = reg(ic->arg[1]), c = 0;
	uint64_t hi = 0, lo = 0;
	int neg = 0;
	if (a >> 63)
		neg = !neg, a = -a;
	if (b >> 63)
		neg = !neg, b = -b;
	for (; a; a >>= 1) {
		if (a & 1) {
			uint64_t old_lo = lo;
			hi += c;
			lo += b;
			if (lo < old_lo)
				hi ++;
		}
		c = (c << 1) | (b >> 63); b <<= 1;
	}
	if (neg) {
		if (lo == 0)
			hi --;
		lo --;
		hi ^= (int64_t) -1;
		lo ^= (int64_t) -1;
	}
	reg(&cpu->cd.mips.lo) = lo;
	reg(&cpu->cd.mips.hi) = hi;
}
X(dmultu)
{
	uint64_t a = reg(ic->arg[0]), b = reg(ic->arg[1]), c = 0;
	uint64_t hi = 0, lo = 0;
	for (; a; a >>= 1) {
		if (a & 1) {
			uint64_t old_lo = lo;
			hi += c;
			lo += b;
			if (lo < old_lo)
				hi ++;
		}
		c = (c << 1) | (b >> 63); b <<= 1;
	}
	reg(&cpu->cd.mips.lo) = lo;
	reg(&cpu->cd.mips.hi) = hi;
}
X(tge)
{
	MODE_int_t a = reg(ic->arg[0]), b = reg(ic->arg[1]);
	if (a >= b) {
		/*  Synch. PC and cause an exception:  */
		int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
		    / sizeof(struct mips_instr_call);
		cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
		    << MIPS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
		mips_cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
	}
}
X(tgeu)
{
	MODE_uint_t a = reg(ic->arg[0]), b = reg(ic->arg[1]);
	if (a >= b) {
		/*  Synch. PC and cause an exception:  */
		int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
		    / sizeof(struct mips_instr_call);
		cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
		    << MIPS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
		mips_cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
	}
}
X(tlt)
{
	MODE_int_t a = reg(ic->arg[0]), b = reg(ic->arg[1]);
	if (a < b) {
		/*  Synch. PC and cause an exception:  */
		int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
		    / sizeof(struct mips_instr_call);
		cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
		    << MIPS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
		mips_cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
	}
}
X(tltu)
{
	MODE_uint_t a = reg(ic->arg[0]), b = reg(ic->arg[1]);
	if (a < b) {
		/*  Synch. PC and cause an exception:  */
		int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
		    / sizeof(struct mips_instr_call);
		cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
		    << MIPS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
		mips_cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
	}
}
X(teq)
{
	MODE_uint_t a = reg(ic->arg[0]), b = reg(ic->arg[1]);
	if (a == b) {
		/*  Synch. PC and cause an exception:  */
		int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
		    / sizeof(struct mips_instr_call);
		cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
		    << MIPS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
		mips_cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
	}
}
X(tne)
{
	MODE_uint_t a = reg(ic->arg[0]), b = reg(ic->arg[1]);
	if (a != b) {
		/*  Synch. PC and cause an exception:  */
		int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
		    / sizeof(struct mips_instr_call);
		cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
		    << MIPS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
		mips_cpu_exception(cpu, EXCEPTION_TR, 0, 0, 0, 0, 0, 0);
	}
}


/*
 *  3-register arithmetic instructions:
 *
 *  arg[0] = ptr to rs
 *  arg[1] = ptr to rt
 *  arg[2] = ptr to rd
 */
X(addu) { reg(ic->arg[2]) = (int32_t)(reg(ic->arg[0]) + reg(ic->arg[1])); }
X(add)
{
	int32_t rs = reg(ic->arg[0]), rt = reg(ic->arg[1]);
	int32_t rd = rs + rt;

	if ((rs >= 0 && rt >= 0 && rd < 0) || (rs < 0 && rt < 0 && rd >= 0)) {
		/*  Synch. PC and cause an exception:  */
		int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
		    / sizeof(struct mips_instr_call);
		cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
		    << MIPS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
		mips_cpu_exception(cpu, EXCEPTION_OV, 0, 0, 0, 0, 0, 0);
	} else
		reg(ic->arg[2]) = rd;
}
X(daddu){ reg(ic->arg[2]) = reg(ic->arg[0]) + reg(ic->arg[1]); }
X(dadd)
{
	int64_t rs = reg(ic->arg[0]), rt = reg(ic->arg[1]);
	int64_t rd = rs + rt;

	if ((rs >= 0 && rt >= 0 && rd < 0) || (rs < 0 && rt < 0 && rd >= 0)) {
		/*  Synch. PC and cause an exception:  */
		int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
		    / sizeof(struct mips_instr_call);
		cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
		    << MIPS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
		mips_cpu_exception(cpu, EXCEPTION_OV, 0, 0, 0, 0, 0, 0);
	} else
		reg(ic->arg[2]) = rd;
}
X(subu) { reg(ic->arg[2]) = (int32_t)(reg(ic->arg[0]) - reg(ic->arg[1])); }
X(sub)
{
	/*  NOTE: Negating rt and using addition. TODO: Is this correct?  */
	int32_t rs = reg(ic->arg[0]), rt = - reg(ic->arg[1]);
	int32_t rd = rs + rt;

	if ((rs >= 0 && rt >= 0 && rd < 0) || (rs < 0 && rt < 0 && rd >= 0)) {
		/*  Synch. PC and cause an exception:  */
		int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
		    / sizeof(struct mips_instr_call);
		cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
		    << MIPS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
		mips_cpu_exception(cpu, EXCEPTION_OV, 0, 0, 0, 0, 0, 0);
	} else
		reg(ic->arg[2]) = rd;
}
X(dsubu){ reg(ic->arg[2]) = reg(ic->arg[0]) - reg(ic->arg[1]); }
X(dsub)
{
	/*  NOTE: Negating rt and using addition. TODO: Is this correct?  */
	int64_t rs = reg(ic->arg[0]), rt = - reg(ic->arg[1]);
	int64_t rd = rs + rt;

	if ((rs >= 0 && rt >= 0 && rd < 0) || (rs < 0 && rt < 0 && rd >= 0)) {
		/*  Synch. PC and cause an exception:  */
		int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
		    / sizeof(struct mips_instr_call);
		cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
		    << MIPS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
		mips_cpu_exception(cpu, EXCEPTION_OV, 0, 0, 0, 0, 0, 0);
	} else
		reg(ic->arg[2]) = rd;
}
X(slt) {
#ifdef MODE32
	reg(ic->arg[2]) = (int32_t)reg(ic->arg[0]) < (int32_t)reg(ic->arg[1]);
#else
	reg(ic->arg[2]) = (int64_t)reg(ic->arg[0]) < (int64_t)reg(ic->arg[1]);
#endif
}
X(sltu) {
#ifdef MODE32
	reg(ic->arg[2]) = (uint32_t)reg(ic->arg[0]) < (uint32_t)reg(ic->arg[1]);
#else
	reg(ic->arg[2]) = (uint64_t)reg(ic->arg[0]) < (uint64_t)reg(ic->arg[1]);
#endif
}
X(and) { reg(ic->arg[2]) = reg(ic->arg[0]) & reg(ic->arg[1]); }
X(or)  { reg(ic->arg[2]) = reg(ic->arg[0]) | reg(ic->arg[1]); }
X(xor) { reg(ic->arg[2]) = reg(ic->arg[0]) ^ reg(ic->arg[1]); }
X(nor) { reg(ic->arg[2]) = ~(reg(ic->arg[0]) | reg(ic->arg[1])); }
X(sll) { reg(ic->arg[2]) = (int32_t)(reg(ic->arg[0]) << ic->arg[1]); }
X(sllv){ int sa = reg(ic->arg[1]) & 31;
	 reg(ic->arg[2]) = (int32_t)(reg(ic->arg[0]) << sa); }
X(srl) { reg(ic->arg[2]) = (int32_t)((uint32_t)reg(ic->arg[0]) >> ic->arg[1]); }
X(srlv){ int sa = reg(ic->arg[1]) & 31;
	 reg(ic->arg[2]) = (int32_t)((uint32_t)reg(ic->arg[0]) >> sa); }
X(sra) { reg(ic->arg[2]) = (int32_t)((int32_t)reg(ic->arg[0]) >> ic->arg[1]); }
X(srav){ int sa = reg(ic->arg[1]) & 31;
	 reg(ic->arg[2]) = (int32_t)((int32_t)reg(ic->arg[0]) >> sa); }
X(dsll) { reg(ic->arg[2]) = (int64_t)reg(ic->arg[0]) << (int64_t)ic->arg[1]; }
X(dsllv){ int sa = reg(ic->arg[1]) & 63;
	 reg(ic->arg[2]) = reg(ic->arg[0]) << sa; }
X(dsrl) { reg(ic->arg[2]) = (int64_t)((uint64_t)reg(ic->arg[0]) >>
	(uint64_t) ic->arg[1]);}
X(dsrlv){ int sa = reg(ic->arg[1]) & 63;
	 reg(ic->arg[2]) = (uint64_t)reg(ic->arg[0]) >> sa; }
X(dsra) { reg(ic->arg[2]) = (int64_t)reg(ic->arg[0]) >> (int64_t)ic->arg[1]; }
X(dsrav){ int sa = reg(ic->arg[1]) & 63;
	 reg(ic->arg[2]) = (int64_t)reg(ic->arg[0]) >> sa; }
X(mul) { reg(ic->arg[2]) = (int32_t)
	( (int32_t)reg(ic->arg[0]) * (int32_t)reg(ic->arg[1]) ); }
X(movn) { if (reg(ic->arg[1])) reg(ic->arg[2]) = reg(ic->arg[0]); }
X(movz) { if (!reg(ic->arg[1])) reg(ic->arg[2]) = reg(ic->arg[0]); }


/*
 *  madd, maddu, msub, msubu: Multiply-and-add/subtract
 *
 *  arg[0] = ptr to rs
 *  arg[1] = ptr to rt
 *  arg[2] = ptr to rd (only used on R5900/TX79)
 */
X(madd)
{
	int64_t rs = (int32_t)reg(ic->arg[0]), rt = (int32_t)reg(ic->arg[1]);
	int64_t sum = rs * rt,
	    hilo = (cpu->cd.mips.hi << 32) | (uint32_t)(cpu->cd.mips.lo);
	hilo += sum;
	cpu->cd.mips.hi = (int32_t)(hilo>>32); cpu->cd.mips.lo = (int32_t)hilo;
}
X(madd_rd)
{
	int64_t rs = (int32_t)reg(ic->arg[0]), rt = (int32_t)reg(ic->arg[1]);
	int64_t sum = rs * rt,
	    hilo = (cpu->cd.mips.hi << 32) | (uint32_t)(cpu->cd.mips.lo);
	hilo += sum;
	cpu->cd.mips.hi = (int32_t)(hilo>>32); cpu->cd.mips.lo = (int32_t)hilo;
	reg(ic->arg[2]) = (int32_t)hilo;
}
X(msub)
{
	int64_t rs = (int32_t)reg(ic->arg[0]), rt = (int32_t)reg(ic->arg[1]);
	int64_t sum = rs * rt,
	    hilo = (cpu->cd.mips.hi << 32) | (uint32_t)(cpu->cd.mips.lo);
	hilo -= sum;
	cpu->cd.mips.hi = (int32_t)(hilo>>32); cpu->cd.mips.lo = (int32_t)hilo;
}
X(maddu)
{
	int64_t rs = (uint32_t)reg(ic->arg[0]), rt = (uint32_t)reg(ic->arg[1]);
	int64_t sum = rs * rt,
	    hilo = (cpu->cd.mips.hi << 32) | (uint32_t)(cpu->cd.mips.lo);
	hilo += sum;
	cpu->cd.mips.hi = (int32_t)(hilo>>32); cpu->cd.mips.lo = (int32_t)hilo;
}
X(maddu_rd)
{
	int64_t rs = (uint32_t)reg(ic->arg[0]), rt = (uint32_t)reg(ic->arg[1]);
	int64_t sum = rs * rt,
	    hilo = (cpu->cd.mips.hi << 32) | (uint32_t)(cpu->cd.mips.lo);
	hilo += sum;
	cpu->cd.mips.hi = (int32_t)(hilo>>32); cpu->cd.mips.lo = (int32_t)hilo;
	reg(ic->arg[2]) = (int32_t)hilo;
}
X(msubu)
{
	int64_t rs = (uint32_t)reg(ic->arg[0]), rt = (uint32_t)reg(ic->arg[1]);
	int64_t sum = rs * rt,
	    hilo = (cpu->cd.mips.hi << 32) | (uint32_t)(cpu->cd.mips.lo);
	hilo -= sum;
	cpu->cd.mips.hi = (int32_t)(hilo>>32); cpu->cd.mips.lo = (int32_t)hilo;
}


/*
 *  mov:  Move one register into another.
 *
 *  arg[0] = pointer to source
 *  arg[2] = pointer to destination
 */
X(mov)  { reg(ic->arg[2]) = reg(ic->arg[0]); }


/*
 *  clz, clo, dclz, dclo: Count leading zeroes/ones.
 *
 *  arg[0] = pointer to rs
 *  arg[1] = pointer to rd
 */
X(clz)
{
	uint32_t x = reg(ic->arg[0]);
	int count;
	for (count=0; count<32; count++) {
		if (x & 0x80000000UL)
			break;
		x <<= 1;
	}
	reg(ic->arg[1]) = count;
}
X(clo)
{
	uint32_t x = reg(ic->arg[0]);
	int count;
	for (count=0; count<32; count++) {
		if (!(x & 0x80000000UL))
			break;
		x <<= 1;
	}
	reg(ic->arg[1]) = count;
}
X(dclz)
{
	uint64_t x = reg(ic->arg[0]);
	int count;
	for (count=0; count<64; count++) {
		if (x & 0x8000000000000000ULL)
			break;
		x <<= 1;
	}
	reg(ic->arg[1]) = count;
}
X(dclo)
{
	uint64_t x = reg(ic->arg[0]);
	int count;
	for (count=0; count<64; count++) {
		if (!(x & 0x8000000000000000ULL))
			break;
		x <<= 1;
	}
	reg(ic->arg[1]) = count;
}


/*
 *  addi, daddi: Add immediate, overflow detection.
 *  addiu, daddiu: Add immediate.
 *  slti:   Set if less than immediate (signed 32-bit)
 *  sltiu:  Set if less than immediate (signed 32-bit, but unsigned compare)
 *
 *  arg[0] = pointer to rs
 *  arg[1] = pointer to rt
 *  arg[2] = (int32_t) immediate value
 */
X(addi)
{
	int32_t rs = reg(ic->arg[0]), imm = (int32_t)ic->arg[2];
	int32_t rt = rs + imm;

	if ((rs >= 0 && imm >= 0 && rt < 0) || (rs < 0 && imm < 0 && rt >= 0)) {
		/*  Synch. PC and cause an exception:  */
		int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
		    / sizeof(struct mips_instr_call);
		cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
		    << MIPS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
		mips_cpu_exception(cpu, EXCEPTION_OV, 0, 0, 0, 0, 0, 0);
	} else
		reg(ic->arg[1]) = rt;
}
X(addiu)
{
	reg(ic->arg[1]) = (int32_t)
	    ((int32_t)reg(ic->arg[0]) + (int32_t)ic->arg[2]);
}
X(daddi)
{
	int64_t rs = reg(ic->arg[0]), imm = (int32_t)ic->arg[2];
	int64_t rt = rs + imm;

	if ((rs >= 0 && imm >= 0 && rt < 0) || (rs < 0 && imm < 0 && rt >= 0)) {
		/*  Synch. PC and cause an exception:  */
		int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
		    / sizeof(struct mips_instr_call);
		cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
		    << MIPS_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
		mips_cpu_exception(cpu, EXCEPTION_OV, 0, 0, 0, 0, 0, 0);
	} else
		reg(ic->arg[1]) = rt;
}
X(daddiu)
{
	reg(ic->arg[1]) = reg(ic->arg[0]) + (int32_t)ic->arg[2];
}
X(slti)
{
	reg(ic->arg[1]) = (MODE_int_t)reg(ic->arg[0]) < (int32_t)ic->arg[2];
}
X(sltiu)
{
	reg(ic->arg[1]) = (MODE_uint_t)reg(ic->arg[0]) <
	   ((MODE_uint_t)(int32_t)ic->arg[2]);
}
X(inc)  { reg(ic->arg[1]) ++; }
X(dec)  { reg(ic->arg[1]) --; }


/*
 *  set:  Set a register to an immediate (signed) 32-bit value.
 *
 *  arg[0] = pointer to the register
 *  arg[1] = (int32_t) immediate value
 */
X(set)
{
	reg(ic->arg[0]) = (int32_t)ic->arg[1];
}


/*
 *  mfc0, dmfc0:  Move from Coprocessor 0.
 *  mtc0, dmtc0:  Move to Coprocessor 0.
 *
 *  arg[0] = pointer to GPR (rt)
 *  arg[1] = coprocessor 0 register number | (select << 5)
 *  arg[2] = relative addr of this instruction within the page
 */
X(mfc0)
{
	int rd = ic->arg[1] & 31, select = ic->arg[1] >> 5;
	uint64_t tmp;
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)<<MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc |= ic->arg[2];
	/*  TODO: cause exception if necessary  */
	coproc_register_read(cpu, cpu->cd.mips.coproc[0], rd, &tmp, select);
	reg(ic->arg[0]) = (int32_t)tmp;
}
X(mtc0)
{
	int rd = ic->arg[1] & 31, select = ic->arg[1] >> 5;
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)<<MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc |= ic->arg[2];
	/*  TODO: cause exception if necessary  */
	coproc_register_write(cpu, cpu->cd.mips.coproc[0], rd,
	    (uint64_t *)ic->arg[0], 0, select);

/*  TODO: fix/remove these!  */
cpu->invalidate_translation_caches(cpu, 0, INVALIDATE_ALL);
}
X(dmfc0)
{
	int rd = ic->arg[1] & 31, select = ic->arg[1] >> 5;
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)<<MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc |= ic->arg[2];
	/*  TODO: cause exception if necessary  */
	coproc_register_read(cpu, cpu->cd.mips.coproc[0], rd,
	    (uint64_t *)ic->arg[0], select);
}
X(dmtc0)
{
	int rd = ic->arg[1] & 31, select = ic->arg[1] >> 5;
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)<<MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc |= ic->arg[2];
	/*  TODO: cause exception if necessary  */
	coproc_register_write(cpu, cpu->cd.mips.coproc[0], rd,
	    (uint64_t *)ic->arg[0], 1, select);

/*  TODO: fix/remove these!  */
cpu->invalidate_translation_caches(cpu, 0, INVALIDATE_ALL);
}


/*
 *  cop1_slow:  Fallback to legacy cop1 code. (Slow, but it should work.)
 */
X(cop1_slow)
{
	int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)<< MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);

	coproc_function(cpu, cpu->cd.mips.coproc[1], 1, ic->arg[0], 0, 1);
}


/*
 *  syscall, break:  Synchronize the PC and cause an exception.
 */
X(syscall)
{
	int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)<< MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	mips_cpu_exception(cpu, EXCEPTION_SYS, 0, 0, 0, 0, 0, 0);
}
X(break)
{
	int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)<< MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	mips_cpu_exception(cpu, EXCEPTION_BP, 0, 0, 0, 0, 0, 0);
}


/*
 *  promemul:  PROM software emulation.
 */
X(promemul)
{
	/*  Synchronize the PC and call the correct emulation layer:  */
	MODE_int_t old_pc;
	int res, low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)<< MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	old_pc = cpu->pc;

	switch (cpu->machine->machine_type) {
	case MACHINE_PMAX:
		res = decstation_prom_emul(cpu);
		break;
	case MACHINE_PS2:
		res = playstation2_sifbios_emul(cpu);
		break;
	case MACHINE_ARC:
	case MACHINE_SGI:
		res = arcbios_emul(cpu);
		break;
	case MACHINE_EVBMIPS:
		res = yamon_emul(cpu);
		break;
	default:fatal("TODO: Unimplemented machine type for PROM magic trap\n");
		exit(1);
	}

	if (res) {
		/*  Return from the PROM call:  */
		cpu->pc = (MODE_int_t)cpu->cd.mips.gpr[MIPS_GPR_RA];
		cpu->delay_slot = NOT_DELAYED;

		if (cpu->machine->show_trace_tree)
			cpu_functioncall_trace_return(cpu);
	} else {
		/*  The PROM call blocks.  */
		cpu->n_translated_instrs += 10;
		cpu->pc = old_pc;
	}

	quick_pc_to_pointers(cpu);
}


/*
 *  tlbw: TLB write indexed and random
 *
 *  arg[0] = 1 for random, 0 for indexed
 *  arg[2] = relative addr of this instruction within the page
 */
X(tlbw)
{
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)<<MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc |= ic->arg[2];
	coproc_tlbwri(cpu, ic->arg[0]);

/*  TODO: smarter invalidate  */
cpu->invalidate_translation_caches(cpu, 0, INVALIDATE_ALL);
}


/*
 *  tlbp: TLB probe
 *  tlbr: TLB read
 *
 *  arg[2] = relative addr of this instruction within the page
 */
X(tlbp)
{
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)<<MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc |= ic->arg[2];
	coproc_tlbpr(cpu, 0);
}
X(tlbr)
{
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)<<MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc |= ic->arg[2];
	coproc_tlbpr(cpu, 1);
}


/*
 *  rfe: Return from exception handler (R2000/R3000)
 */
X(rfe)
{
	coproc_rfe(cpu);

	/*  pc to pointers should not be necessary here.  */
	/*  quick_pc_to_pointers(cpu);  */
}


/*
 *  eret: Return from exception handler
 */
X(eret)
{
	coproc_eret(cpu);
	quick_pc_to_pointers(cpu);
}


/*
 *  rdhwr: Read hardware register into gpr (MIPS32/64 rev 2).
 *
 *  arg[0] = ptr to rt (destination register)
 */
X(rdhwr_cpunum)
{
	reg(ic->arg[0]) = cpu->cpu_id;
}


#include "tmp_mips_loadstore.c"


/*
 *  Load linked / store conditional:
 *
 *  A Load-linked instruction initiates a RMW (read-modify-write) sequence.
 *  COP0_LLADDR is updated for diagnostic purposes, except for CPUs in the
 *  R10000 family.
 *
 *  A Store-conditional instruction ends the sequence.
 *
 *  arg[0] = ptr to rt
 *  arg[1] = ptr to rs
 *  arg[2] = int32_t imm
 */
X(ll)
{
	MODE_int_t addr = reg(ic->arg[1]) + (int32_t)ic->arg[2];
	int low_pc;
	uint8_t word[sizeof(uint32_t)];

	/*  Synch. PC and load using slow memory_rw():  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);

	if (addr & (sizeof(word)-1)) {
		fatal("TODO: load linked unaligned access: exception\n");
		exit(1);
	}

	if (!cpu->memory_rw(cpu, cpu->mem, addr, word,
	    sizeof(word), MEM_READ, CACHE_DATA)) {
		/*  An exception occurred.  */
		return;
	}

	cpu->cd.mips.rmw = 1;
	cpu->cd.mips.rmw_addr = addr;
	cpu->cd.mips.rmw_len = sizeof(word);
	if (cpu->cd.mips.cpu_type.exc_model != MMU10K)
		cpu->cd.mips.coproc[0]->reg[COP0_LLADDR] =
		    (addr >> 4) & 0xffffffffULL;

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		reg(ic->arg[0]) = (int32_t) (word[0] + (word[1] << 8)
		    + (word[2] << 16) + (word[3] << 24));
	else
		reg(ic->arg[0]) = (int32_t) (word[3] + (word[2] << 8)
		    + (word[1] << 16) + (word[0] << 24));
}
X(lld)
{
	MODE_int_t addr = reg(ic->arg[1]) + (int32_t)ic->arg[2];
	int low_pc;
	uint8_t word[sizeof(uint64_t)];

	/*  Synch. PC and load using slow memory_rw():  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);

	if (addr & (sizeof(word)-1)) {
		fatal("TODO: load linked unaligned access: exception\n");
		exit(1);
	}

	if (!cpu->memory_rw(cpu, cpu->mem, addr, word,
	    sizeof(word), MEM_READ, CACHE_DATA)) {
		/*  An exception occurred.  */
		return;
	}

	cpu->cd.mips.rmw = 1;
	cpu->cd.mips.rmw_addr = addr;
	cpu->cd.mips.rmw_len = sizeof(word);
	if (cpu->cd.mips.cpu_type.exc_model != MMU10K)
		cpu->cd.mips.coproc[0]->reg[COP0_LLADDR] =
		    (addr >> 4) & 0xffffffffULL;

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		reg(ic->arg[0]) = word[0] + (word[1] << 8)
		    + (word[2] << 16) + ((uint64_t)word[3] << 24) +
		    + ((uint64_t)word[4] << 32) + ((uint64_t)word[5] << 40)
		    + ((uint64_t)word[6] << 48) + ((uint64_t)word[7] << 56);
	else
		reg(ic->arg[0]) = word[7] + (word[6] << 8)
		    + (word[5] << 16) + ((uint64_t)word[4] << 24) +
		    + ((uint64_t)word[3] << 32) + ((uint64_t)word[2] << 40)
		    + ((uint64_t)word[1] << 48) + ((uint64_t)word[0] << 56);
}
X(sc)
{
	MODE_int_t addr = reg(ic->arg[1]) + (int32_t)ic->arg[2];
	uint64_t r = reg(ic->arg[0]);
	int low_pc, i;
	uint8_t word[sizeof(uint32_t)];

	/*  Synch. PC and store using slow memory_rw():  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);

	if (addr & (sizeof(word)-1)) {
		fatal("TODO: sc unaligned access: exception\n");
		exit(1);
	}

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		word[0]=r; word[1]=r>>8; word[2]=r>>16; word[3]=r>>24;
	} else {
		word[3]=r; word[2]=r>>8; word[1]=r>>16; word[0]=r>>24;
	}

	if (!cpu->memory_rw(cpu, cpu->mem, addr, word,
	    sizeof(word), MEM_WRITE, CACHE_DATA)) {
		/*  An exception occurred.  */
		return;
	}

	/*  If rmw is 0, then the store failed.  (This cache-line was written
	    to by someone else.)  */
	if (cpu->cd.mips.rmw == 0 || cpu->cd.mips.rmw_addr != addr
	    || cpu->cd.mips.rmw_len != sizeof(word)) {
		reg(ic->arg[0]) = 0;
		cpu->cd.mips.rmw = 0;
		return;
	}

	/*  We succeeded. Let's invalidate everybody else's store to this
	    cache line:  */
	for (i=0; i<cpu->machine->ncpus; i++) {
		if (cpu->machine->cpus[i]->cd.mips.rmw) {
			uint64_t yaddr = addr, xaddr = cpu->machine->cpus[i]->
			    cd.mips.rmw_addr;
			uint64_t mask = ~(cpu->machine->cpus[i]->
			    cd.mips.cache_linesize[CACHE_DATA] - 1);
			xaddr &= mask;
			yaddr &= mask;
			if (xaddr == yaddr)
				cpu->machine->cpus[i]->cd.mips.rmw = 0;
		}
	}

	reg(ic->arg[0]) = 1;
	cpu->cd.mips.rmw = 0;
}
X(scd)
{
	MODE_int_t addr = reg(ic->arg[1]) + (int32_t)ic->arg[2];
	uint64_t r = reg(ic->arg[0]);
	int low_pc, i;
	uint8_t word[sizeof(uint64_t)];

	/*  Synch. PC and store using slow memory_rw():  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);

	if (addr & (sizeof(word)-1)) {
		fatal("TODO: sc unaligned access: exception\n");
		exit(1);
	}

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		word[0]=r;     word[1]=r>>8; word[2]=r>>16; word[3]=r>>24;
		word[4]=r>>32; word[5]=r>>40; word[6]=r>>48; word[7]=r>>56;
	} else {
		word[7]=r;     word[6]=r>>8; word[5]=r>>16; word[4]=r>>24;
		word[3]=r>>32; word[2]=r>>40; word[1]=r>>48; word[0]=r>>56;
	}

	if (!cpu->memory_rw(cpu, cpu->mem, addr, word,
	    sizeof(word), MEM_WRITE, CACHE_DATA)) {
		/*  An exception occurred.  */
		return;
	}

	/*  If rmw is 0, then the store failed.  (This cache-line was written
	    to by someone else.)  */
	if (cpu->cd.mips.rmw == 0 || cpu->cd.mips.rmw_addr != addr
	    || cpu->cd.mips.rmw_len != sizeof(word)) {
		reg(ic->arg[0]) = 0;
		cpu->cd.mips.rmw = 0;
		return;
	}

	/*  We succeeded. Let's invalidate everybody else's store to this
	    cache line:  */
	for (i=0; i<cpu->machine->ncpus; i++) {
		if (cpu->machine->cpus[i]->cd.mips.rmw) {
			uint64_t yaddr = addr, xaddr = cpu->machine->cpus[i]->
			    cd.mips.rmw_addr;
			uint64_t mask = ~(cpu->machine->cpus[i]->
			    cd.mips.cache_linesize[CACHE_DATA] - 1);
			xaddr &= mask;
			yaddr &= mask;
			if (xaddr == yaddr)
				cpu->machine->cpus[i]->cd.mips.rmw = 0;
		}
	}

	reg(ic->arg[0]) = 1;
	cpu->cd.mips.rmw = 0;
}


/*
 *  lwc1, swc1:  Coprocessor 1 load/store
 *
 *  arg[0] = ptr to coprocessor register
 *  arg[1] = ptr to rs (base pointer register)
 *  arg[2] = int32_t imm
 */
X(lwc1)
{
	const int cpnr = 1;

	/*  Synch. PC and call the generic load/store function:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);

	/*  ... but first, let's see if the coprocessor is available:  */
	if (!(cpu->cd.mips.coproc[0]->
	    reg[COP0_STATUS] & ((1 << cpnr) << STATUS_CU_SHIFT)) ) {
		mips_cpu_exception(cpu, EXCEPTION_CPU, 0, 0, cpnr, 0, 0, 0);
		return;
	}

#ifdef MODE32
	mips32_loadstore
#else
	mips_loadstore
#endif
	    [ (cpu->byte_order == EMUL_LITTLE_ENDIAN? 0 : 16) + 2 * 2 + 1]
	    (cpu, ic);
}
X(swc1)
{
	const int cpnr = 1;

	/*  Synch. PC and call the generic load/store function:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);

	/*  ... but first, let's see if the coprocessor is available:  */
	if (!(cpu->cd.mips.coproc[0]->
	    reg[COP0_STATUS] & ((1 << cpnr) << STATUS_CU_SHIFT)) ) {
		mips_cpu_exception(cpu, EXCEPTION_CPU, 0, 0, cpnr, 0, 0, 0);
		return;
	}

#ifdef MODE32
	mips32_loadstore
#else
	mips_loadstore
#endif
	    [ (cpu->byte_order == EMUL_LITTLE_ENDIAN? 0 : 16) + 8 + 2 * 2]
	    (cpu, ic);
}


/*
 *  Unaligned loads/stores:
 *
 *  arg[0] = ptr to rt
 *  arg[1] = ptr to rs
 *  arg[2] = int32_t imm
 */
X(lwl)
{
	/*  Synch. PC and call the generic load/store function:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	mips_unaligned_loadstore(cpu, ic, 1 /* is_left */, sizeof(uint32_t), 0);
}
X(lwr)
{
	/*  Synch. PC and call the generic load/store function:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	mips_unaligned_loadstore(cpu, ic, 0 /* is_left */, sizeof(uint32_t), 0);
}
X(ldl)
{
	/*  Synch. PC and call the generic load/store function:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	mips_unaligned_loadstore(cpu, ic, 1 /* is_left */, sizeof(uint64_t), 0);
}
X(ldr)
{
	/*  Synch. PC and call the generic load/store function:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	mips_unaligned_loadstore(cpu, ic, 0 /* is_left */, sizeof(uint64_t), 0);
}
X(swl)
{
	/*  Synch. PC and call the generic load/store function:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	mips_unaligned_loadstore(cpu, ic, 1 /* is_left */, sizeof(uint32_t), 1);
}
X(swr)
{
	/*  Synch. PC and call the generic load/store function:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	mips_unaligned_loadstore(cpu, ic, 0 /* is_left */, sizeof(uint32_t), 1);
}
X(sdl)
{
	/*  Synch. PC and call the generic load/store function:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	mips_unaligned_loadstore(cpu, ic, 1 /* is_left */, sizeof(uint64_t), 1);
}
X(sdr)
{
	/*  Synch. PC and call the generic load/store function:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	mips_unaligned_loadstore(cpu, ic, 0 /* is_left */, sizeof(uint64_t), 1);
}


/*****************************************************************************/


/*
 *  b_samepage_addiu:
 *
 *  Combination of branch within the same page, followed by addiu.
 */
X(b_samepage_addiu)
{
	reg(ic[1].arg[1]) = (int32_t)
	    ( (int32_t)reg(ic[1].arg[0]) + (int32_t)ic[1].arg[2] );
	cpu->n_translated_instrs ++;
	cpu->cd.mips.next_ic = (struct mips_instr_call *) ic->arg[2];
}


/*
 *  b_samepage_daddiu:
 *
 *  Combination of branch within the same page, followed by daddiu.
 */
X(b_samepage_daddiu)
{
	*(uint64_t *)ic[1].arg[1] = *(uint64_t *)ic[1].arg[0] +
	    (int32_t)ic[1].arg[2];
	cpu->n_translated_instrs ++;
	cpu->cd.mips.next_ic = (struct mips_instr_call *) ic->arg[2];
}


/*****************************************************************************/


X(end_of_page)
{
	struct mips_instr_call *next_ic;

	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1) <<
	    MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (MIPS_IC_ENTRIES_PER_PAGE << MIPS_INSTR_ALIGNMENT_SHIFT);

	/*  Find the new physpage and update translation pointers:  */
	quick_pc_to_pointers(cpu);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;

	/*  Simple jump to the next page (if we are lucky):  */
	if (cpu->delay_slot == NOT_DELAYED)
		return;

	/*  Tricky situation; the delay slot is on the next virtual page:  */
	/*  fatal("[ end_of_page: delay slot across page boundary! ]\n");  */

	next_ic = cpu->cd.mips.next_ic ++;
	instr(to_be_translated)(cpu, next_ic);

	/*  The instruction in the delay slot has now executed.  */
	/*  fatal("[ end_of_page: back from executing the delay slot, %i ]\n",
	    cpu->delay_slot);  */

	/*  Find the physpage etc of the instruction in the delay slot
	    (or, if there was an exception, the exception handler):  */
	quick_pc_to_pointers(cpu);
}


X(end_of_page2)
{
	/*  Synchronize PC on the _second_ instruction on the next page:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);
	cpu->pc &= ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);

	/*  This doesn't count as an executed instruction.  */
	cpu->n_translated_instrs --;

	quick_pc_to_pointers(cpu);

	if (cpu->delay_slot == NOT_DELAYED)
		return;

	fatal("end_of_page2: fatal error, we're in a delay slot\n");
	exit(1);
}


/*****************************************************************************/


/*
 *  Combine: [Conditional] branch, followed by addiu.
 */
void COMBINE(b_addiu)(struct cpu *cpu, struct mips_instr_call *ic,
	int low_addr)
{
	int n_back = (low_addr >> MIPS_INSTR_ALIGNMENT_SHIFT)
	    & (MIPS_IC_ENTRIES_PER_PAGE - 1);

	if (n_back < 1)
		return;

	if (ic[-1].f == instr(b_samepage)) {
		ic[-1].f = instr(b_samepage_addiu);
		combined;
	}

	/*  TODO: other branches that are followed by addiu should be here  */
}


/*
 *  Combine: [Conditional] branch, followed by daddiu.
 */
void COMBINE(b_daddiu)(struct cpu *cpu, struct mips_instr_call *ic,
	int low_addr)
{
	int n_back = (low_addr >> MIPS_INSTR_ALIGNMENT_SHIFT)
	    & (MIPS_IC_ENTRIES_PER_PAGE - 1);

	if (n_back < 1)
		return;

	if (ic[-1].f == instr(b_samepage)) {
		ic[-1].f = instr(b_samepage_daddiu);
		combined;
	}

	/*  TODO: other branches that are followed by daddiu should be here  */
}


/*****************************************************************************/


/*
 *  mips_instr_to_be_translated():
 *
 *  Translate an instruction word into a mips_instr_call. ic is filled in with
 *  valid data for the translated instruction, or a "nothing" instruction if
 *  there was a translation failure. The newly translated instruction is then
 *  executed.
 */
X(to_be_translated)
{
	uint64_t addr, low_pc;
	uint32_t iword, imm;
	unsigned char *page;
	unsigned char ib[4];
	int main_opcode, rt, rs, rd, sa, s6, x64 = 0;
	int in_crosspage_delayslot = 0;
	int delay_slot_danger = 1;
	void (*samepage_function)(struct cpu *, struct mips_instr_call *);
	int store, signedness, size;

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.mips.cur_ic_page)
	    / sizeof(struct mips_instr_call);

	/*  Special case for branch with delayslot on the next page:  */
	if (cpu->delay_slot == TO_BE_DELAYED && low_pc == 0) {
		/*  fatal("[ delay-slot translation across page "
		    "boundary ]\n");  */
		in_crosspage_delayslot = 1;
	}

	addr = cpu->pc & ~((MIPS_IC_ENTRIES_PER_PAGE-1)
	    << MIPS_INSTR_ALIGNMENT_SHIFT);
	addr += (low_pc << MIPS_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = (MODE_int_t)addr;
	addr &= ~((1 << MIPS_INSTR_ALIGNMENT_SHIFT) - 1);

	/*  Read the instruction word from memory:  */
#ifdef MODE32
	page = cpu->cd.mips.host_load[(uint32_t)addr >> 12];
#else
	{
		const uint32_t mask1 = (1 << DYNTRANS_L1N) - 1;
		const uint32_t mask2 = (1 << DYNTRANS_L2N) - 1;
		const uint32_t mask3 = (1 << DYNTRANS_L3N) - 1;
		uint32_t x1 = (addr >> (64-DYNTRANS_L1N)) & mask1;
		uint32_t x2 = (addr >> (64-DYNTRANS_L1N-DYNTRANS_L2N)) & mask2;
		uint32_t x3 = (addr >> (64-DYNTRANS_L1N-DYNTRANS_L2N-
		    DYNTRANS_L3N)) & mask3;
		struct DYNTRANS_L2_64_TABLE *l2 = cpu->cd.mips.l1_64[x1];
		struct DYNTRANS_L3_64_TABLE *l3 = l2->l3[x2];
		page = l3->host_load[x3];
	}
#endif

	if (page != NULL) {
		/*  fatal("TRANSLATION HIT!\n");  */
		memcpy(ib, page + (addr & 0xffc), sizeof(ib));
	} else {
		/*  fatal("TRANSLATION MISS!\n");  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, ib,
		    sizeof(ib), MEM_READ, CACHE_INSTRUCTION)) {
			fatal("to_be_translated(): "
			    "read failed: TODO\n");
			goto bad;
		}
	}

	iword = *((uint32_t *)&ib[0]);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		iword = LE32_TO_HOST(iword);
	else
		iword = BE32_TO_HOST(iword);

	/*  Is the instruction in the delay slot known to be safe?  */
	if ((addr & 0xffc) < 0xffc) {
		/*  TODO: check the instruction  */
		delay_slot_danger = 0;
	}


#define DYNTRANS_TO_BE_TRANSLATED_HEAD
#include "cpu_dyntrans.c"
#undef  DYNTRANS_TO_BE_TRANSLATED_HEAD


	/*
	 *  Translate the instruction:
 	 *
	 *  NOTE: _NEVER_ allow writes to the zero register; all such
	 *  instructions should be made into NOPs.
	 */

	main_opcode = iword >> 26;
	rs = (iword >> 21) & 31;
	rt = (iword >> 16) & 31;
	rd = (iword >> 11) & 31;
	sa = (iword >>  6) & 31;
	imm = (int16_t)iword;
	s6 = iword & 63;

	switch (main_opcode) {

	case HI6_SPECIAL:
		switch (s6) {

		case SPECIAL_SLL:
		case SPECIAL_SLLV:
		case SPECIAL_SRL:
		case SPECIAL_SRLV:
		case SPECIAL_SRA:
		case SPECIAL_SRAV:
		case SPECIAL_DSRL:
		case SPECIAL_DSRLV:
		case SPECIAL_DSRL32:
		case SPECIAL_DSLL:
		case SPECIAL_DSLLV:
		case SPECIAL_DSLL32:
		case SPECIAL_DSRA:
		case SPECIAL_DSRAV:
		case SPECIAL_DSRA32:
			switch (s6) {
			case SPECIAL_SLL:  ic->f = instr(sll); break;
			case SPECIAL_SLLV: ic->f = instr(sllv); sa = -1; break;
			case SPECIAL_SRL:  ic->f = instr(srl); break;
			case SPECIAL_SRLV: ic->f = instr(srlv); sa = -1; break;
			case SPECIAL_SRA:  ic->f = instr(sra); break;
			case SPECIAL_SRAV: ic->f = instr(srav); sa = -1; break;
			case SPECIAL_DSRL: ic->f = instr(dsrl); x64=1; break;
			case SPECIAL_DSRLV:ic->f = instr(dsrlv);
					   x64 = 1; sa = -1; break;
			case SPECIAL_DSRL32:ic->f= instr(dsrl); x64=1;
					   sa += 32; break;
			case SPECIAL_DSLL: ic->f = instr(dsll); x64=1; break;
			case SPECIAL_DSLLV:ic->f = instr(dsllv);
					   x64 = 1; sa = -1; break;
			case SPECIAL_DSLL32:ic->f= instr(dsll); x64=1;
					   sa += 32; break;
			case SPECIAL_DSRA: ic->f = instr(dsra); x64=1; break;
			case SPECIAL_DSRAV:ic->f = instr(dsrav);
					   x64 = 1; sa = -1; break;
			case SPECIAL_DSRA32:ic->f = instr(dsra); x64=1;
					   sa += 32; break;
			}
			ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rt];
			if (sa >= 0)
				ic->arg[1] = sa;
			else
				ic->arg[1] = (size_t)&cpu->cd.mips.gpr[rs];
			ic->arg[2] = (size_t)&cpu->cd.mips.gpr[rd];
			if (rd == MIPS_GPR_ZERO)
				ic->f = instr(nop);
			break;

		case SPECIAL_ADD:
		case SPECIAL_ADDU:
		case SPECIAL_SUB:
		case SPECIAL_SUBU:
		case SPECIAL_DADD:
		case SPECIAL_DADDU:
		case SPECIAL_DSUB:
		case SPECIAL_DSUBU:
		case SPECIAL_SLT:
		case SPECIAL_SLTU:
		case SPECIAL_AND:
		case SPECIAL_OR:
		case SPECIAL_XOR:
		case SPECIAL_NOR:
		case SPECIAL_MOVN:
		case SPECIAL_MOVZ:
		case SPECIAL_MFHI:
		case SPECIAL_MFLO:
		case SPECIAL_MTHI:
		case SPECIAL_MTLO:
		case SPECIAL_DIV:
		case SPECIAL_DIVU:
		case SPECIAL_DDIV:
		case SPECIAL_DDIVU:
		case SPECIAL_MULT:
		case SPECIAL_MULTU:
		case SPECIAL_DMULT:
		case SPECIAL_DMULTU:
		case SPECIAL_TGE:
		case SPECIAL_TGEU:
		case SPECIAL_TLT:
		case SPECIAL_TLTU:
		case SPECIAL_TEQ:
		case SPECIAL_TNE:
			switch (s6) {
			case SPECIAL_ADD:   ic->f = instr(add); break;
			case SPECIAL_ADDU:  ic->f = instr(addu); break;
			case SPECIAL_SUB:   ic->f = instr(sub); break;
			case SPECIAL_SUBU:  ic->f = instr(subu); break;
			case SPECIAL_DADD:  ic->f = instr(dadd); x64=1; break;
			case SPECIAL_DADDU: ic->f = instr(daddu); x64=1; break;
			case SPECIAL_DSUB:  ic->f = instr(dsub); x64=1; break;
			case SPECIAL_DSUBU: ic->f = instr(dsubu); x64=1; break;
			case SPECIAL_SLT:   ic->f = instr(slt); break;
			case SPECIAL_SLTU:  ic->f = instr(sltu); break;
			case SPECIAL_AND:   ic->f = instr(and); break;
			case SPECIAL_OR:    ic->f = instr(or); break;
			case SPECIAL_XOR:   ic->f = instr(xor); break;
			case SPECIAL_NOR:   ic->f = instr(nor); break;
			case SPECIAL_MFHI:  ic->f = instr(mov); break;
			case SPECIAL_MFLO:  ic->f = instr(mov); break;
			case SPECIAL_MTHI:  ic->f = instr(mov); break;
			case SPECIAL_MTLO:  ic->f = instr(mov); break;
			case SPECIAL_DIV:   ic->f = instr(div); break;
			case SPECIAL_DIVU:  ic->f = instr(divu); break;
			case SPECIAL_DDIV:  ic->f = instr(ddiv); x64=1; break;
			case SPECIAL_DDIVU: ic->f = instr(ddivu); x64=1; break;
			case SPECIAL_MULT : ic->f = instr(mult); break;
			case SPECIAL_MULTU: ic->f = instr(multu); break;
			case SPECIAL_DMULT: ic->f = instr(dmult); x64=1; break;
			case SPECIAL_DMULTU:ic->f = instr(dmultu); x64=1; break;
			case SPECIAL_TGE:   ic->f = instr(tge); break;
			case SPECIAL_TGEU:  ic->f = instr(tgeu); break;
			case SPECIAL_TLT:   ic->f = instr(tlt); break;
			case SPECIAL_TLTU:  ic->f = instr(tltu); break;
			case SPECIAL_TEQ:   ic->f = instr(teq); break;
			case SPECIAL_TNE:   ic->f = instr(tne); break;
			case SPECIAL_MOVN:  ic->f = instr(movn); break;
			case SPECIAL_MOVZ:  ic->f = instr(movz); break;
			}
			ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rs];
			ic->arg[1] = (size_t)&cpu->cd.mips.gpr[rt];
			ic->arg[2] = (size_t)&cpu->cd.mips.gpr[rd];
			switch (s6) {
			case SPECIAL_MFHI:
				ic->arg[0] = (size_t)&cpu->cd.mips.hi;
				break;
			case SPECIAL_MFLO:
				ic->arg[0] = (size_t)&cpu->cd.mips.lo;
				break;
			case SPECIAL_MTHI:
				ic->arg[2] = (size_t)&cpu->cd.mips.hi;
				break;
			case SPECIAL_MTLO:
				ic->arg[2] = (size_t)&cpu->cd.mips.lo;
				break;
			}
			/*  Special cases for rd:  */
			switch (s6) {
			case SPECIAL_MTHI:
			case SPECIAL_MTLO:
			case SPECIAL_DIV:
			case SPECIAL_DIVU:
			case SPECIAL_DDIV:
			case SPECIAL_DDIVU:
			case SPECIAL_MULT:
			case SPECIAL_MULTU:
			case SPECIAL_DMULT:
			case SPECIAL_DMULTU:
				if (s6 == SPECIAL_MULT && rd != MIPS_GPR_ZERO) {
					ic->f = instr(mult_xx);
					break;
				}
				if (rd != MIPS_GPR_ZERO) {
					fatal("TODO: rd NON-zero\n");
					goto bad;
				}
				/*  These instructions don't use rd.  */
				break;
			default:if (rd == MIPS_GPR_ZERO)
					ic->f = instr(nop);
			}
			break;

		case SPECIAL_JR:
		case SPECIAL_JALR:
			ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rs];
			ic->arg[1] = (size_t)&cpu->cd.mips.gpr[rd];
			if (s6 == SPECIAL_JALR && rd == MIPS_GPR_ZERO)
				s6 = SPECIAL_JR;
			ic->arg[2] = (addr & 0xffc) + 8;
			switch (s6) {
			case SPECIAL_JR:
				if (rs == MIPS_GPR_RA) {
					if (cpu->machine->show_trace_tree)
						ic->f = instr(jr_ra_trace);
					else
						ic->f = instr(jr_ra);
				} else {
					ic->f = instr(jr);
				}
				break;
			case SPECIAL_JALR:
				if (cpu->machine->show_trace_tree)
					ic->f = instr(jalr_trace);
				else
					ic->f = instr(jalr);
				break;
			}
			if (in_crosspage_delayslot) {
				fatal("[ WARNING: branch in delay slot? ]\n");
				ic->f = instr(nop);
			}
			break;

		case SPECIAL_SYSCALL:
			if (((iword >> 6) & 0xfffff) == 0x30378) {
				/*  "Magic trap" for PROM emulation:  */
				ic->f = instr(promemul);
			} else {
				ic->f = instr(syscall);
			}
			break;

		case SPECIAL_BREAK:
			ic->f = instr(break);
			break;

		case SPECIAL_SYNC:
			ic->f = instr(nop);
			break;

		default:goto bad;
		}
		break;

	case HI6_BEQ:
	case HI6_BNE:
	case HI6_BEQL:
	case HI6_BNEL:
	case HI6_BLEZ:
	case HI6_BLEZL:
	case HI6_BGTZ:
	case HI6_BGTZL:
		samepage_function = NULL;  /*  get rid of a compiler warning  */
		switch (main_opcode) {
		case HI6_BEQ:
			ic->f = instr(beq);
			samepage_function = instr(beq_samepage);
			/*  Special case: comparing a register with itself:  */
			if (rs == rt) {
				ic->f = instr(b);
				samepage_function = instr(b_samepage);
			}
			break;
		case HI6_BNE:
			ic->f = instr(bne);
			samepage_function = instr(bne_samepage);
			break;
		case HI6_BEQL:
			ic->f = instr(beql);
			samepage_function = instr(beql_samepage);
			/*  Special case: comparing a register with itself:  */
			if (rs == rt) {
				ic->f = instr(b);
				samepage_function = instr(b_samepage);
			}
			break;
		case HI6_BNEL:
			ic->f = instr(bnel);
			samepage_function = instr(bnel_samepage);
			break;
		case HI6_BLEZ:
			ic->f = instr(blez);
			samepage_function = instr(blez_samepage);
			break;
		case HI6_BLEZL:
			ic->f = instr(blezl);
			samepage_function = instr(blezl_samepage);
			break;
		case HI6_BGTZ:
			ic->f = instr(bgtz);
			samepage_function = instr(bgtz_samepage);
			break;
		case HI6_BGTZL:
			ic->f = instr(bgtzl);
			samepage_function = instr(bgtzl_samepage);
			break;
		}
		ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rs];
		ic->arg[1] = (size_t)&cpu->cd.mips.gpr[rt];
		ic->arg[2] = (imm << MIPS_INSTR_ALIGNMENT_SHIFT)
		    + (addr & 0xffc) + 4;
		/*  Is the offset from the start of the current page still
		    within the same page? Then use the samepage_function:  */
		if ((uint32_t)ic->arg[2] < ((MIPS_IC_ENTRIES_PER_PAGE - 1)
		    << MIPS_INSTR_ALIGNMENT_SHIFT) && (addr & 0xffc) < 0xffc) {
			ic->arg[2] = (size_t) (cpu->cd.mips.cur_ic_page +
			    ((ic->arg[2] >> MIPS_INSTR_ALIGNMENT_SHIFT)
			    & (MIPS_IC_ENTRIES_PER_PAGE - 1)));
			ic->f = samepage_function;
		}
		if (in_crosspage_delayslot) {
			fatal("TODO: branch in delay slot?\n");
			goto bad;
		}
		break;

	case HI6_ADDI:
	case HI6_ADDIU:
	case HI6_SLTI:
	case HI6_SLTIU:
	case HI6_DADDI:
	case HI6_DADDIU:
	case HI6_ANDI:
	case HI6_ORI:
	case HI6_XORI:
		ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rs];
		ic->arg[1] = (size_t)&cpu->cd.mips.gpr[rt];
		if (main_opcode == HI6_ADDI ||
		    main_opcode == HI6_ADDIU ||
		    main_opcode == HI6_SLTI ||
		    main_opcode == HI6_SLTIU ||
		    main_opcode == HI6_DADDI ||
		    main_opcode == HI6_DADDIU)
			ic->arg[2] = (int16_t)iword;
		else
			ic->arg[2] = (uint16_t)iword;

		switch (main_opcode) {
		case HI6_ADDI:    ic->f = instr(addi); break;
		case HI6_ADDIU:   ic->f = instr(addiu); break;
		case HI6_SLTI:    ic->f = instr(slti); break;
		case HI6_SLTIU:   ic->f = instr(sltiu); break;
		case HI6_DADDI:   ic->f = instr(daddi); x64 = 1; break;
		case HI6_DADDIU:  ic->f = instr(daddiu); x64 = 1; break;
		case HI6_ANDI:    ic->f = instr(andi); break;
		case HI6_ORI:     ic->f = instr(ori); break;
		case HI6_XORI:    ic->f = instr(xori); break;
		}

		if ((main_opcode == HI6_ADDIU && cpu->is_32bit) ||
		    main_opcode == HI6_DADDIU) {
			if (rs == rt && ic->arg[2] == 1)
				ic->f = instr(inc);
			if (rs == rt && ic->arg[2] == (size_t) -1)
				ic->f = instr(dec);
		}

		if (rt == MIPS_GPR_ZERO)
			ic->f = instr(nop);

		if (ic->f == instr(addiu))
			cpu->cd.mips.combination_check = COMBINE(b_addiu);
		if (ic->f == instr(daddiu))
			cpu->cd.mips.combination_check = COMBINE(b_daddiu);
		break;

	case HI6_LUI:
		ic->f = instr(set);
		ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rt];
		ic->arg[1] = imm << 16;
		if (rt == MIPS_GPR_ZERO)
			ic->f = instr(nop);
		break;

	case HI6_J:
	case HI6_JAL:
		switch (main_opcode) {
		case HI6_J:
			ic->f = instr(j);
			break;
		case HI6_JAL:
			if (cpu->machine->show_trace_tree)
				ic->f = instr(jal_trace);
			else
				ic->f = instr(jal);
			break;
		}
		ic->arg[0] = (iword & 0x03ffffff) << 2;
		ic->arg[1] = (addr & 0xffc) + 8;
		if (in_crosspage_delayslot) {
			fatal("TODO: branch in delay slot?\n");
			goto bad;
		}
		break;

	case HI6_COP0:
		if ((iword >> 25) & 1) {
			ic->arg[2] = addr & 0xffc;
			switch (iword & 0xff) {
			case COP0_TLBR:
				ic->f = instr(tlbr);
				break;
			case COP0_TLBWI:
			case COP0_TLBWR:
				ic->f = instr(tlbw);
				ic->arg[0] = (iword & 0xff) == COP0_TLBWR;
				break;
			case COP0_TLBP:
				ic->f = instr(tlbp);
				break;
			case COP0_RFE:
				ic->f = instr(rfe);
				break;
			case COP0_ERET:
				ic->f = instr(eret);
				break;
			case COP0_IDLE:
			case COP0_STANDBY:
			case COP0_SUSPEND:
			case COP0_HIBERNATE:
				/*  TODO  */
				ic->f = instr(nop);
				break;
			default:fatal("UNIMPLEMENTED cop0 (func 0x%02x)\n",
				    iword & 0xff);
				goto bad;
			}
			break;
		}

		/*  rs contains the coprocessor opcode!  */
		switch (rs) {
		case COPz_MFCz:
		case COPz_DMFCz:
			ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rt];
			ic->arg[1] = rd + ((iword & 7) << 5);
			ic->arg[2] = addr & 0xffc;
			ic->f = rs == COPz_MFCz? instr(mfc0) : instr(dmfc0);
			if (rt == MIPS_GPR_ZERO)
				ic->f = instr(nop);
			break;
		case COPz_MTCz:
		case COPz_DMTCz:
			ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rt];
			ic->arg[1] = rd + ((iword & 7) << 5);
			ic->arg[2] = addr & 0xffc;
			ic->f = rs == COPz_MTCz? instr(mtc0) : instr(dmtc0);
			break;
		case 8:	if (iword == 0x4100ffff) {
				/*  R2020 DECstation write-loop thingy.  */
				ic->f = instr(nop);
			} else {
				fatal("Unimplemented blah blah zzzz...\n");
				goto bad;
			}
			break;
		
		default:fatal("UNIMPLEMENTED cop0 (rs = %i)\n", rs);
			goto bad;
		}
		break;

	case HI6_COP1:
		/*  Fallback to slow pre-dyntrans code, for now.  */
		/*  TODO: Fix/optimize/rewrite.  */
		ic->f = instr(cop1_slow);
		ic->arg[0] = (uint32_t)iword & ((1 << 26) - 1);

		/*  Cause a coprocessor unusable exception if
		    there is no floating point coprocessor:  */
		if (cpu->cd.mips.coproc[1] == NULL) {
			ic->f = instr(cpu);
			ic->arg[0] = 1;
		}
		break;

	case HI6_SPECIAL2:
		if (cpu->cd.mips.cpu_type.rev == MIPS_R5900) {
			/*  R5900, TX79/C790, have MMI instead of SPECIAL2:  */
			switch (s6) {

			case MMI_MADD:
				ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rs];
				ic->arg[1] = (size_t)&cpu->cd.mips.gpr[rt];
				ic->arg[2] = (size_t)&cpu->cd.mips.gpr[rd];
				if (rd == MIPS_GPR_ZERO)
					ic->f = instr(madd);
				else
					ic->f = instr(madd_rd);
				break;

			case MMI_MADDU:
				ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rs];
				ic->arg[1] = (size_t)&cpu->cd.mips.gpr[rt];
				ic->arg[2] = (size_t)&cpu->cd.mips.gpr[rd];
				if (rd == MIPS_GPR_ZERO)
					ic->f = instr(maddu);
				else
					ic->f = instr(maddu_rd);
				break;

			default:goto bad;
			}
			break;
		}

		/*  TODO: is this correct? Or are there other non-MIPS32/64
		    MIPS processors that have support for SPECIAL2 opcodes?  */
		if (cpu->cd.mips.cpu_type.isa_level < 32) {
			ic->f = instr(reserved);
			break;
		}

		/*  SPECIAL2:  */
		switch (s6) {

		case SPECIAL2_MADD:
		case SPECIAL2_MADDU:
		case SPECIAL2_MSUB:
		case SPECIAL2_MSUBU:
			ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rs];
			ic->arg[1] = (size_t)&cpu->cd.mips.gpr[rt];
			switch (s6) {
			case SPECIAL2_MADD: ic->f = instr(madd); break;
			case SPECIAL2_MADDU:ic->f = instr(maddu); break;
			case SPECIAL2_MSUB: ic->f = instr(msub); break;
			case SPECIAL2_MSUBU:ic->f = instr(msubu); break;
			}
			break;

		case SPECIAL2_MUL:
			ic->f = instr(mul);
			ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rs];
			ic->arg[1] = (size_t)&cpu->cd.mips.gpr[rt];
			ic->arg[2] = (size_t)&cpu->cd.mips.gpr[rd];
			if (rd == MIPS_GPR_ZERO)
				ic->f = instr(nop);
			break;

		case SPECIAL2_CLZ:
		case SPECIAL2_CLO:
		case SPECIAL2_DCLZ:
		case SPECIAL2_DCLO:
			switch (s6) {
			case SPECIAL2_CLZ:  ic->f = instr(clz); break;
			case SPECIAL2_CLO:  ic->f = instr(clo); break;
			case SPECIAL2_DCLZ: ic->f = instr(dclz); break;
			case SPECIAL2_DCLO: ic->f = instr(dclo); break;
			}
			ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rs];
			ic->arg[1] = (size_t)&cpu->cd.mips.gpr[rd];
			if (rd == MIPS_GPR_ZERO)
				ic->f = instr(nop);
			break;

		default:goto bad;
		}
		break;

	case HI6_REGIMM:
		switch (rt) {
		case REGIMM_BGEZ:
		case REGIMM_BGEZL:
		case REGIMM_BLTZ:
		case REGIMM_BLTZL:
		case REGIMM_BGEZAL:
		case REGIMM_BGEZALL:
		case REGIMM_BLTZAL:
		case REGIMM_BLTZALL:
			samepage_function = NULL;
			switch (rt) {
			case REGIMM_BGEZ:
				ic->f = instr(bgez);
				samepage_function = instr(bgez_samepage);
				break;
			case REGIMM_BGEZL:
				ic->f = instr(bgezl);
				samepage_function = instr(bgezl_samepage);
				break;
			case REGIMM_BLTZ:
				ic->f = instr(bltz);
				samepage_function = instr(bltz_samepage);
				break;
			case REGIMM_BLTZL:
				ic->f = instr(bltzl);
				samepage_function = instr(bltzl_samepage);
				break;
			case REGIMM_BGEZAL:
				ic->f = instr(bgezal);
				samepage_function = instr(bgezal_samepage);
				break;
			case REGIMM_BGEZALL:
				ic->f = instr(bgezall);
				samepage_function = instr(bgezall_samepage);
				break;
			case REGIMM_BLTZAL:
				ic->f = instr(bltzal);
				samepage_function = instr(bltzal_samepage);
				break;
			case REGIMM_BLTZALL:
				ic->f = instr(bltzall);
				samepage_function = instr(bltzall_samepage);
				break;
			}
			ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rs];
			ic->arg[2] = (imm << MIPS_INSTR_ALIGNMENT_SHIFT)
			    + (addr & 0xffc) + 4;
			/*  Is the offset from the start of the current page
			    still within the same page? Then use the
			    samepage_function:  */
			if ((uint32_t)ic->arg[2] < ((MIPS_IC_ENTRIES_PER_PAGE-1)
			    << MIPS_INSTR_ALIGNMENT_SHIFT) && (addr & 0xffc)
			    < 0xffc) {
				ic->arg[2] = (size_t) (cpu->cd.mips.cur_ic_page+
				    ((ic->arg[2] >> MIPS_INSTR_ALIGNMENT_SHIFT)
				    & (MIPS_IC_ENTRIES_PER_PAGE - 1)));
				ic->f = samepage_function;
			}
			if (in_crosspage_delayslot) {
				fatal("TODO: branch in delay slot?\n");
				goto bad;
			}
			break;
		default:fatal("UNIMPLEMENTED regimm rt=%i\n", rt);
			goto bad;
		}
		break;

	case HI6_LB:
	case HI6_LBU:
	case HI6_SB:
	case HI6_LH:
	case HI6_LHU:
	case HI6_SH:
	case HI6_LW:
	case HI6_LWU:
	case HI6_SW:
	case HI6_LD:
	case HI6_SD:
		/*  TODO: LWU should probably also be x64=1?  */
		size = 2; signedness = 0; store = 0;
		switch (main_opcode) {
		case HI6_LB:  size = 0; signedness = 1; break;
		case HI6_LBU: size = 0; break;
		case HI6_LH:  size = 1; signedness = 1; break;
		case HI6_LHU: size = 1; break;
		case HI6_LW:  signedness = 1; break;
		case HI6_LWU: break;
		case HI6_LD:  size = 3; x64 = 1; break;
		case HI6_SB:  store = 1; size = 0; break;
		case HI6_SH:  store = 1; size = 1; break;
		case HI6_SW:  store = 1; break;
		case HI6_SD:  store = 1; size = 3; x64 = 1; break;
		}

		/*
		 *  NOTE/TODO: This is not very good; this is used for code
		 *  which relies on R2000/R3000 cache characteristics.
		 *  Unfortunately the code only gets translated _once_, which
		 *  could be a performance bottleneck. If profiling reveals
		 *  this to be a problem, then this must be redesigned. :-/
		 */

		if (cpu->cd.mips.cpu_type.mmu_model == MMU3K &&
		    cpu->cd.mips.coproc[0]->reg[COP0_STATUS] & 
		    MIPS1_ISOL_CACHES) {
			ic->f =
#ifdef MODE32
			    mips32_loadstore_generic
#else
			    mips_loadstore_generic
#endif
			    [ store * 8 + size * 2 + signedness];
		} else {
			ic->f =
#ifdef MODE32
			    mips32_loadstore
#else
			    mips_loadstore
#endif
			    [ (cpu->byte_order == EMUL_LITTLE_ENDIAN? 0 : 16)
			    + store * 8 + size * 2 + signedness];
		}
		ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rt];
		ic->arg[1] = (size_t)&cpu->cd.mips.gpr[rs];
		ic->arg[2] = (int32_t)imm;
		if (!store && rt == MIPS_GPR_ZERO)
			ic->f = instr(nop);
		break;

	case HI6_LL:
	case HI6_LLD:
	case HI6_SC:
	case HI6_SCD:
		store = 0;
		switch (main_opcode) {
		case HI6_LL:  ic->f = instr(ll); break;
		case HI6_LLD: ic->f = instr(lld); break;
		case HI6_SC:  ic->f = instr(sc); store = 1; break;
		case HI6_SCD: ic->f = instr(scd); store = 1; break;
		}
		ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rt];
		ic->arg[1] = (size_t)&cpu->cd.mips.gpr[rs];
		ic->arg[2] = (int32_t)imm;
		if (!store && rt == MIPS_GPR_ZERO) {
			fatal("HM... unusual load linked\n");
			goto bad;
		}
		break;

	case HI6_LWL:
	case HI6_LWR:
	case HI6_LDL:
	case HI6_LDR:
	case HI6_SWL:
	case HI6_SWR:
	case HI6_SDL:
	case HI6_SDR:
		/*  TODO: replace these with faster versions...  */
		store = 0;
		switch (main_opcode) {
		case HI6_LWL: ic->f = instr(lwl); break;
		case HI6_LWR: ic->f = instr(lwr); break;
		case HI6_LDL: ic->f = instr(ldl); break;
		case HI6_LDR: ic->f = instr(ldr); break;
		case HI6_SWL: ic->f = instr(swl); store = 1; break;
		case HI6_SWR: ic->f = instr(swr); store = 1; break;
		case HI6_SDL: ic->f = instr(sdl); store = 1; break;
		case HI6_SDR: ic->f = instr(sdr); store = 1; break;
		}
		ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rt];
		ic->arg[1] = (size_t)&cpu->cd.mips.gpr[rs];
		ic->arg[2] = (int32_t)imm;
		if (!store && rt == MIPS_GPR_ZERO)
			ic->f = instr(nop);
		break;

	case HI6_LWC1:
	case HI6_SWC1:
		ic->arg[0] = (size_t)&cpu->cd.mips.coproc[1]->reg[rt];
		ic->arg[1] = (size_t)&cpu->cd.mips.gpr[rs];
		ic->arg[2] = (int32_t)imm;
		switch (main_opcode) {
		case HI6_LWC1: ic->f = instr(lwc1); break;
		case HI6_SWC1: ic->f = instr(swc1); break;
		}
		/*  Cause a coprocessor unusable exception if
		    there is no floating point coprocessor:  */
		if (cpu->cd.mips.coproc[1] == NULL) {
			ic->f = instr(cpu);
			ic->arg[0] = 1;
		}
		break;

	case HI6_LWC3:
		if (cpu->cd.mips.cpu_type.isa_level >= 4) {
			/*  PREF (prefetch); treat as nop for now:  */
			ic->f = instr(nop);
		} else {
			fatal("TODO: lwc3 not implemented yet\n");
			goto bad;
		}
		break;

	case HI6_LQ_MDMX:
		if (cpu->cd.mips.cpu_type.rev == MIPS_R5900) {
			fatal("TODO: R5900 128-bit loads\n");
			goto bad;
		}

		fatal("TODO: MDMX\n");
		goto bad;
		/*  break  */

	case HI6_SQ_SPECIAL3:
		if (cpu->cd.mips.cpu_type.rev == MIPS_R5900) {
			fatal("TODO: R5900 128-bit stores\n");
			goto bad;
		}

		if (cpu->cd.mips.cpu_type.isa_level < 32 ||
		    cpu->cd.mips.cpu_type.isa_revision < 2) {
			ic->f = instr(reserved);
			break;
		}

		switch (s6) {

		case SPECIAL3_RDHWR:
			ic->arg[0] = (size_t)&cpu->cd.mips.gpr[rt];

			switch (rd) {

			case 0:	ic->f = instr(rdhwr_cpunum);
				if (rt == MIPS_GPR_ZERO)
					ic->f = instr(nop);
				break;

			default:fatal("unimplemented rdhwr register\n");
				goto bad;
			}
			break;

		default:goto bad;
		}
		break;

	case HI6_CACHE:
		/*  TODO: rt and op etc...  */
		ic->f = instr(cache);
		break;

	default:goto bad;
	}

#ifdef MODE32
	if (x64) {
		static int has_warned = 0;
		if (!has_warned)
			fatal("[ WARNING/NOTE: attempt to execute a 64-bit"
			    " instruction on an emulated 32-bit processor ]\n");
		has_warned = 1;
		ic->f = instr(reserved);
	}
#endif


#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}

