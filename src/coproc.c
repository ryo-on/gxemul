/*
 *  Copyright (C) 2003,2004  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
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
 *  $Id: coproc.c,v 1.134 2004-12-30 18:38:25 debug Exp $
 *
 *  Emulation of MIPS coprocessors.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "misc.h"

#include "bintrans.h"
#include "cop0.h"
#include "cpu_types.h"
#include "emul.h"
#include "memory.h"
#include "opcodes.h"


char *cop0_names[32] = COP0_NAMES;


/*  FPU control registers:  */
#define	FPU_FCIR	0
#define	FPU_FCCR	25
#define	FPU_FCSR	31
#define	  FCSR_FCC0_SHIFT	  23
#define	  FCSR_FCC1_SHIFT	  25


/*
 *  coproc_new():
 *
 *  Create a new coprocessor object.
 */
struct coproc *coproc_new(struct cpu *cpu, int coproc_nr)
{
	struct coproc *c;
	int IB, DB, SB, IC, DC, SC;

	c = malloc(sizeof(struct coproc));
	if (c == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(c, 0, sizeof(struct coproc));
	c->coproc_nr = coproc_nr;

	if (coproc_nr == 0) {
		c->nr_of_tlbs = cpu->cpu_type.nr_of_tlb_entries;

		/*
		 *  Start with nothing in the status register. This makes sure
		 *  that we are running in kernel mode with all interrupts
		 *  disabled.
		 */
		c->reg[COP0_STATUS] = 0;

		/*  For userland emulation, enable all four coprocessors:  */
		if (cpu->emul->userland_emul)
			c->reg[COP0_STATUS] |=
			    ((uint32_t)0xf << STATUS_CU_SHIFT);

		/*  Hm. Enable coprocessors 0 and 1 even if we're not just
		    emulating userland? TODO: Think about this.  */
		if (cpu->emul->prom_emulation)
			c->reg[COP0_STATUS] |=
			    ((uint32_t)0x3 << STATUS_CU_SHIFT);

		c->reg[COP0_COMPARE] = (uint64_t) -1;

		if (!cpu->emul->prom_emulation)
			c->reg[COP0_STATUS] |= STATUS_BEV;

		/*  Note: .rev may contain the company ID as well!  */
		c->reg[COP0_PRID] =
		      (0x00 << 24)		/*  Company Options  */
		    | (0x00 << 16)		/*  Company ID       */
		    | (cpu->cpu_type.rev <<  8)	/*  Processor ID     */
		    | (cpu->cpu_type.sub)	/*  Revision         */
		    ;

		c->reg[COP0_WIRED] = 0;

		c->reg[COP0_CONFIG] =
		      (   0 << 31)	/*  config1 present  */
		    | (0x00 << 16)	/*  implementation dependant  */
		    | ((cpu->byte_order==EMUL_BIG_ENDIAN? 1 : 0) << 15)
					/*  endian mode  */
		    | (   2 << 13)	/*  0 = MIPS32,
					    1 = MIPS64 with 32-bit segments,
					    2 = MIPS64 with all segments,
					    3 = reserved  */
		    | (   0 << 10)	/*  architecture revision level,
					    0 = "Revision 1", other
					    values are reserved  */
		    | (   1 <<  7)	/*  MMU type:  0 = none,
					    1 = Standard TLB,
					    2 = Standard BAT,
					    3 = fixed mapping, 4-7=reserved  */
		    | (   0 <<  0)	/*  kseg0 coherency algorithm
					(TODO)  */
		    ;

		switch (cpu->cpu_type.rev) {
		case MIPS_R4000:	/*  according to the R4000 manual  */
		case MIPS_R4600:
			IB = cpu->emul->cache_picache_linesize - 4;
			IB = IB < 0? 0 : (IB > 1? 1 : IB);
			DB = cpu->emul->cache_pdcache_linesize - 4;
			DB = DB < 0? 0 : (DB > 1? 1 : DB);
			SB = cpu->emul->cache_secondary_linesize - 4;
			SB = SB < 0? 0 : (SB > 3? 3 : SB);
			IC = cpu->emul->cache_picache - 12;
			IC = IC < 0? 0 : (IC > 7? 7 : IC);
			DC = cpu->emul->cache_pdcache - 12;
			DC = DC < 0? 0 : (DC > 7? 7 : DC);
			SC = cpu->emul->cache_secondary? 0 : 1;
			c->reg[COP0_CONFIG] =
			      (   0 << 31)	/*  Master/Checker present bit  */
			    | (0x00 << 28)	/*  EC: system clock divisor, 0x00 = '2'  */
			    | (0x00 << 24)	/*  EP  */
			    | (  SB << 22)	/*  SB  */
			    | (0x00 << 21)	/*  SS: 0 = mixed i/d scache  */
			    | (0x00 << 20)	/*  SW  */
			    | (0x00 << 18)	/*  EW: 0=64-bit  */
			    | (  SC << 17)	/*  SC: 0=secondary cache present, 1=non-present  */
			    | (0x00 << 16)	/*  SM: (todo)  */
			    | ((cpu->byte_order==EMUL_BIG_ENDIAN? 1 : 0) << 15) 	/*  endian mode  */
			    | (0x01 << 14)	/*  ECC: 0=enabled, 1=disabled  */
			    | (0x00 << 13)	/*  EB: (todo)  */
			    | (0x00 << 12)	/*  0 (resered)  */
			    | (  IC <<  9)	/*  IC: I-cache = 2^(12+IC) bytes  (1 = 8KB, 4=64K)  */
			    | (  DC <<  6)	/*  DC: D-cache = 2^(12+DC) bytes  (1 = 8KB, 4=64K)  */
			    | (  IB <<  5)	/*  IB: I-cache line size (0=16, 1=32)  */
			    | (  DB <<  4)	/*  DB: D-cache line size (0=16, 1=32)  */
			    | (   0 <<  3)	/*  CU: todo  */
			    | (   0 <<  0)	/*  kseg0 coherency algorithm
							(TODO)  */
			    ;
			break;
		case MIPS_R5000:
		case MIPS_RM5200:	/*  rm5200 is just a wild guess  */
			/*  These are just guesses: (the comments are wrong) */
			c->reg[COP0_CONFIG] =
			      (   0 << 31)	/*  Master/Checker present bit  */
			    | (0x00 << 28)	/*  EC: system clock divisor, 0x00 = '2'  */
			    | (0x00 << 24)	/*  EP  */
			    | (0x00 << 22)	/*  SB  */
			    | (0x00 << 21)	/*  SS  */
			    | (0x00 << 20)	/*  SW  */
			    | (0x00 << 18)	/*  EW: 0=64-bit  */
			    | (0x01 << 17)	/*  SC: 0=secondary cache present, 1=non-present  */
			    | (0x00 << 16)	/*  SM: (todo)  */
			    | ((cpu->byte_order==EMUL_BIG_ENDIAN? 1 : 0) << 15) 	/*  endian mode  */
			    | (0x01 << 14)	/*  ECC: 0=enabled, 1=disabled  */
			    | (0x00 << 13)	/*  EB: (todo)  */
			    | (0x00 << 12)	/*  0 (resered)  */
			    | (   3 <<  9)	/*  IC: I-cache = 2^(12+IC) bytes  (1 = 8KB, 4=64K)  */
			    | (   3 <<  6)	/*  DC: D-cache = 2^(12+DC) bytes  (1 = 8KB, 4=64K)  */
			    | (   1 <<  5)	/*  IB: I-cache line size (0=16, 1=32)  */
			    | (   1 <<  4)	/*  DB: D-cache line size (0=16, 1=32)  */
			    | (   0 <<  3)	/*  CU: todo  */
			    | (   2 <<  0)	/*  kseg0 coherency algorithm
							(TODO)  */
			    ;
			break;
		case MIPS_R10000:
		case MIPS_R12000:
		case MIPS_R14000:
			IC = cpu->emul->cache_picache - 12;
			IC = IC < 0? 0 : (IC > 7? 7 : IC);
			DC = cpu->emul->cache_pdcache - 12;
			DC = DC < 0? 0 : (DC > 7? 7 : DC);
			SC = cpu->emul->cache_secondary - 19;
			SC = SC < 0? 0 : (SC > 7? 7 : SC);
			/*  According to the R10000 User's Manual:  */
			c->reg[COP0_CONFIG] =
			      (  IC << 29)	/*  Primary instruction cache size (3 = 32KB)  */
			    | (  DC << 26)	/*  Primary data cache size (3 = 32KB)  */
			    | (   0 << 19)	/*  SCClkDiv  */
			    | (  SC << 16)	/*  SCSize, secondary cache size. 0 = 512KB. powers of two  */
			    | (   0 << 15)	/*  MemEnd  */
			    | (   0 << 14)	/*  SCCorEn  */
			    | (   1 << 13)	/*  SCBlkSize. 0=16 words, 1=32 words  */
			    | (   0 <<  9)	/*  SysClkDiv  */
			    | (   0 <<  7)	/*  PrcReqMax  */
			    | (   0 <<  6)	/*  PrcElmReq  */
			    | (   0 <<  5)	/*  CohPrcReqTar  */
			    | (   0 <<  3)	/*  Device number  */
			    | (   2 <<  0)	/*  Cache coherency algorithm for kseg0  */
			    ;
			break;
		case MIPS_R5900:
			/*
			 *  R5900 is supposed to have the following (according to NetBSD/playstation2):
			 *	cpu0: 16KB/64B 2-way set-associative L1 Instruction cache, 48 TLB entries
			 *	cpu0: 8KB/64B 2-way set-associative write-back L1 Data cache
			 *  The following settings are just guesses: (comments are incorrect)
			 */
			c->reg[COP0_CONFIG] =
			      (   0 << 31)	/*  Master/Checker present bit  */
			    | (0x00 << 28)	/*  EC: system clock divisor, 0x00 = '2'  */
			    | (0x00 << 24)	/*  EP  */
			    | (0x00 << 22)	/*  SB  */
			    | (0x00 << 21)	/*  SS  */
			    | (0x00 << 20)	/*  SW  */
			    | (0x00 << 18)	/*  EW: 0=64-bit  */
			    | (0x01 << 17)	/*  SC: 0=secondary cache present, 1=non-present  */
			    | (0x00 << 16)	/*  SM: (todo)  */
			    | ((cpu->byte_order==EMUL_BIG_ENDIAN? 1 : 0) << 15) 	/*  endian mode  */
			    | (0x01 << 14)	/*  ECC: 0=enabled, 1=disabled  */
			    | (0x00 << 13)	/*  EB: (todo)  */
			    | (0x00 << 12)	/*  0 (resered)  */
			    | (   3 <<  9)	/*  IC: I-cache = 2^(12+IC) bytes  (1 = 8KB, 4=64K)  */
			    | (   3 <<  6)	/*  DC: D-cache = 2^(12+DC) bytes  (1 = 8KB, 4=64K)  */
			    | (   1 <<  5)	/*  IB: I-cache line size (0=16, 1=32)  */
			    | (   1 <<  4)	/*  DB: D-cache line size (0=16, 1=32)  */
			    | (   0 <<  3)	/*  CU: todo  */
			    | (   0 <<  0)	/*  kseg0 coherency algorithm
							(TODO)  */
			    ;
			break;
		case MIPS_4Kc:
		case MIPS_5Kc:
			/*  According to the MIPS64 5K User's Manual:  */
			/*  TODO: How good does this work with 4K?  */
			c->reg[COP0_CONFIG] =
			      (   (uint32_t)1 << 31)/*  Config 1 present bit  */
			    | (   0 << 20)	/*  ISD:  instruction scheduling disable (=1)  */
			    | (   0 << 17)	/*  DID:  dual issue disable  */
			    | (   0 << 16)	/*  BM:   burst mode  */
			    | ((cpu->byte_order==EMUL_BIG_ENDIAN? 1 : 0) << 15) 	/*  endian mode  */
			    | ((cpu->cpu_type.rev==MIPS_5Kc?2:1) << 13)	/*  1=32-bit only, 2=32/64  */
			    | (   0 << 10)	/*  Architecture revision  */
			    | (   1 <<  7)	/*  MMU type: 1=TLB, 3=FMT  */
			    | (   2 <<  0)	/*  kseg0 cache coherency algorithm  */
			    ;
			/*  TODO:  Config select 1: caches and such  */
			break;
		default:
			;
		}

		/*  Make sure the status register is sign-extended nicely:  */
		c->reg[COP0_STATUS] = (int64_t)(int32_t)c->reg[COP0_STATUS];
	}

	if (coproc_nr == 1) {
		int fpu_rev;
		uint64_t other_stuff = 0;

		switch (cpu->cpu_type.rev & 0xff) {
		case MIPS_R2000:	fpu_rev = MIPS_R2010;	break;
		case MIPS_R3000:	fpu_rev = MIPS_R3010;
					other_stuff |= 0x40;	/*  or 0x30? TODO  */
					break;
		case MIPS_R6000:	fpu_rev = MIPS_R6010;	break;
		case MIPS_R4000:	fpu_rev = MIPS_R4010;	break;
		case MIPS_4Kc:		/*  TODO: Is this the same as 5Kc?  */
		case MIPS_5Kc:		other_stuff = COP1_REVISION_DOUBLE | COP1_REVISION_SINGLE;
		case MIPS_R5000:
		case MIPS_RM5200:	fpu_rev = cpu->cpu_type.rev;
					other_stuff |= 0x10;	/*  or cpu->cpu_type.sub ? TODO  */
					break;
		case MIPS_R10000:	fpu_rev = MIPS_R10000;	break;
		case MIPS_R12000:	fpu_rev = 0x9;	break;
		default:		fpu_rev = MIPS_SOFT;
		}

		c->fcr[COP1_REVISION] = (fpu_rev << 8) | other_stuff;

#if 0
		/*  These are mentioned in the MIPS64 documentation:  */
		    + (1 << 16)		/*  single  */
		    + (1 << 17)		/*  double  */
		    + (1 << 18)		/*  paired-single  */
		    + (1 << 19)		/*  3d  */
#endif
	}

	return c;
}


/*
 *  coproc_tlb_set_entry():
 */
void coproc_tlb_set_entry(struct cpu *cpu, int entrynr, int size,
	uint64_t vaddr, uint64_t paddr0, uint64_t paddr1,
	int valid0, int valid1, int dirty0, int dirty1, int global, int asid,
	int cachealgo0, int cachealgo1)
{
	if (entrynr < 0 || entrynr >= cpu->coproc[0]->nr_of_tlbs) {
		printf("coproc_tlb_set_entry(): invalid entry nr: %i\n",
		    entrynr);
		exit(1);
	}

	switch (cpu->cpu_type.mmu_model) {
	case MMU3K:
		if (size != 4096) {
			printf("coproc_tlb_set_entry(): invalid pagesize "
			    "(%i) for MMU3K\n", size);
			exit(1);
		}
		cpu->coproc[0]->tlbs[entrynr].hi =
		    (vaddr & R2K3K_ENTRYHI_VPN_MASK) |
		    ((asid << R2K3K_ENTRYHI_ASID_SHIFT) & 
		    R2K3K_ENTRYHI_ASID_MASK);
		cpu->coproc[0]->tlbs[entrynr].lo0 =
		    (paddr0 & R2K3K_ENTRYLO_PFN_MASK) |
		    (cachealgo0? R2K3K_ENTRYLO_N : 0) |
		    (dirty0? R2K3K_ENTRYLO_D : 0) |
		    (valid0? R2K3K_ENTRYLO_V : 0) |
		    (global? R2K3K_ENTRYLO_G : 0);
		break;
	default:
		/*  MMU4K and MMU10K, etc:  */
		if (cpu->cpu_type.mmu_model == MMU10K)
			cpu->coproc[0]->tlbs[entrynr].hi =
			    (vaddr & ENTRYHI_VPN2_MASK_R10K) |
			    (vaddr & ENTRYHI_R_MASK) |
			    (asid & ENTRYHI_ASID) |
			    (global? TLB_G : 0);
		else
			cpu->coproc[0]->tlbs[entrynr].hi =
			    (vaddr & ENTRYHI_VPN2_MASK) |
			    (vaddr & ENTRYHI_R_MASK) |
			    (asid & ENTRYHI_ASID) |
			    (global? TLB_G : 0);
		/*  NOTE: The pagemask size is for a "dual" page:  */
		cpu->coproc[0]->tlbs[entrynr].mask = (2*size - 1) & ~0x1fff;
		cpu->coproc[0]->tlbs[entrynr].lo0 =
		    (((paddr0 >> 12) << ENTRYLO_PFN_SHIFT) &
			ENTRYLO_PFN_MASK) |
		    (dirty0? ENTRYLO_D : 0) |
		    (valid0? ENTRYLO_V : 0) |
		    (global? ENTRYLO_G : 0) |
		    ((cachealgo0 << ENTRYLO_C_SHIFT) & ENTRYLO_C_MASK);
		cpu->coproc[0]->tlbs[entrynr].lo1 =
		    (((paddr1 >> 12) << ENTRYLO_PFN_SHIFT) &
			ENTRYLO_PFN_MASK) |
		    (dirty1? ENTRYLO_D : 0) |
		    (valid1? ENTRYLO_V : 0) |
		    (global? ENTRYLO_G : 0) |
		    ((cachealgo1 << ENTRYLO_C_SHIFT) & ENTRYLO_C_MASK);
	}
}


/*
 *  update_translation_table():
 */
void update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page)
{
#ifdef BINTRANS
	if (cpu->emul->bintrans_enable) {
		int a, b;
		struct vth32_table *tbl1;
		void *p;
		uint32_t p_paddr;

		if (writeflag > 0)
			bintrans_invalidate(cpu, paddr_page);

		switch (cpu->cpu_type.mmu_model) {
		case MMU3K:
			a = (vaddr_page >> 22) & 0x3ff;
			b = (vaddr_page >> 12) & 0x3ff;
			/*  printf("vaddr = %08x, a = %03x, b = %03x\n", (int)vaddr_page,a, b);  */
			tbl1 = cpu->vaddr_to_hostaddr_table0_kernel[a];
			/*  printf("tbl1 = %p\n", tbl1);  */
			if (tbl1 == cpu->vaddr_to_hostaddr_nulltable) {
				/*  Allocate a new table1:  */
				/*  printf("ALLOCATING a new table1, 0x%08x - 0x%08x\n",
				    a << 22, (a << 22) + 0x3fffff);  */
				if (cpu->next_free_vth_table == NULL) {
					tbl1 = malloc(sizeof(struct vth32_table));
					if (tbl1 == NULL) {
						fprintf(stderr, "out of mem\n");
						exit(1);
					}
					memset(tbl1, 0, sizeof(struct vth32_table));
				} else {
					tbl1 = cpu->next_free_vth_table;
					cpu->next_free_vth_table = tbl1->next_free;
					tbl1->next_free = NULL;
				}
				cpu->vaddr_to_hostaddr_table0_kernel[a] = tbl1;
			}
			p = tbl1->haddr_entry[b];
			p_paddr = tbl1->paddr_entry[b];
			/* printf("   p = %p\n", p); */
			if (p == NULL && p_paddr == 0 && (host_page!=NULL || paddr_page!=0)) {
				tbl1->refcount ++;
				/*  printf("ADDING %08x -> %p wf=%i (refcount is now %i)\n",
				    (int)vaddr_page, host_page, writeflag, tbl1->refcount);  */
			}
			if (writeflag == -1) {
				/*  Forced downgrade to read-only:  */
				tbl1->haddr_entry[b] = (void *)
				    ((size_t)tbl1->haddr_entry[b] & ~1);
			} else if (writeflag==0 && (size_t)p & 1 && host_page != NULL) {
				/*  Don't degrade a page from writable to readonly.  */
			} else {
				if (host_page != NULL)
					tbl1->haddr_entry[b] = (void *)((size_t)host_page + (writeflag?1:0));
				else
					tbl1->haddr_entry[b] = NULL;
				tbl1->paddr_entry[b] = paddr_page;
			}
			tbl1->bintrans_chunks[b] = NULL;
			break;
		default:
			;
		}
	}
#endif
}


