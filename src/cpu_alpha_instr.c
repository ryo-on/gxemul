/*
 *  Copyright (C) 2005  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_alpha_instr.c,v 1.1 2005-07-13 11:13:44 debug Exp $
 *
 *  Alpha instructions.
 *
 *  Individual functions should keep track of cpu->cd.alpha.n_translated_instrs.
 *  (If no instruction was executed, then it should be decreased. If, say, 4
 *  instructions were combined into one function and executed, then it should
 *  be increased by 3.)
 */


/*
 *  Helper definitions:
 */

#define X(n) void alpha_instr_ ## n(struct cpu *cpu, \
	struct alpha_instr_call *ic)


/*  This is for marking a physical page as containing translated or
    combined instructions, respectively:  */
#define	translated	(cpu->cd.alpha.cur_physpage->flags |= TRANSLATIONS)
#define	combined	(cpu->cd.alpha.cur_physpage->flags |= COMBINATIONS)


/*
 *  nothing:  Do nothing.
 *
 *  The difference between this function and the "nop" instruction is that
 *  this function does not increase the program counter or the number of
 *  translated instructions.  It is used to "get out" of running in translated
 *  mode.
 *
 *  IMPORTANT NOTE: Do a   cpu->cd.alpha.running_translated = 0;
 *                  before setting cpu->cd.alpha.next_ic = &nothing_call;
 */
X(nothing)
{
	cpu->cd.alpha.n_translated_instrs --;
	cpu->cd.alpha.next_ic --;
}


static struct alpha_instr_call nothing_call = { instr(nothing), {0,0,0} };


/*****************************************************************************/


/*
 *  nop:  Do nothing.
 */
X(nop)
{
}


/*
 *  b:  Branch (to a different translated page)
 *
 *  arg[0] = relative offset (as an int32_t)
 */
X(b)
{
	uint64_t low_pc;

	/*  Calculate new PC from this instruction + arg[0]  */
	low_pc = ((size_t)ic - (size_t)
	    cpu->cd.alpha.cur_ic_page) / sizeof(struct alpha_instr_call);
	cpu->pc &= ~((ALPHA_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (low_pc << 2);
	cpu->pc += (int32_t)ic->arg[0];

	/*  Find the new physical page and update the translation pointers:  */
	alpha_pc_to_pointers(cpu);
}


/*
 *  b_samepage:  Branch (to within the same translated page)
 *
 *  arg[0] = pointer to new alpha_instr_call
 */
X(b_samepage)
{
	cpu->cd.alpha.next_ic = (struct alpha_instr_call *) ic->arg[0];
}


/*
 *  clear:  Clear a register.
 *
 *  arg[0] = pointer to an uint64_t to clear.
 */
X(clear)
{
	*((uint64_t *)ic->arg[0]) = 0;
}


/*****************************************************************************/


X(end_of_page)
{
	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->pc &= ~((ALPHA_IC_ENTRIES_PER_PAGE-1) << 2);
	cpu->pc += (ALPHA_IC_ENTRIES_PER_PAGE << 2);

	/*  Find the new physical page and update the translation pointers:  */
	alpha_pc_to_pointers(cpu);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->cd.alpha.n_translated_instrs --;
}


/*****************************************************************************/


/*
 *  alpha_combine_instructions():
 *
 *  Combine two or more instructions, if possible, into a single function call.
 */
void alpha_combine_instructions(struct cpu *cpu, struct alpha_instr_call *ic,
	uint64_t addr)
{
	int n_back;
	n_back = (addr >> 2) & (ALPHA_IC_ENTRIES_PER_PAGE-1);
#if 0
	if (n_back >= 1) {
		/*  Double "mov":  */
		if (ic[-1].f == instr(mov) || ic[-1].f == instr(clear)) {
			if (ic[-1].f == instr(mov) && ic[0].f == instr(mov)) {
				ic[-1].f = instr(mov_2);
				combined;
			}
			if (ic[-1].f == instr(clear) && ic[0].f == instr(mov)) {
				ic[-1].f = instr(mov_2);
				ic[-1].arg[1] = 0;
				combined;
			}
			if (ic[-1].f == instr(mov) && ic[0].f == instr(clear)) {
				ic[-1].f = instr(mov_2);
				ic[0].arg[1] = 0;
				combined;
			}
			if (ic[-1].f == instr(clear) && ic[0].f==instr(clear)) {
				ic[-1].f = instr(mov_2);
				ic[-1].arg[1] = 0;
				ic[0].arg[1] = 0;
				combined;
			}
		}
	}
#endif
	/*  TODO: Combine forward as well  */
}


/*****************************************************************************/


/*
 *  alpha_instr_to_be_translated():
 *
 *  Translate an instruction word into an alpha_instr_call. ic is filled in with
 *  valid data for the translated instruction, or a "nothing" instruction if
 *  there was a translation failure. The newly translated instruction is then
 *  executed.
 */
X(to_be_translated)
{
	uint64_t addr, low_pc, iword, imm;
	struct alpha_vph_page *vph_p;
	unsigned char *page;
	unsigned char ib[4];
	void (*samepage_function)(struct cpu *, struct alpha_instr_call *);

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.alpha.cur_ic_page)
	    / sizeof(struct alpha_instr_call);
	addr = cpu->pc & ~((ALPHA_IC_ENTRIES_PER_PAGE-1) << 2);
	addr += (low_pc << 2);
	addr &= ~0x3;


printf("TO BE TRANSLATED todo\n");
goto bad;


	/*  Read the instruction word from memory:  */
	vph_p = cpu->cd.alpha.vph_table0[addr >> 22];
	page = vph_p->host_load[(addr >> 12) & 1023];

	if (page != NULL) {
		fatal("TRANSLATION HIT!\n");
		memcpy(ib, page + (addr & 0xffc), sizeof(ib));
	} else {
		fatal("TRANSLATION MISS!\n");
		if (!cpu->memory_rw(cpu, cpu->mem, addr, &ib[0],
		    sizeof(ib), MEM_READ, CACHE_INSTRUCTION)) {
			fatal("to_be_translated(): read failed: TODO\n");
			goto bad;
		}
	}

	iword = ib[0] + (ib[1]<<8) + (ib[2]<<16) + (ib[3]<<24);

	fatal("{ Alpha: translating pc=0x%016llx iword=0x%08x }\n",
	    (long long)addr, (int)iword);


/*  ...  */


	/*
	 *  If we end up here, then an instruction was translated. Now it is
	 *  time to check for combinations of instructions that can be
	 *  converted into a single function call.
	 */

	/*  Single-stepping doesn't work with combinations:  */
	if (!single_step && !cpu->machine->instruction_trace)
		alpha_combine_instructions(cpu, ic, addr);

	/*  ... and finally execute the translated instruction:  */
	ic->f(cpu, ic);

	return;


bad:	/*
	 *  Nothing was translated. (Unimplemented or illegal instruction.)
	 */
	quiet_mode = 0;
	fatal("to_be_translated(): TODO: unimplemented Alpha instruction:\n");
	alpha_cpu_disassemble_instr(cpu, ib, 1, 0, 0);
	cpu->running = 0;
	cpu->dead = 1;
	cpu->cd.alpha.running_translated = 0;
	ic = cpu->cd.alpha.next_ic = &nothing_call;
	cpu->cd.alpha.next_ic ++;

	/*  Execute the "nothing" instruction:  */
	ic->f(cpu, ic);
}

