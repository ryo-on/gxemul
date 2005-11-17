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
 *  $Id: memory_ppc.c,v 1.8 2005-11-17 13:53:41 debug Exp $
 *
 *  Included from cpu_ppc.c.
 */


#include "bat.h"


/*
 *  ppc_bat():
 *
 *  BAT translation. Returns -1 if there was no BAT hit, >= 0 for a hit.
 */
int ppc_bat(struct cpu *cpu, uint64_t vaddr, uint64_t *return_addr, int flags)
{
	int i, n = cpu->cd.ppc.n_bats * 2;

	/*
	 *  TODO: Differentiate user mode vs supervisor mode access!
	 */

	if (cpu->cd.ppc.bits != 32) {
		fatal("TODO: ppc_bat() for non-32-bit\n");
		exit(1);
	}
	if (cpu->cd.ppc.cpu_type.flags & PPC_601) {
		fatal("TODO: ppc_bat() for PPC 601\n");
		exit(1);
	}

	for (i=0; i<n; i++) {
		uint32_t ux = i&1?
		    cpu->cd.ppc.dbat_u[i >> 1] : cpu->cd.ppc.ibat_u[i >> 1];
		uint32_t lx = i&1?
		    cpu->cd.ppc.dbat_l[i >> 1] : cpu->cd.ppc.ibat_l[i >> 1];
		uint32_t phys = lx & BAT_RPN, ebs = ux & BAT_EPI;
		uint32_t mask = ((ux & BAT_BL) << 15) | 0x1ffff;
		int pp = lx & BAT_PP;

		/*  Not valid in either supervisor or user mode? Then skip.  */
		if (!(ux & (BAT_Vs | BAT_Vu)))
			continue;

		/*  Virtual address mismatch? Then skip.  */
		if ((vaddr & ~mask) != (ebs & ~mask))
			continue;

		/*  Instruction BAT, but not instruction lookup? Then skip.  */
		if ((i&1) == 0 && !(flags & FLAG_INSTR))
			continue;

		*return_addr = (vaddr & mask) | (phys & ~mask);

		/*  pp happens to (almost) match our return values :-)  */
		if (pp == BAT_PP_RO)
			pp = 1;
		return pp;
	}

	return -1;
}


static int get_upper_and_lower_pte(struct cpu *cpu, uint64_t pteg_select,
	uint32_t *upper_pte, uint32_t *lower_pte, uint32_t vsid, int api)
{
	int i;
	uint32_t upper, lower;
	unsigned char d[8];
	for (i=0; i<8; i++) {
		cpu->memory_rw(cpu, cpu->mem, pteg_select + i*8,
		    &d[0], 8, MEM_READ, PHYSICAL | NO_EXCEPTIONS);
		upper = (d[0]<<24)+(d[1]<<16)+(d[2]<<8)+d[3];
		lower = (d[4]<<24)+(d[5]<<16)+(d[6]<<8)+d[7];

		/*  Valid PTE, and correct api and vsid?  */
		if (upper & 0x80000000 && (upper & 0x3f) == api &&
		    ((upper >> 7) & 0x00ffffff) == vsid) {
			*upper_pte = upper; *lower_pte = lower;
			return 1;
		}
	}
	return 0;
}


/*
 *  ppc_translate_address():
 *
 *  Don't call this function is userland_emul is non-NULL, or cpu is NULL.
 *
 *  Return values:
 *	0  Failure
 *	1  Success, the page is readable only
 *	2  Success, the page is read/write
 */
int ppc_translate_address(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_addr, int flags)
{
	int instr = flags & FLAG_INSTR, res;
	int writeflag = flags & FLAG_WRITEFLAG;
	uint64_t sdr1 = cpu->cd.ppc.sdr1, htaborg;

	if (cpu->cd.ppc.bits == 32)
		vaddr &= 0xffffffff;

	if ((instr && !(cpu->cd.ppc.msr & PPC_MSR_IR)) ||
	    (!instr && !(cpu->cd.ppc.msr & PPC_MSR_DR))) {
		*return_addr = vaddr;
		return 2;
	}

	/*  Try the BATs:  */
	if (cpu->cd.ppc.n_bats > 0) {
		res = ppc_bat(cpu, vaddr, return_addr, flags);
		if (res >= 0)
			return res;
	}

	/*  Hm... translation base 0 seems very unlikely.  */
	if (sdr1 == 0) {
		*return_addr = vaddr;
		return 2;
	}

	/*
	 *  Virtual page translation 
	 */
	/*  fatal("{ vaddr = 0x%llx }\n", (long long)vaddr);  */

	if (cpu->cd.ppc.bits == 32) {
		int srn = (vaddr >> 28) & 15, api = (vaddr >> 22) & 0x3f;
		uint32_t vsid = cpu->cd.ppc.sr[srn] & 0x00ffffff;
		uint32_t hash1, hash2, pteg_select, tmp;
		uint32_t upper_pte, lower_pte;

		htaborg = sdr1 & 0xffff0000UL;
		hash1 = (vsid & 0x7ffff) ^ ((vaddr >> 12) & 0xffff);
		hash2 = hash1 ^ 0x7ffff;
		tmp = (hash1 >> 10) & (sdr1 & 0x1ff);
		pteg_select = htaborg & 0xfe000000;
		pteg_select |= ((hash1 & 0x3ff) << 6);
		pteg_select |= (htaborg & 0x01ff0000) | (tmp << 16);
		res = get_upper_and_lower_pte(cpu, pteg_select,
		    &upper_pte, &lower_pte, vsid, api);
		if (res == 0) {
			tmp = (hash2 >> 10) & (sdr1 & 0x1ff);
			pteg_select = htaborg & 0xfe000000;
			pteg_select |= ((hash2 & 0x3ff) << 6);
			pteg_select |= (htaborg & 0x01ff0000) | (tmp << 16);
			res = get_upper_and_lower_pte(cpu, pteg_select,
			    &upper_pte, &lower_pte, vsid, api);
		}
		if (res) {
			*return_addr = (lower_pte & 0xfffff000)|(vaddr & 0xfff);

			if ((lower_pte & 3) != 2) {
				fatal("PPC TODO: permission bits: %x\n",
				    lower_pte);
				exit(1);
			}
			return 2;
		}
	} else {
		htaborg = sdr1 & 0xfffffffffffc0000ULL;

		fatal("TODO: ppc 64-bit translation\n");
		exit(1);
	}


	/*  Return failure:  */
	if (flags & FLAG_NOEXCEPTIONS)
		return 0;

	/*  Cause an exception:  */
	fatal("TODO: exception! sdr1=0x%llx vaddr=0x%llx pc=0x%llx\n",
	    (long long)cpu->cd.ppc.sdr1, (long long)vaddr, (long long)cpu->pc);

	ppc_exception(cpu, instr? 0x10 : (writeflag? 0x11 : 0x12));

	return 0;
}