#ifdef BINTRANS
/*
 *  invalidate_table_entry():
 */
static void invalidate_table_entry(struct cpu *cpu, uint64_t vaddr)
{
	int a, b;
	struct vth32_table *tbl1;
	void *p;
	uint32_t p_paddr;

	switch (cpu->cpu_type.mmu_model) {
	case MMU3K:
		a = (vaddr >> 22) & 0x3ff;
		b = (vaddr >> 12) & 0x3ff;
/*
		printf("vaddr = %08x, a = %03x, b = %03x\n", (int)vaddr,a, b);
*/
		tbl1 = cpu->vaddr_to_hostaddr_table0_kernel[a];
/*		printf("tbl1 = %p\n", tbl1); */
		p = tbl1->haddr_entry[b];
		p_paddr = tbl1->paddr_entry[b];
		tbl1->bintrans_chunks[b] = NULL;
/*		printf("   p = %p\n", p);
*/		if (p != NULL || p_paddr != 0) {
/*			printf("Found a mapping, vaddr = %08x, a = %03x, b = %03x\n", (int)vaddr,a, b);
*/			tbl1->haddr_entry[b] = NULL;
			tbl1->paddr_entry[b] = 0;
			tbl1->refcount --;
			if (tbl1->refcount == 0) {
				cpu->vaddr_to_hostaddr_table0_kernel[a] =
				    cpu->vaddr_to_hostaddr_nulltable;
				/*  "free" tbl1:  */
				tbl1->next_free = cpu->next_free_vth_table;
				cpu->next_free_vth_table = tbl1;
			}
		}
		break;
	default:
		;
	}
}


/*
 *  clear_all_chunks_from_all_tables():
 */
void clear_all_chunks_from_all_tables(struct cpu *cpu)
{
	int a, b;
	struct vth32_table *tbl1;

	switch (cpu->cpu_type.mmu_model) {
	case MMU3K:
		for (a=0; a<0x400; a++) {
			tbl1 = cpu->vaddr_to_hostaddr_table0_kernel[a];
			if (tbl1 != cpu->vaddr_to_hostaddr_nulltable) {
				for (b=0; b<0x400; b++)
					tbl1->bintrans_chunks[b] = NULL;
			}
		}
		break;
	default:
		;
	}
}
#endif


/*
 *  invalidate_translation_caches_paddr():
 *
 *  Invalidate based on physical address.
 */
void invalidate_translation_caches_paddr(struct cpu *cpu, uint64_t paddr)
{
#ifdef BINTRANS
	paddr &= ~0xfff;

	if (cpu->emul->bintrans_enable) {
#if 0
		int i;
		uint64_t tlb_paddr;
		uint64_t tlb_vaddr;
		switch (cpu->cpu_type.mmu_model) {
		case MMU3K:
			for (i=0; i<64; i++) {
				tlb_paddr = cpu->coproc[0]->tlbs[i].lo0 & R2K3K_ENTRYLO_PFN_MASK;
				tlb_vaddr = cpu->coproc[0]->tlbs[i].hi & R2K3K_ENTRYHI_VPN_MASK;
				if ((cpu->coproc[0]->tlbs[i].lo0 & R2K3K_ENTRYLO_V) &&
				    tlb_paddr == paddr)
					invalidate_table_entry(cpu, tlb_vaddr);
			}

		}
#endif

		if (paddr < 0x20000000) {
			invalidate_table_entry(cpu, 0x80000000 + paddr);
			invalidate_table_entry(cpu, 0xa0000000 + paddr);
		}
	}

#if 0
	/*  TODO: Don't invalidate everything.  */
	for (i=0; i<N_BINTRANS_VADDR_TO_HOST; i++)
		cpu->bintrans_data_hostpage[i] = NULL;
#endif

#endif
}


/*
 *  invalidate_translation_caches():
 *
 *  This is necessary for every change to the TLB, and when the ASID is
 *  changed, so that for example user-space addresses are not cached when
 *  they should not be.
 */
static void invalidate_translation_caches(struct cpu *cpu,
	int all, uint64_t vaddr, int kernelspace, int old_asid_to_invalidate)
{
	int i;

/* printf("inval(all=%i,kernel=%i,addr=%016llx)\n",all,kernelspace,(long long)vaddr);
*/
#ifdef BINTRANS
	if (cpu->emul->bintrans_enable) {
		if (all) {
			int i;
			uint64_t tlb_vaddr;
			switch (cpu->cpu_type.mmu_model) {
			case MMU3K:
				for (i=0; i<64; i++) {
					tlb_vaddr = cpu->coproc[0]->tlbs[i].hi & R2K3K_ENTRYHI_VPN_MASK;
					if ((cpu->coproc[0]->tlbs[i].lo0 & R2K3K_ENTRYLO_V) &&
					    (tlb_vaddr & 0xc0000000ULL) != 0x80000000ULL) {
						int asid = (cpu->coproc[0]->tlbs[i].hi & R2K3K_ENTRYHI_ASID_MASK) >> R2K3K_ENTRYHI_ASID_SHIFT;
						if (old_asid_to_invalidate < 0 ||
						    old_asid_to_invalidate == asid)
							invalidate_table_entry(cpu, tlb_vaddr);
					}
				}
			}
		} else
			invalidate_table_entry(cpu, vaddr);
	}

	/*  TODO: Don't invalidate everything.  */
	for (i=0; i<N_BINTRANS_VADDR_TO_HOST; i++)
		cpu->bintrans_data_hostpage[i] = NULL;
#endif

	if (kernelspace)
		all = 1;

#ifdef USE_TINY_CACHE
{
	vaddr >>= 12;

	/*  Invalidate the tiny translation cache...  */
	if (!cpu->emul->bintrans_enable)
		for (i=0; i<N_TRANSLATION_CACHE_INSTR; i++)
			if (all ||
			    vaddr == (cpu->translation_cache_instr[i].vaddr_pfn))
				cpu->translation_cache_instr[i].wf = 0;

	if (!cpu->emul->bintrans_enable)
		for (i=0; i<N_TRANSLATION_CACHE_DATA; i++)
			if (all ||
			    vaddr == (cpu->translation_cache_data[i].vaddr_pfn))
				cpu->translation_cache_data[i].wf = 0;
}
#endif
}


/*
 *  coproc_register_read();
 *
 *  Read a value from a coprocessor register.
 */
void coproc_register_read(struct cpu *cpu,
	struct coproc *cp, int reg_nr, uint64_t *ptr)
{
	int unimpl = 1;

	if (cp->coproc_nr==0 && reg_nr==COP0_INDEX)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_RANDOM)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_ENTRYLO0)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_ENTRYLO1)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_CONTEXT)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_PAGEMASK)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_WIRED)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_BADVADDR)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_COUNT)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_ENTRYHI)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_COMPARE)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_STATUS)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_CAUSE)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_EPC)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_PRID)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_CONFIG)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_LLADDR)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_WATCHLO)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_WATCHHI)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_XCONTEXT)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_ERRCTL)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_CACHEERR)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_TAGDATA_LO)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_TAGDATA_HI)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_ERROREPC)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_RESERVED_22) {
		/*  Used by Linux on Linksys WRT54G  */
		unimpl = 0;
	}
	if (cp->coproc_nr==0 && reg_nr==COP0_DEBUG)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_PERFCNT)	unimpl = 0;
	if (cp->coproc_nr==0 && reg_nr==COP0_DESAVE)	unimpl = 0;

	if (cp->coproc_nr==1)	unimpl = 0;

	if (unimpl) {
		fatal("cpu%i: warning: read from unimplemented coproc%i"
		    " register %i (%s)\n", cpu->cpu_id, cp->coproc_nr, reg_nr,
		    cp->coproc_nr==0? cop0_names[reg_nr] : "?");

		cpu_exception(cpu, EXCEPTION_CPU, 0, 0, cp->coproc_nr, 0, 0, 0);
		return;
	}

	*ptr = cp->reg[reg_nr];
}


/*
 *  coproc_register_write();
 *
 *  Write a value to a coprocessor register.
 */
void coproc_register_write(struct cpu *cpu,
	struct coproc *cp, int reg_nr, uint64_t *ptr, int flag64)
{
	int unimpl = 1;
	int readonly = 0;
	uint64_t tmp = *ptr;
	uint64_t tmp2 = 0, old;
	int inval = 0, old_asid, oldmode;

	switch (cp->coproc_nr) {
	case 0:
		/*  COPROC 0:  */
		switch (reg_nr) {
		case COP0_INDEX:
		case COP0_RANDOM:
			unimpl = 0;
			break;
		case COP0_ENTRYLO0:
			unimpl = 0;
			if (cpu->cpu_type.mmu_model == MMU3K && (tmp & 0xff)!=0) {
				/*  char *symbol;
				    uint64_t offset;
				    symbol = get_symbol_name(cpu->pc_last, &offset);
				    fatal("YO! pc = 0x%08llx <%s> lo=%016llx\n", (long long)cpu->pc_last, symbol? symbol : "no symbol", (long long)tmp); */
				tmp &= (R2K3K_ENTRYLO_PFN_MASK |
				    R2K3K_ENTRYLO_N | R2K3K_ENTRYLO_D |
				    R2K3K_ENTRYLO_V | R2K3K_ENTRYLO_G);
			} else if (cpu->cpu_type.mmu_model == MMU4K) {
				tmp &= (ENTRYLO_PFN_MASK | ENTRYLO_C_MASK |
				    ENTRYLO_D | ENTRYLO_V | ENTRYLO_G);
			}
			break;
		case COP0_BADVADDR:
			/*  Hm. Irix writes to this register. (Why?)  */
			unimpl = 0;
			break;
		case COP0_ENTRYLO1:
			unimpl = 0;
			if (cpu->cpu_type.mmu_model == MMU4K) {
				tmp &= (ENTRYLO_PFN_MASK | ENTRYLO_C_MASK |
				    ENTRYLO_D | ENTRYLO_V | ENTRYLO_G);
			}
			break;
		case COP0_CONTEXT:
			old = cp->reg[COP0_CONTEXT];
			cp->reg[COP0_CONTEXT] = tmp;
			if (cpu->cpu_type.mmu_model == MMU3K) {
				cp->reg[COP0_CONTEXT] &= ~R2K3K_CONTEXT_BADVPN_MASK;
				cp->reg[COP0_CONTEXT] |= (old & R2K3K_CONTEXT_BADVPN_MASK);
			} else {
				cp->reg[COP0_CONTEXT] &= ~CONTEXT_BADVPN2_MASK;
				cp->reg[COP0_CONTEXT] |= (old & CONTEXT_BADVPN2_MASK);
			}
			return;
		case COP0_PAGEMASK:
			tmp2 = tmp >> PAGEMASK_SHIFT;
			if (tmp2 != 0x000 &&
			    tmp2 != 0x003 &&
			    tmp2 != 0x00f &&
			    tmp2 != 0x03f &&
			    tmp2 != 0x0ff &&
			    tmp2 != 0x3ff &&
			    tmp2 != 0xfff)
				fatal("cpu%i: trying to write an invalid pagemask %08lx to COP0_PAGEMASK\n",
				    cpu->cpu_id, (long)tmp2);
			unimpl = 0;
			break;
		case COP0_WIRED:
			if (cpu->cpu_type.mmu_model == MMU3K) {
				fatal("cpu%i: r2k/r3k wired register must always be 8\n", cpu->cpu_id);
				tmp = 8;
			}
			cp->reg[COP0_RANDOM] = cp->nr_of_tlbs-1;
			tmp &= INDEX_MASK;
			unimpl = 0;
			break;
		case COP0_COUNT:
			unimpl = 0;
			break;
		case COP0_COMPARE:
			/*  Clear the timer interrupt bit (bit 7):  */
			cpu_interrupt_ack(cpu, 7);
			unimpl = 0;
			break;
		case COP0_ENTRYHI:
			/*
			 *  Translation caches must be invalidated, because the
			 *  address space might change (if the ASID changes).
			 */
			switch (cpu->cpu_type.mmu_model) {
			case MMU3K:
				old_asid = (cp->reg[COP0_ENTRYHI] & R2K3K_ENTRYHI_ASID_MASK)
				    >> R2K3K_ENTRYHI_ASID_SHIFT;
				if ((cp->reg[COP0_ENTRYHI] & R2K3K_ENTRYHI_ASID_MASK) != (tmp & R2K3K_ENTRYHI_ASID_MASK))
					inval = 1;
				break;
			default:
				old_asid = cp->reg[COP0_ENTRYHI] & ENTRYHI_ASID;
				if ((cp->reg[COP0_ENTRYHI] & ENTRYHI_ASID) != (tmp & ENTRYHI_ASID))
					inval = 1;
				break;
			}
			if (inval)
				invalidate_translation_caches(cpu, 1, 0, 0, old_asid);
			unimpl = 0;
			if (cpu->cpu_type.mmu_model == MMU3K && (tmp & 0x3f)!=0) {
				/* char *symbol;
				   uint64_t offset;
				   symbol = get_symbol_name(cpu->pc_last, &offset);
				   fatal("YO! pc = 0x%08llx <%s> hi=%016llx\n", (long long)cpu->pc_last, symbol? symbol : "no symbol", (long long)tmp);  */
				tmp &= ~0x3f;
			}
			if (cpu->cpu_type.mmu_model == MMU3K)
				tmp &= (R2K3K_ENTRYHI_VPN_MASK | R2K3K_ENTRYHI_ASID_MASK);
			else if (cpu->cpu_type.mmu_model == MMU10K)
				tmp &= (ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK_R10K | ENTRYHI_ASID);
			else
				tmp &= (ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK | ENTRYHI_ASID);
			break;
		case COP0_EPC:
			unimpl = 0;
			break;
		case COP0_PRID:
			readonly = 1;
			break;
		case COP0_CONFIG:
			/*  fatal("COP0_CONFIG: modifying K0 bits: 0x%08x => ", cp->reg[reg_nr]);  */
			tmp = *ptr;
			tmp &= 0x3;	/*  only bits 2..0 can be written  */
			cp->reg[reg_nr] &= ~(0x3);  cp->reg[reg_nr] |= tmp;
			/*  fatal("0x%08x\n", cp->reg[reg_nr]);  */
			return;
		case COP0_STATUS:
			oldmode = cp->reg[COP0_STATUS];
			tmp &= ~(1 << 21);	/*  bit 21 is read-only  */
			/*  Changing from kernel to user mode? Then
			    invalidate some translation caches:  */
			if (cpu->cpu_type.mmu_model == MMU3K) {
				if (!(oldmode & MIPS1_SR_KU_CUR)
				    && (tmp & MIPS1_SR_KU_CUR))
					invalidate_translation_caches(cpu, 0, 0, 1, 0);
			} else {
				/*  TODO: don't hardcode  */
				if ((oldmode & 0xff) != (tmp & 0xff))
					invalidate_translation_caches(cpu, 0, 0, 1, 0);
			}
			unimpl = 0;
			break;
		case COP0_CAUSE:
			/*  A write to the cause register only affects IM bits 0 and 1:  */
			cp->reg[reg_nr] &= ~(0x3 << STATUS_IM_SHIFT);
			cp->reg[reg_nr] |= (tmp & (0x3 << STATUS_IM_SHIFT));
			if (!(cp->reg[COP0_CAUSE] & STATUS_IM_MASK))
		                cpu->cached_interrupt_is_possible = 0;
			else
		                cpu->cached_interrupt_is_possible = 1;
			return;
		case COP0_FRAMEMASK:
			/*  TODO: R10000  */
			unimpl = 0;
			break;
		case COP0_TAGDATA_LO:
		case COP0_TAGDATA_HI:
			/*  TODO: R4300 and others?  */
			unimpl = 0;
			break;
		case COP0_LLADDR:
			unimpl = 0;
			break;
		case COP0_WATCHLO:
		case COP0_WATCHHI:
			unimpl = 0;
			break;
		case COP0_XCONTEXT:
			/*
			 *  TODO:  According to the R10000 manual, the R4400 shares the PTEbase
			 *  portion of the context registers (that is, xcontext and context).
			 *  on R10000, they are separate registers.
			 */
			/*  debug("[ xcontext 0x%016llx ]\n", tmp);  */
			unimpl = 0;
			break;

		/*  Most of these are actually TODOs:  */
		case COP0_ERROREPC:
		case COP0_DEPC:
		case COP0_RESERVED_22:	/*  Used by Linux on Linksys WRT54G  */
		case COP0_DESAVE:
		case COP0_PERFCNT:
		case COP0_ERRCTL:	/*  R10000  */
			unimpl = 0;
			break;
		}
		break;

	case 1:
		/*  COPROC 1:  */
		unimpl = 0;
		break;
	}

	if (unimpl) {
		fatal("cpu%i: warning: write to unimplemented coproc%i "
		    "register %i (%s), data = 0x%016llx\n", cpu->cpu_id, cp->coproc_nr, reg_nr,
		    cp->coproc_nr==0? cop0_names[reg_nr] : "?", (long long)tmp);

		cpu_exception(cpu, EXCEPTION_CPU, 0, 0, cp->coproc_nr, 0, 0, 0);
		return;
	}

	if (readonly) {
		fatal("cpu%i: warning: write to READONLY coproc%i register "
		    "%i ignored\n", cpu->cpu_id, cp->coproc_nr, reg_nr);
		return;
	}

	cp->reg[reg_nr] = tmp;
}


/*
 *  MIPS floating-point stuff:
 *
 *  TODO:  Move this to some other file?
 */
#define	FMT_S		16
#define	FMT_D		17
#define	FMT_W		20
#define	FMT_L		21
#define	FMT_PS		22

#define	FPU_OP_ADD	1
#define	FPU_OP_SUB	2
#define	FPU_OP_MUL	3
#define	FPU_OP_DIV	4
#define	FPU_OP_SQRT	5
#define	FPU_OP_MOV	6
#define	FPU_OP_CVT	7
#define	FPU_OP_C	8
#define	FPU_OP_ABS	9
#define	FPU_OP_NEG	10
/*  TODO: CEIL.L, CEIL.W, FLOOR.L, FLOOR.W, RECIP, ROUND.L, ROUND.W,
 RSQRT  */


struct internal_float_value {
	double	f;
	int	nan;
};


/*
 *  fpu_interpret_float_value():
 *
 *  Interprets a float value from binary IEEE format into
 *  a internal_float_value struct.
 */
static void fpu_interpret_float_value(uint64_t reg,
	struct internal_float_value *fvp, int fmt)
{
	int n_frac = 0, n_exp = 0;
	int i, nan, sign = 0, exponent;
	double fraction;

	memset(fvp, 0, sizeof(struct internal_float_value));

	/*  n_frac and n_exp:  */
	switch (fmt) {
	case FMT_S:	n_frac = 23; n_exp = 8; break;
	case FMT_W:	n_frac = 31; n_exp = 0; break;
	case FMT_D:	n_frac = 52; n_exp = 11; break;
	case FMT_L:	n_frac = 63; n_exp = 0; break;
	default:
		fatal("fpu_interpret_float_value(): unimplemented format %i\n", fmt);
	}

	/*  exponent:  */
	exponent = 0;
	switch (fmt) {
	case FMT_W:
		reg &= 0xffffffffULL;
	case FMT_L:
		break;
	case FMT_S:
		reg &= 0xffffffffULL;
	case FMT_D:
		exponent = (reg >> n_frac) & ((1 << n_exp) - 1);
		exponent -= (1 << (n_exp-1)) - 1;
		break;
	default:
		fatal("fpu_interpret_float_value(): unimplemented format %i\n", fmt);
	}

	/*  nan:  */
	nan = 0;
	switch (fmt) {
	case FMT_S:
		if (reg == 0x7fffffffULL || reg == 0x7fbfffffULL)
			nan = 1;
		break;
	case FMT_D:
		if (reg == 0x7fffffffffffffffULL ||
		    reg == 0x7ff7ffffffffffffULL)
			nan = 1;
		break;
	}

	if (nan) {
		fvp->f = 1.0;
		goto no_reasonable_result;
	}

	/*  fraction:  */
	fraction = 0.0;
	switch (fmt) {
	case FMT_W:
		{
			int32_t r_int = reg;
			fraction = r_int;
		}
		break;
	case FMT_L:
		{
			int64_t r_int = reg;
			fraction = r_int;
		}
		break;
	case FMT_S:
	case FMT_D:
		/*  sign:  */
		sign = (reg >> 31) & 1;
		if (fmt == FMT_D)
			sign = (reg >> 63) & 1;

		fraction = 0.0;
		for (i=0; i<n_frac; i++) {
			int bit = (reg >> i) & 1;
			fraction /= 2.0;
			if (bit)
				fraction += 1.0;
		}
		/*  Add implicit bit 0:  */
		fraction = (fraction / 2.0) + 1.0;
		break;
	default:
		fatal("fpu_interpret_float_value(): unimplemented format %i\n", fmt);
	}

	/*  form the value:  */
	fvp->f = fraction;

	/*  fatal("load  reg=%016llx sign=%i exponent=%i fraction=%f ", (long long)reg, sign, exponent, fraction);  */

	/*  TODO: this is awful for exponents of large magnitude.  */
	if (exponent > 0) {
		while (exponent-- > 0)
			fvp->f *= 2.0;
	} else if (exponent < 0) {
		while (exponent++ < 0)
			fvp->f /= 2.0;
	}

	if (sign)
		fvp->f = -fvp->f;

no_reasonable_result:
	fvp->nan = nan;

	/*  fatal("f = %f\n", fvp->f);  */
}


/*
 *  fpu_store_float_value():
 *
 *  Stores a float value (actually a double) in fmt format.
 */
static void fpu_store_float_value(struct coproc *cp, int fd,
	double nf, int fmt, int nan)
{
	int n_frac = 0, n_exp = 0, signofs=0;
	int i, exponent;
	uint64_t r = 0, r2;
	int64_t r3;

	/*  n_frac and n_exp:  */
	switch (fmt) {
	case FMT_S:	n_frac = 23; n_exp = 8; signofs = 31; break;
	case FMT_W:	n_frac = 31; n_exp = 0; signofs = 31; break;
	case FMT_D:	n_frac = 52; n_exp = 11; signofs = 63; break;
	case FMT_L:	n_frac = 63; n_exp = 0; signofs = 63; break;
	default:
		fatal("fpu_store_float_value(): unimplemented format %i\n", fmt);
	}

	if ((fmt == FMT_S || fmt == FMT_D) && nan)
		goto store_nan;

	/*  fraction:  */
	switch (fmt) {
	case FMT_W:
	case FMT_L:
		/*
		 *  This causes an implicit conversion of double to integer.
		 *  If nf < 0.0, then r2 will begin with a sequence of binary
		 *  1's, which is ok.
		 */
		r3 = nf;
		r2 = r3;
		r |= r2;

		if (fmt == FMT_W)
			r &= 0xffffffffULL;
		break;
	case FMT_S:
	case FMT_D:
		/*  fatal("store f=%f ", nf);  */

		/*  sign bit:  */
		if (nf < 0.0) {
			r |= ((uint64_t)1 << signofs);
			nf = -nf;
		}

		/*
		 *  How to convert back from double to exponent + fraction:
		 *  We want fraction to be 1.xxx, that is   1.0 <= fraction < 2.0
		 *
		 *  This method is very slow but should work:
		 */
		exponent = 0;
		while (nf < 1.0 && exponent > -1023) {
			nf *= 2.0;
			exponent --;
		}
		while (nf >= 2.0 && exponent < 1023) {
			nf /= 2.0;
			exponent ++;
		}

		/*  Here:   1.0 <= nf < 2.0  */
		/*  fatal(" nf=%f", nf);  */
		nf -= 1.0;	/*  remove implicit first bit  */
		for (i=n_frac-1; i>=0; i--) {
			nf *= 2.0;
			if (nf >= 1.0) {
				r |= ((uint64_t)1 << i);
				nf -= 1.0;
			}
			/*  printf("\n i=%2i r=%016llx\n", i, (long long)r);  */
		}

		/*  Insert the exponent into the resulting word:  */
		/*  (First bias, then make sure it's within range)  */
		exponent += (((uint64_t)1 << (n_exp-1)) - 1);
		if (exponent < 0)
			exponent = 0;
		if (exponent >= ((int64_t)1 << n_exp))
			exponent = ((int64_t)1 << n_exp) - 1;
		r |= (uint64_t)exponent << n_frac;

		/*  Special case for 0.0:  */
		if (exponent == 0)
			r = 0;

		/*  fatal(" exp=%i, r = %016llx\n", exponent, (long long)r);  */

		break;
	default:
		/*  TODO  */
		fatal("fpu_store_float_value(): unimplemented format %i\n", fmt);
	}

store_nan:
	if (nan) {
		if (fmt == FMT_S)
			r = 0x7fffffffULL;
		else if (fmt == FMT_D)
			r = 0x7fffffffffffffffULL;
		else
			r = 0x7fffffffULL;
	}

	/*
	 *  TODO:  this is for 32-bit mode. It has to be updated later
	 *		for 64-bit coprocessor stuff.
	 */
	if (fmt == FMT_D || fmt == FMT_L) {
		cp->reg[fd] = r & 0xffffffffULL;
		cp->reg[(fd+1) & 31] = (r >> 32) & 0xffffffffULL;

		if (cp->reg[fd] & 0x80000000ULL)
			cp->reg[fd] |= 0xffffffff00000000ULL;
		if (cp->reg[fd+1] & 0x80000000ULL)
			cp->reg[fd+1] |= 0xffffffff00000000ULL;
	} else {
		cp->reg[fd] = r & 0xffffffffULL;

		if (cp->reg[fd] & 0x80000000ULL)
			cp->reg[fd] |= 0xffffffff00000000ULL;
	}
}


/*
 *  fpu_op():
 *
 *  Perform a floating-point operation.  For those of fs and ft
 *  that are >= 0, those numbers are interpreted into local
 *  variables.
 *
 *  Only FPU_OP_C (compare) returns anything of interest, 1 for
 *  true, 0 for false.
 */
static int fpu_op(struct cpu *cpu, struct coproc *cp, int op, int fmt,
	int ft, int fs, int fd, int cond, int output_fmt)
{
	/*  Potentially two input registers, fs and ft  */
	struct internal_float_value float_value[2];
	int unordered, nan;
	uint64_t fs_v = 0;
	double nf;

	if (fs >= 0) {
		fs_v = cp->reg[fs];
		/*  TODO: register-pair mode and plain register mode? "FR" bit?  */
		if (fmt == FMT_D || fmt == FMT_L)
			fs_v = (fs_v & 0xffffffffULL) + (cp->reg[(fs + 1) & 31] << 32);
		fpu_interpret_float_value(fs_v, &float_value[0], fmt);
	}
	if (ft >= 0) {
		uint64_t v = cp->reg[ft];
		/*  TODO: register-pair mode and plain register mode? "FR" bit?  */
		if (fmt == FMT_D || fmt == FMT_L)
			v = (v & 0xffffffffULL) + (cp->reg[(ft + 1) & 31] << 32);
		fpu_interpret_float_value(v, &float_value[1], fmt);
	}

	switch (op) {
	case FPU_OP_ADD:
		nf = float_value[0].f + float_value[1].f;
		/*  debug("  add: %f + %f = %f\n", float_value[0].f, float_value[1].f, nf);  */
		fpu_store_float_value(cp, fd, nf, output_fmt,
		    float_value[0].nan || float_value[1].nan);
		break;
	case FPU_OP_SUB:
		nf = float_value[0].f - float_value[1].f;
		/*  debug("  sub: %f - %f = %f\n", float_value[0].f, float_value[1].f, nf);  */
		fpu_store_float_value(cp, fd, nf, output_fmt,
		    float_value[0].nan || float_value[1].nan);
		break;
	case FPU_OP_MUL:
		nf = float_value[0].f * float_value[1].f;
		/*  debug("  mul: %f * %f = %f\n", float_value[0].f, float_value[1].f, nf);  */
		fpu_store_float_value(cp, fd, nf, output_fmt,
		    float_value[0].nan || float_value[1].nan);
		break;
	case FPU_OP_DIV:
		nan = float_value[0].nan || float_value[1].nan;
		if (fabs(float_value[1].f) > 0.00000000001)
			nf = float_value[0].f / float_value[1].f;
		else {
			fatal("DIV by zero !!!!\n");
			nf = 0.0;	/*  TODO  */
			nan = 1;
		}
		/*  debug("  div: %f / %f = %f\n", float_value[0].f, float_value[1].f, nf);  */
		fpu_store_float_value(cp, fd, nf, output_fmt, nan);
		break;
	case FPU_OP_SQRT:
		nan = float_value[0].nan;
		if (float_value[0].f >= 0.0)
			nf = sqrt(float_value[0].f);
		else {
			fatal("SQRT by less than zero, %f !!!!\n", float_value[0].f);
			nf = 0.0;	/*  TODO  */
			nan = 1;
		}
		/*  debug("  sqrt: %f => %f\n", float_value[0].f, nf);  */
		fpu_store_float_value(cp, fd, nf, output_fmt, nan);
		break;
	case FPU_OP_ABS:
		nf = fabs(float_value[0].f);
		/*  debug("  abs: %f => %f\n", float_value[0].f, nf);  */
		fpu_store_float_value(cp, fd, nf, output_fmt,
		    float_value[0].nan);
		break;
	case FPU_OP_NEG:
		nf = - float_value[0].f;
		/*  debug("  neg: %f => %f\n", float_value[0].f, nf);  */
		fpu_store_float_value(cp, fd, nf, output_fmt,
		    float_value[0].nan);
		break;
	case FPU_OP_CVT:
		nf = float_value[0].f;
		/*  debug("  mov: %f => %f\n", float_value[0].f, nf);  */
		fpu_store_float_value(cp, fd, nf, output_fmt,
		    float_value[0].nan);
		break;
	case FPU_OP_MOV:
		/*  Non-arithmetic move:  */
		/*
		 *  TODO:  this is for 32-bit mode. It has to be updated later
		 *		for 64-bit coprocessor stuff.
		 */
		if (output_fmt == FMT_D || output_fmt == FMT_L) {
			cp->reg[fd] = fs_v & 0xffffffffULL;
			cp->reg[(fd+1) & 31] = (fs_v >> 32) & 0xffffffffULL;
			if (cp->reg[fd] & 0x80000000ULL)
				cp->reg[fd] |= 0xffffffff00000000ULL;
			if (cp->reg[fd+1] & 0x80000000ULL)
				cp->reg[fd+1] |= 0xffffffff00000000ULL;
		} else {
			cp->reg[fd] = fs_v & 0xffffffffULL;
			if (cp->reg[fd] & 0x80000000ULL)
				cp->reg[fd] |= 0xffffffff00000000ULL;
		}
		break;
	case FPU_OP_C:
		/*  debug("  c: cond=%i\n", cond);  */

		unordered = 0;
		if (float_value[0].nan || float_value[1].nan)
			unordered = 1;

		switch (cond) {
		case 2:		return (float_value[0].f == float_value[1].f);	/*  Equal  */
		case 4:		return (float_value[0].f < float_value[1].f) || !unordered;	/*  Ordered or Less than  TODO (?)  */
		case 5:		return (float_value[0].f < float_value[1].f) || unordered;	/*  Unordered or Less than  */
		case 6:		return (float_value[0].f <= float_value[1].f) || !unordered;	/*  Ordered or Less than or Equal  TODO (?)  */
		case 7:		return (float_value[0].f <= float_value[1].f) || unordered;	/*  Unordered or Less than or Equal  */
		case 12:	return (float_value[0].f < float_value[1].f);	/*  Less than  */
		case 14:	return (float_value[0].f <= float_value[1].f);	/*  Less than or equal  */

		/*  The following are not commonly used, so I'll move these out
		    of the if-0 on a case-by-case basis.  */
#if 0
		case 0:		return 0;					/*  False  */
		case 1:		return 0;					/*  Unordered  */
		case 3:		return (float_value[0].f == float_value[1].f);	/*  Unordered or Equal  */
		case 8:		return 0;					/*  Signaling false  */
		case 9:		return 0;					/*  Not Greater than or Less than or Equal  */
		case 10:	return (float_value[0].f == float_value[1].f);	/*  Signaling Equal  */
		case 11:	return (float_value[0].f == float_value[1].f);	/*  Not Greater than or Less than  */
		case 13:	return !(float_value[0].f >= float_value[1].f);	/*  Not greater than or equal */
		case 15:	return !(float_value[0].f > float_value[1].f);	/*  Not greater than  */
#endif

		default:
			fatal("fpu_op(): unimplemented condition code %i\n", cond);
		}
		break;
	default:
		fatal("fpu_op(): unimplemented op %i\n", op);
	}

	return 0;
}


/*
 *  fpu_function():
 *
 *  Returns 1 if function was implemented, 0 otherwise.
 *  Debug trace should be printed for known instructions.
 */
static int fpu_function(struct cpu *cpu, struct coproc *cp,
	uint32_t function, int unassemble_only)
{
	int fd, fs, ft, fmt, cond, cc;

	fmt = (function >> 21) & 31;
	ft = (function >> 16) & 31;
	fs = (function >> 11) & 31;
	cc = (function >> 8) & 7;
	fd = (function >> 6) & 31;
	cond = (function >> 0) & 15;


	/*  bc1f, bc1t, bc1fl, bc1tl:  */
	if ((function & 0x03e00000) == 0x01000000) {
		int nd, tf, imm, cond_true;
		char *instr_mnem;

		/*  cc are bits 20..18:  */
		cc = (function >> 18) & 7;
		nd = (function >> 17) & 1;
		tf = (function >> 16) & 1;
		imm = function & 65535;
		if (imm >= 32768)
			imm -= 65536;

		instr_mnem = NULL;
		if (nd == 0 && tf == 0)  instr_mnem = "bc1f";
		if (nd == 0 && tf == 1)  instr_mnem = "bc1t";
		if (nd == 1 && tf == 0)  instr_mnem = "bc1fl";
		if (nd == 1 && tf == 1)  instr_mnem = "bc1tl";

		if (cpu->emul->instruction_trace || unassemble_only)
			debug("%s\t%i,0x%016llx\n", instr_mnem, cc, (long long) (cpu->pc + (imm << 2)));
		if (unassemble_only)
			return 1;

		if (cpu->delay_slot) {
			fatal("%s: jump inside a jump's delay slot, or similar. TODO\n", instr_mnem);
			cpu->running = 0;
			return 1;
		}

		/*  Both the FCCR and FCSR contain condition code bits...  */
		if (cc == 0)
			cond_true = (cp->fcr[FPU_FCSR] >> FCSR_FCC0_SHIFT) & 1;
		else
			cond_true = (cp->fcr[FPU_FCSR] >> (FCSR_FCC1_SHIFT + cc-1)) & 1;

		if (!tf)
			cond_true = !cond_true;

		if (cond_true) {
			cpu->delay_slot = TO_BE_DELAYED;
			cpu->delay_jmpaddr = cpu->pc + (imm << 2);
		} else {
			/*  "likely":  */
			if (nd)
				cpu->nullify_next = 1;	/*  nullify delay slot  */
		}

		return 1;
	}

	/*  add.fmt: Floating-point add  */
	if ((function & 0x0000003f) == 0x00000000) {
		if (cpu->emul->instruction_trace || unassemble_only)
			debug("add.%i\tr%i,r%i,r%i\n", fmt, fd, fs, ft);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_ADD, fmt, ft, fs, fd, -1, fmt);
		return 1;
	}

	/*  sub.fmt: Floating-point subtract  */
	if ((function & 0x0000003f) == 0x00000001) {
		if (cpu->emul->instruction_trace || unassemble_only)
			debug("sub.%i\tr%i,r%i,r%i\n", fmt, fd, fs, ft);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_SUB, fmt, ft, fs, fd, -1, fmt);
		return 1;
	}

	/*  mul.fmt: Floating-point multiply  */
	if ((function & 0x0000003f) == 0x00000002) {
		if (cpu->emul->instruction_trace || unassemble_only)
			debug("mul.%i\tr%i,r%i,r%i\n", fmt, fd, fs, ft);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_MUL, fmt, ft, fs, fd, -1, fmt);
		return 1;
	}

	/*  div.fmt: Floating-point divide  */
	if ((function & 0x0000003f) == 0x00000003) {
		if (cpu->emul->instruction_trace || unassemble_only)
			debug("div.%i\tr%i,r%i,r%i\n", fmt, fd, fs, ft);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_DIV, fmt, ft, fs, fd, -1, fmt);
		return 1;
	}

	/*  sqrt.fmt: Floating-point square-root  */
	if ((function & 0x001f003f) == 0x00000004) {
		if (cpu->emul->instruction_trace || unassemble_only)
			debug("sqrt.%i\tr%i,r%i\n", fmt, fd, fs);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_SQRT, fmt, -1, fs, fd, -1, fmt);
		return 1;
	}

	/*  abs.fmt: Floating-point absolute value  */
	if ((function & 0x001f003f) == 0x00000005) {
		if (cpu->emul->instruction_trace || unassemble_only)
			debug("abs.%i\tr%i,r%i\n", fmt, fd, fs);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_ABS, fmt, -1, fs, fd, -1, fmt);
		return 1;
	}

	/*  mov.fmt: Floating-point (non-arithmetic) move  */
	if ((function & 0x0000003f) == 0x00000006) {
		if (cpu->emul->instruction_trace || unassemble_only)
			debug("mov.%i\tr%i,r%i\n", fmt, fd, fs);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_MOV, fmt, -1, fs, fd, -1, fmt);
		return 1;
	}

	/*  neg.fmt: Floating-point negate  */
	if ((function & 0x001f003f) == 0x00000007) {
		if (cpu->emul->instruction_trace || unassemble_only)
			debug("neg.%i\tr%i,r%i\n", fmt, fd, fs);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_NEG, fmt, -1, fs, fd, -1, fmt);
		return 1;
	}

	/*  trunc.l.fmt: Truncate  */
	if ((function & 0x001f003f) == 0x00000009) {
		if (cpu->emul->instruction_trace || unassemble_only)
			debug("trunc.l.%i\tr%i,r%i\n", fmt, fd, fs);
		if (unassemble_only)
			return 1;

		/*  TODO: not CVT?  */

		fpu_op(cpu, cp, FPU_OP_CVT, fmt, -1, fs, fd, -1, FMT_L);
		return 1;
	}

	/*  trunc.w.fmt: Truncate  */
	if ((function & 0x001f003f) == 0x0000000d) {
		if (cpu->emul->instruction_trace || unassemble_only)
			debug("trunc.w.%i\tr%i,r%i\n", fmt, fd, fs);
		if (unassemble_only)
			return 1;

		/*  TODO: not CVT?  */

		fpu_op(cpu, cp, FPU_OP_CVT, fmt, -1, fs, fd, -1, FMT_W);
		return 1;
	}

	/*  c.cond.fmt: Floating-point compare  */
	if ((function & 0x000000f0) == 0x00000030) {
		int cond_true;
		int bit;

		if (cpu->emul->instruction_trace || unassemble_only)
			debug("c.%i.%i\tr%i,r%i,r%i\n", cond, fmt, cc, fs, ft);
		if (unassemble_only)
			return 1;

		cond_true = fpu_op(cpu, cp, FPU_OP_C, fmt,
		    ft, fs, -1, cond, fmt);

		/*
		 *  Both the FCCR and FCSR contain condition code bits:
		 *	FCCR:  bits 7..0
		 *	FCSR:  bits 31..25 and 23
		 */
		cp->fcr[FPU_FCCR] &= ~(1 << cc);
		if (cond_true)
			cp->fcr[FPU_FCCR] |= (1 << cc);

		if (cc == 0) {
			bit = 1 << FCSR_FCC0_SHIFT;
			cp->fcr[FPU_FCSR] &= ~bit;
			if (cond_true)
				cp->fcr[FPU_FCSR] |= bit;
		} else {
			bit = 1 << (FCSR_FCC1_SHIFT + cc-1);
			cp->fcr[FPU_FCSR] &= ~bit;
			if (cond_true)
				cp->fcr[FPU_FCSR] |= bit;
		}

		return 1;
	}

	/*  cvt.s.fmt: Convert to single floating-point  */
	if ((function & 0x001f003f) == 0x00000020) {
		if (cpu->emul->instruction_trace || unassemble_only)
			debug("cvt.s.%i\tr%i,r%i\n", fmt, fd, fs);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_CVT, fmt, -1, fs, fd, -1, FMT_S);
		return 1;
	}

	/*  cvt.d.fmt: Convert to double floating-point  */
	if ((function & 0x001f003f) == 0x00000021) {
		if (cpu->emul->instruction_trace || unassemble_only)
			debug("cvt.d.%i\tr%i,r%i\n", fmt, fd, fs);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_CVT, fmt, -1, fs, fd, -1, FMT_D);
		return 1;
	}

	/*  cvt.w.fmt: Convert to word fixed-point  */
	if ((function & 0x001f003f) == 0x00000024) {
		if (cpu->emul->instruction_trace || unassemble_only)
			debug("cvt.w.%i\tr%i,r%i\n", fmt, fd, fs);
		if (unassemble_only)
			return 1;

		fpu_op(cpu, cp, FPU_OP_CVT, fmt, -1, fs, fd, -1, FMT_W);
		return 1;
	}

	return 0;
}


/*
 *  coproc_tlbpr():
 *
 *  'tlbp' and 'tlbr'.
 */
void coproc_tlbpr(struct cpu *cpu, int readflag)
{
	struct coproc *cp = cpu->coproc[0];
	int i, found, g_bit;
	uint64_t vpn2, xmask;

	/*  Read:  */
	if (readflag) {
		if (cpu->cpu_type.mmu_model == MMU3K) {
			i = (cp->reg[COP0_INDEX] & R2K3K_INDEX_MASK) >> R2K3K_INDEX_SHIFT;
			if (i >= cp->nr_of_tlbs) {
				/*  TODO:  exception?  */
				fatal("warning: tlbr from index %i (too high)\n", i);
				return;
			}

			cp->reg[COP0_ENTRYHI]  = cp->tlbs[i].hi  & ~0x3f;
			cp->reg[COP0_ENTRYLO0] = cp->tlbs[i].lo0 & ~0xff;
		} else {
			/*  R4000:  */
			i = cp->reg[COP0_INDEX] & INDEX_MASK;
			if (i >= cp->nr_of_tlbs)	/*  TODO:  exception  */
				return;

			g_bit = cp->tlbs[i].hi & TLB_G;

			cp->reg[COP0_PAGEMASK] = cp->tlbs[i].mask;
			cp->reg[COP0_ENTRYHI]  = cp->tlbs[i].hi & ~TLB_G;
			cp->reg[COP0_ENTRYLO1] = cp->tlbs[i].lo1;
			cp->reg[COP0_ENTRYLO0] = cp->tlbs[i].lo0;

			cp->reg[COP0_ENTRYLO0] &= ~ENTRYLO_G;
			cp->reg[COP0_ENTRYLO1] &= ~ENTRYLO_G;
			if (g_bit) {
				cp->reg[COP0_ENTRYLO0] |= ENTRYLO_G;
				cp->reg[COP0_ENTRYLO1] |= ENTRYLO_G;
			}
		}

		return;
	}

	/*  Probe:  */
	if (cpu->cpu_type.mmu_model == MMU3K) {
		vpn2 = cp->reg[COP0_ENTRYHI] & R2K3K_ENTRYHI_VPN_MASK;
		found = -1;
		for (i=0; i<cp->nr_of_tlbs; i++)
			if ( ((cp->tlbs[i].hi & R2K3K_ENTRYHI_ASID_MASK) == (cp->reg[COP0_ENTRYHI] & R2K3K_ENTRYHI_ASID_MASK))
			    || cp->tlbs[i].lo0 & R2K3K_ENTRYLO_G)
				if ((cp->tlbs[i].hi & R2K3K_ENTRYHI_VPN_MASK) == vpn2) {
					found = i;
					break;
				}
	} else {
		/*  R4000 and R10000:  */
		if (cpu->cpu_type.mmu_model == MMU10K)
			xmask = ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK_R10K;
		else
			xmask = ENTRYHI_R_MASK | ENTRYHI_VPN2_MASK;
		vpn2 = cp->reg[COP0_ENTRYHI] & xmask;
		found = -1;
		for (i=0; i<cp->nr_of_tlbs; i++)
			if ( ((cp->tlbs[i].hi & ENTRYHI_ASID) == (cp->reg[COP0_ENTRYHI] & ENTRYHI_ASID))
			    || cp->tlbs[i].hi & TLB_G)
				if ((cp->tlbs[i].hi & xmask) == vpn2) {
					found = i;
					break;
				}
	}
	if (found == -1)
		cp->reg[COP0_INDEX] = INDEX_P;
	else {
		if (cpu->cpu_type.mmu_model == MMU3K)
			cp->reg[COP0_INDEX] = found << R2K3K_INDEX_SHIFT;
		else
			cp->reg[COP0_INDEX] = found;
	}

	/*  Sign extend the index register:  */
	if ((cp->reg[COP0_INDEX] >> 32) == 0 &&
	    cp->reg[COP0_INDEX] & 0x80000000)
		cp->reg[COP0_INDEX] |=
		    0xffffffff00000000ULL;
}


/*
 *  coproc_tlbwri():
 *
 *  'tlbwr' and 'tlbwi'
 */
void coproc_tlbwri(struct cpu *cpu, int randomflag)
{
	struct coproc *cp = cpu->coproc[0];
	int index, g_bit;
	uint64_t oldvaddr;
	int old_asid = -1;

	/*
	 *  ... and the last instruction page:
	 *
	 *  Some thoughts about this:  Code running in
	 *  the kernel's physical address space has the
	 *  same vaddr->paddr translation, so the last
	 *  virtual page invalidation only needs to
	 *  happen if we are for some extremely weird
	 *  reason NOT running in the kernel's physical
	 *  address space.
	 *
	 *  (An even insaner (but probably useless)
	 *  optimization would be to only invalidate
	 *  the last virtual page stuff if the TLB
	 *  update actually affects the vaddr in
	 *  question.)
	 */

	if (cpu->pc < (uint64_t)0xffffffff80000000ULL ||
	    cpu->pc >= (uint64_t)0xffffffffc0000000ULL)
		cpu->pc_last_virtual_page =
		    PC_LAST_PAGE_IMPOSSIBLE_VALUE;

	if (randomflag) {
		if (cpu->cpu_type.mmu_model == MMU3K)
			index = (cp->reg[COP0_RANDOM] & R2K3K_RANDOM_MASK) >> R2K3K_RANDOM_SHIFT;
		else
			index = cp->reg[COP0_RANDOM] & RANDOM_MASK;
	} else {
		if (cpu->cpu_type.mmu_model == MMU3K)
			index = (cp->reg[COP0_INDEX] & R2K3K_INDEX_MASK) >> R2K3K_INDEX_SHIFT;
		else
			index = cp->reg[COP0_INDEX] & INDEX_MASK;
	}

	if (index >= cp->nr_of_tlbs) {
		fatal("warning: tlb index %i too high (max is %i)\n", index, cp->nr_of_tlbs-1);
		/*  TODO:  cause an exception?  */
		return;
	}

#if 0
	/*  Debug dump of the previous entry at that index:  */
	debug(" old entry at index = %04x", index);
	debug(" mask = %016llx", (long long) cp->tlbs[index].mask);
	debug(" hi = %016llx", (long long) cp->tlbs[index].hi);
	debug(" lo0 = %016llx", (long long) cp->tlbs[index].lo0);
	debug(" lo1 = %016llx\n", (long long) cp->tlbs[index].lo1);
#endif

	/*  Translation caches must be invalidated:  */
	switch (cpu->cpu_type.mmu_model) {
	case MMU3K:
		oldvaddr = cp->tlbs[index].hi & R2K3K_ENTRYHI_VPN_MASK;
		oldvaddr &= 0xffffffffULL;
		if (oldvaddr & 0x80000000ULL)
			oldvaddr |= 0xffffffff00000000ULL;
		old_asid = (cp->tlbs[index].hi & R2K3K_ENTRYHI_ASID_MASK)
		    >> R2K3K_ENTRYHI_ASID_SHIFT;

/*  TODO: Bug? Why does this if need to be commented out?  */

		/*  if (cp->tlbs[index].lo0 & ENTRYLO_V) */
			invalidate_translation_caches(cpu, 0, oldvaddr, 0, 0);
		break;
	default:
		if (cpu->cpu_type.mmu_model == MMU10K) {
			oldvaddr = cp->tlbs[index].hi & ENTRYHI_VPN2_MASK_R10K;
			/*  44 addressable bits:  */
			if (oldvaddr & 0x80000000000ULL)
				oldvaddr |= 0xfffff00000000000ULL;
		} else {
			/*  Assume MMU4K  */
			oldvaddr = cp->tlbs[index].hi & ENTRYHI_VPN2_MASK;
			/*  40 addressable bits:  */
			if (oldvaddr & 0x8000000000ULL)
				oldvaddr |= 0xffffff0000000000ULL;
		}
		/*  Both pages:  */
		invalidate_translation_caches(cpu, 0, oldvaddr & ~0x1fff, 0, 0);
		invalidate_translation_caches(cpu, 0, (oldvaddr & ~0x1fff) | 0x1000, 0, 0);
	}


	/*  Write the new entry:  */

	if (cpu->cpu_type.mmu_model == MMU3K) {
		uint64_t vaddr, paddr;
		int wf = cp->reg[COP0_ENTRYLO0] & R2K3K_ENTRYLO_D? 1 : 0;
		unsigned char *memblock = NULL;

		cp->tlbs[index].hi = cp->reg[COP0_ENTRYHI];
		cp->tlbs[index].lo0 = cp->reg[COP0_ENTRYLO0];

		vaddr =  cp->reg[COP0_ENTRYHI] & R2K3K_ENTRYHI_VPN_MASK;
		paddr = cp->reg[COP0_ENTRYLO0] & R2K3K_ENTRYLO_PFN_MASK;

		/*  TODO: This is ugly.  */
		if (paddr < 0x10000000)
			memblock = memory_paddr_to_hostaddr(cpu->mem, paddr, 1);

		if (memblock != NULL && cp->reg[COP0_ENTRYLO0] & R2K3K_ENTRYLO_V) {
			memblock += (paddr & ((1 << BITS_PER_PAGETABLE) - 1));

			/*
			 *  TODO: Hahaha, this is even uglier than the thing
			 *  above. Some OSes seem to map code pages read/write,
			 *  which causes the bintrans cache to be invalidated
			 *  even when it doesn't have to be.
			 */
/*			if (vaddr < 0x10000000)  */
				wf = 0;

			update_translation_table(cpu, vaddr, memblock, wf, paddr);
		}
	} else {
		/*  R4000:  */
		g_bit = (cp->reg[COP0_ENTRYLO0] & cp->reg[COP0_ENTRYLO1]) & ENTRYLO_G;
		cp->tlbs[index].mask = cp->reg[COP0_PAGEMASK];
		cp->tlbs[index].hi   = cp->reg[COP0_ENTRYHI];
		cp->tlbs[index].lo1  = cp->reg[COP0_ENTRYLO1] & ~ENTRYLO_G;
		cp->tlbs[index].lo0  = cp->reg[COP0_ENTRYLO0] & ~ENTRYLO_G;
		cp->tlbs[index].hi &= ~TLB_G;
		if (g_bit)
			cp->tlbs[index].hi |= TLB_G;
	}

	if (randomflag) {
		if (cpu->cpu_type.exc_model == EXC3K) {
			cp->reg[COP0_RANDOM] =
			    ((random() % (cp->nr_of_tlbs - 8)) + 8) << R2K3K_RANDOM_SHIFT;
		} else {
			cp->reg[COP0_RANDOM] = cp->reg[COP0_WIRED] +
			    (random() % (cp->nr_of_tlbs - cp->reg[COP0_WIRED]));
		}
	}
}


/*
 *  coproc_rfe():
 *
 *  Return from exception. (R3000 etc.)
 */
void coproc_rfe(struct cpu *cpu)
{
	int oldmode;

	oldmode = cpu->coproc[0]->reg[COP0_STATUS] & MIPS1_SR_KU_CUR;

	cpu->coproc[0]->reg[COP0_STATUS] =
	    (cpu->coproc[0]->reg[COP0_STATUS] & ~0x3f) |
	    ((cpu->coproc[0]->reg[COP0_STATUS] & 0x3c) >> 2);

	/*  Changing from kernel to user mode?
	    Then this is necessary:  */
	if (!oldmode && 
	    (cpu->coproc[0]->reg[COP0_STATUS] &
	    MIPS1_SR_KU_CUR))
		invalidate_translation_caches(cpu, 0, 0, 1, 0);
}


/*
 *  coproc_eret():
 *
 *  Return from exception. (R4000 etc.)
 */
void coproc_eret(struct cpu *cpu)
{
	int oldmode, newmode;

	/*  Kernel mode flag:  */
	oldmode = 0;
	if ((cpu->coproc[0]->reg[COP0_STATUS] & MIPS3_SR_KSU_MASK)
			!= MIPS3_SR_KSU_USER
	    || (cpu->coproc[0]->reg[COP0_STATUS] & (STATUS_EXL |
	    STATUS_ERL)) ||
	    (cpu->coproc[0]->reg[COP0_STATUS] & 1) == 0)
		oldmode = 1;

	if (cpu->coproc[0]->reg[COP0_STATUS] & STATUS_ERL) {
		cpu->pc = cpu->pc_last = cpu->coproc[0]->reg[COP0_ERROREPC];
		cpu->coproc[0]->reg[COP0_STATUS] &= ~STATUS_ERL;
	} else {
		cpu->pc = cpu->pc_last = cpu->coproc[0]->reg[COP0_EPC];
		cpu->delay_slot = 0;
		cpu->coproc[0]->reg[COP0_STATUS] &= ~STATUS_EXL;
	}

	cpu->rmw = 0;	/*  the "LL bit"  */

	/*  New kernel mode flag:  */
	newmode = 0;
	if ((cpu->coproc[0]->reg[COP0_STATUS] & MIPS3_SR_KSU_MASK)
			!= MIPS3_SR_KSU_USER
	    || (cpu->coproc[0]->reg[COP0_STATUS] & (STATUS_EXL |
	    STATUS_ERL)) ||
	    (cpu->coproc[0]->reg[COP0_STATUS] & 1) == 0)
		newmode = 1;

	/*  Changing from kernel to user mode?
	    Then this is necessary:  TODO  */
	if (oldmode && !newmode)
		invalidate_translation_caches(cpu, 0, 0, 1, 0);
}


/*
 *  coproc_function():
 *
 *  Execute a coprocessor specific instruction. cp must be != NULL.
 *  Debug trace should be printed for known instructions, if
 *  unassemble_only is non-zero. (This will NOT execute the instruction.)
 *
 *  TODO:  This is a mess and should be restructured (again).
 */
void coproc_function(struct cpu *cpu, struct coproc *cp,
	uint32_t function, int unassemble_only, int running)
{
	int co_bit, op, rt, rd, fs, copz;
	uint64_t tmpvalue;
	int cpnr = cp->coproc_nr;

	/*  For quick reference:  */
	copz = (function >> 21) & 31;
	rt = (function >> 16) & 31;
	rd = (function >> 11) & 31;

	if (cpnr < 2 && (((function & 0x03e007f8) == (COPz_MFCz << 21))
	              || ((function & 0x03e007f8) == (COPz_DMFCz << 21)))) {
		if (unassemble_only) {
			debug("%s%i\tr%i,r%i\n", copz==COPz_DMFCz? "dmfc" : "mfc", cpnr, rt, rd);
			return;
		}
		coproc_register_read(cpu, cpu->coproc[cpnr], rd, &tmpvalue);
		cpu->gpr[rt] = tmpvalue;
		if (copz == COPz_MFCz) {
			/*  Sign-extend:  */
			cpu->gpr[rt] &= 0xffffffffULL;
			if (cpu->gpr[rt] & 0x80000000ULL)
				cpu->gpr[rt] |= 0xffffffff00000000ULL;
		}
		return;
	}

	if (cpnr < 2 && (((function & 0x03e007f8) == (COPz_MTCz << 21))
	              || ((function & 0x03e007f8) == (COPz_DMTCz << 21)))) {
		if (unassemble_only) {
			debug("%s%i\tr%i,r%i\n", copz==COPz_DMTCz? "dmtc" : "mtc", cpnr, rt, rd);
			return;
		}
		tmpvalue = cpu->gpr[rt];
		if (copz == COPz_MTCz) {
			/*  Sign-extend:  */
			tmpvalue &= 0xffffffffULL;
			if (tmpvalue & 0x80000000ULL)
				tmpvalue |= 0xffffffff00000000ULL;
		}
		coproc_register_write(cpu, cpu->coproc[cpnr], rd,
		    &tmpvalue, copz == COPz_DMTCz);
		return;
	}

	if (cpnr == 1 && (((function & 0x03e007ff) == (COPz_CFCz << 21))
	              || ((function & 0x03e007ff) == (COPz_CTCz << 21)))) {
		switch (copz) {
		case COPz_CFCz:		/*  Copy from FPU control register  */
			rt = (function >> 16) & 31;
			fs = (function >> 11) & 31;
			if (unassemble_only) {
				debug("cfc%i\tr%i,r%i\n", cpnr, rt, fs);
				return;
			}
			cpu->gpr[rt] = cp->fcr[fs] & 0xffffffffULL;
			if (cpu->gpr[rt] & 0x80000000ULL)
				cpu->gpr[rt] |= 0xffffffff00000000ULL;
			/*  TODO: implement delay for gpr[rt] (for MIPS I,II,III only)  */
			return;
		case COPz_CTCz:		/*  Copy to FPU control register  */
			rt = (function >> 16) & 31;
			fs = (function >> 11) & 31;
			if (unassemble_only) {
				debug("ctc%i\tr%i,r%i\n", cpnr, rt, fs);
				return;
			}
			if (fs == 0)
				fatal("[ Attempt to write to FPU control register 0 (?) ]\n");
			else {
				uint64_t tmp = cpu->gpr[rt];
				cp->fcr[fs] = tmp;

				/*  TODO: writing to control register 31
				    should cause exceptions, depending on
				    status bits!  */

				switch (fs) {
				case FPU_FCCR:
					cp->fcr[FPU_FCSR] =
					    (cp->fcr[FPU_FCSR] & 0x017fffffULL)
					    | ((tmp & 1) << FCSR_FCC0_SHIFT)
					    | (((tmp & 0xfe) >> 1) << FCSR_FCC1_SHIFT);
					break;
				case FPU_FCSR:
					cp->fcr[FPU_FCCR] =
					    (cp->fcr[FPU_FCCR] & 0xffffff00ULL)
					    | ((tmp >> FCSR_FCC0_SHIFT) & 1)
					    | (((tmp >> FCSR_FCC1_SHIFT) & 0x7f) << 1);
					break;
				default:
					;
				}
			}

			/*  TODO: implement delay for gpr[rt] (for MIPS I,II,III only)  */
			return;
		default:
			;
		}
	}

	/*  Math (Floating point) coprocessor calls:  */
	if (cpnr==1) {
		if (fpu_function(cpu, cp, function, unassemble_only))
			return;
	}

	/*  For AU1500 and probably others:  deret  */
	if (function == 0x0200001f) {
		if (unassemble_only) {
			debug("deret\n");
			return;
		}

		/*
		 *  According to the MIPS64 manual, deret loads PC from the
		 *  DEPC cop0 register, and jumps there immediately. No
		 *  delay slot.
		 *
		 *  TODO: This instruction is only available if the processor
		 *  is in debug mode. (What does that mean?)
		 *  TODO: This instruction is undefined in a delay slot.
		 */

		cpu->pc = cpu->pc_last = cp->reg[COP0_DEPC];
		cpu->delay_slot = 0;
		cp->reg[COP0_STATUS] &= ~STATUS_EXL;

		return;
	}


	/*  Ugly R5900 hacks:  */
	if ((function & 0xfffff) == 0x38) {		/*  ei  */
		if (unassemble_only) {
			debug("ei\n");
			return;
		}
		cpu->coproc[0]->reg[COP0_STATUS] |= R5900_STATUS_EIE;
		return;
	}

	if ((function & 0xfffff) == 0x39) {		/*  di  */
		if (unassemble_only) {
			debug("di\n");
			return;
		}
		cpu->coproc[0]->reg[COP0_STATUS] &= ~R5900_STATUS_EIE;
		return;
	}

	co_bit = (function >> 25) & 1;

	/*  TLB operations and other things:  */
	if (cp->coproc_nr == 0) {
		op = (function) & 31;
		switch (co_bit) {
		case 1:
			switch (op) {
			case COP0_TLBR:		/*  Read indexed TLB entry  */
				if (unassemble_only) {
					debug("tlbr\n");
					return;
				}
				coproc_tlbpr(cpu, 1);
				return;
			case COP0_TLBWI:	/*  Write indexed  */
			case COP0_TLBWR:	/*  Write random  */
				if (unassemble_only) {
					if (op == COP0_TLBWI)
						debug("tlbwi");
					else
						debug("tlbwr");
					if (!running) {
						debug("\n");
						return;
					}
					debug("\tindex=%08llx",
					    (long long)cp->reg[COP0_INDEX]);
					debug(", random=%08llx",
					    (long long)cp->reg[COP0_RANDOM]);
					debug(", mask=%016llx",
					    (long long)cp->reg[COP0_PAGEMASK]);
					debug(", hi=%016llx",
					    (long long)cp->reg[COP0_ENTRYHI]);
					debug(", lo0=%016llx",
					    (long long)cp->reg[COP0_ENTRYLO0]);
					debug(", lo1=%016llx\n",
					    (long long)cp->reg[COP0_ENTRYLO1]);
				}
				coproc_tlbwri(cpu, op == COP0_TLBWR);
				return;
			case COP0_TLBP:		/*  Probe TLB for matching entry  */
				if (unassemble_only) {
					debug("tlbp\n");
					return;
				}
				coproc_tlbpr(cpu, 0);
				return;
			case COP0_RFE:		/*  R2000/R3000 only: Return from Exception  */
				if (unassemble_only) {
					debug("rfe\n");
					return;
				}
				coproc_rfe(cpu);
				return;
			case COP0_ERET:		/*  R4000: Return from exception  */
				if (unassemble_only) {
					debug("eret\n");
					return;
				}
				coproc_eret(cpu);
				return;
			default:
				;
			}
		default:
			;
		}
	}

	/*  TODO: coprocessor R2020 on DECstation?  */
	if ((cp->coproc_nr==0 || cp->coproc_nr==3) && function == 0x0100ffff)
		return;

	/*  TODO: RM5200 idle (?)  */
	if ((cp->coproc_nr==0 || cp->coproc_nr==3) && function == 0x02000020) {
		if (unassemble_only) {
			debug("idle(?)\n");	/*  TODO  */
			return;
		}

		/*  Idle? TODO  */
		return;
	}

	if (unassemble_only) {
		debug("cop%i\t%08lx\n", cpnr, function);
		return;
	}

	fatal("cpu%i: warning: unimplemented coproc%i function %08lx (pc = %016llx)\n",
	    cpu->cpu_id, cp->coproc_nr, function, (long long)cpu->pc_last);
#if 1
	exit(1);
#else
	cpu_exception(cpu, EXCEPTION_CPU, 0, 0, cp->coproc_nr, 0, 0, 0);
#endif
}

