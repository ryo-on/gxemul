/*
 *  Copyright (C) 2003-2004  Anders Gavare.  All rights reserved.
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
 *  $Id: memory_v2p.c,v 1.12 2004-12-22 16:12:58 debug Exp $
 *
 *  Included from memory.c.
 */


/*
 *  translate_address():
 *
 *  Don't call this function is userland_emul is non-zero, or cpu is NULL.
 *
 *  TODO:  vpn2 is a bad name for R2K/R3K, as it is the actual framenumber.
 *
 *  Return values:
 *	0  Failure
 *	1  Success, the page is readable only
 *	2  Success, the page is read/write
 */
int TRANSLATE_ADDRESS(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_addr, int flags)
{
	int writeflag = flags & FLAG_WRITEFLAG? MEM_WRITE : MEM_READ;
	int no_exceptions = flags & FLAG_NOEXCEPTIONS;
	int instr = flags & FLAG_INSTR;
	int ksu, use_tlb, status, i;
	uint64_t vaddr_vpn2=0, vaddr_asid=0;
	int exccode, tlb_refill;
	struct coproc *cp0;
	int bintrans_cached = cpu->emul->bintrans_enable;

#ifdef V2P_MMU3K
	const int x_64 = 0;
	const int n_tlbs = 64;
	const int pmask = 0xfff;
#else
	int x_64;	/*  non-zero for 64-bit address space accesses  */
	int pageshift, n_tlbs;
	int pmask;
#endif


#ifdef USE_TINY_CACHE
	/*
	 *  Check the tiny translation cache first:
	 *
	 *  Only userland addresses are checked, because other addresses
	 *  are probably better of being statically translated, or through
	 *  the TLB.  (Note: When running with 64-bit addresses, this
	 *  will still produce the correct result. At worst, we check the
	 *  cache in vain, but the result should still be correct.)
	 */
	if (!bintrans_cached &&
	    (vaddr & 0xc0000000ULL) != 0x80000000ULL) {
		int i, wf = 1 + (writeflag == MEM_WRITE);
		uint64_t vaddr_shift_12 = vaddr >> 12;

		if (instr) {
			/*  Code:  */
			for (i=0; i<N_TRANSLATION_CACHE_INSTR; i++) {
				if (cpu->translation_cache_instr[i].wf >= wf &&
				    vaddr_shift_12 == (cpu->translation_cache_instr[i].vaddr_pfn)) {
					*return_addr = cpu->translation_cache_instr[i].paddr | (vaddr & 0xfff);
					return cpu->translation_cache_instr[i].wf;
				}
			}
		} else {
			/*  Data:  */
			for (i=0; i<N_TRANSLATION_CACHE_DATA; i++) {
				if (cpu->translation_cache_data[i].wf >= wf &&
				    vaddr_shift_12 == (cpu->translation_cache_data[i].vaddr_pfn)) {
					*return_addr = cpu->translation_cache_data[i].paddr | (vaddr & 0xfff);
					return cpu->translation_cache_data[i].wf;
				}
			}
		}
	}
#endif

	exccode = -1;
	tlb_refill = 1;

	/*  Cached values:  */
	cp0 = cpu->coproc[0];
	status = cp0->reg[COP0_STATUS];

	/*
	 *  R4000 Address Translation:
	 *
	 *  An address may be in one of the kernel segments, that
	 *  are directly mapped, or the address can go through the
	 *  TLBs to be turned into a physical address.
	 *
	 *  KSU: EXL: ERL: X:  Name:   Range:
	 *  ---- ---- ---- --  -----   ------
	 *
	 *   10   0    0    0  useg    0 - 0x7fffffff    (2GB)  (via TLB)
	 *   10   0    0    1  xuseg   0 - 0xffffffffff  (1TB)  (via TLB)
	 *
	 *   01   0    0    0  suseg   0          - 0x7fffffff                  (2GB)  (via TLB)
	 *   01   0    0    0  ssseg   0xc0000000 - 0xdfffffff                  (0.5 GB)  (via TLB)
	 *   01   0    0    1  xsuseg  0 - 0xffffffffff                         (1TB)  (via TLB)
	 *   01   0    0    1  xsseg   0x4000000000000000 - 0x400000ffffffffff  (1TB)  (via TLB)
	 *   01   0    0    1  csseg   0xffffffffc0000000 - 0xffffffffdfffffff  (0.5TB)  (via TLB)
	 *
	 *   00   x    x    0  kuseg   0 - 0x7fffffff  (2GB)  (via TLB)  (*)
	 *   00   x    x    0  kseg0   0x80000000 - 0x9fffffff (0.5GB)  unmapped, cached
	 *   00   x    x    0  kseg1   0xa0000000 - 0xbfffffff (0.5GB)  unmapped, uncached
	 *   00   x    x    0  ksseg   0xc0000000 - 0xdfffffff (0.5GB)  (via TLB)
	 *   00   x    x    0  kseg3   0xe0000000 - 0xffffffff (0.5GB)  (via TLB)
	 *   00   x    x    1  xksuseg 0 - 0xffffffffff (1TB) (via TLB) (*)
	 *   00   x    x    1  xksseg  0x4000000000000000 - 0x400000ffffffffff  (1TB)  (via TLB)
	 *   00   x    x    1  xkphys  0x8000000000000000 - 0xbfffffffffffffff  todo
	 *   00   x    x    1  xkseg   0xc000000000000000 - 0xc00000ff7fffffff  todo
	 *   00   x    x    1  ckseg0  0xffffffff80000000 - 0xffffffff9fffffff  like kseg0
	 *   00   x    x    1  ckseg1  0xffffffffa0000000 - 0xffffffffbfffffff  like kseg1
	 *   00   x    x    1  cksseg  0xffffffffc0000000 - 0xffffffffdfffffff  like ksseg
	 *   00   x    x    1  ckseg3  0xffffffffe0000000 - 0xffffffffffffffff  like kseg2
	 *
	 *  (*) = if ERL=1 then kuseg is not via TLB, but unmapped, uncached physical memory.
	 *
	 *  (KSU==0 or EXL=1 or ERL=1 is enough to use k*seg*.)
	 *
	 *  An invalid address causes an Address Error.
	 *
	 *  See chapter 4, page 96, in the R4000 manual for more info!
	 */

#ifdef V2P_MMU3K
	if (status & MIPS1_SR_KU_CUR)
		ksu = KSU_USER;
	else
		ksu = KSU_KERNEL;

	/*  These are needed later:  */
	vaddr_asid = cp0->reg[COP0_ENTRYHI] & R2K3K_ENTRYHI_ASID_MASK;
	vaddr_vpn2 = vaddr & R2K3K_ENTRYHI_VPN_MASK;
#else
	/*
	 *  R4000 and others:
	 *
	 *  kx,sx,ux = 0 for 32-bit addressing,
	 *  1 for 64-bit addressing. 
	 */
	n_tlbs = cpu->cpu_type.nr_of_tlb_entries;

	ksu = (status & STATUS_KSU_MASK) >> STATUS_KSU_SHIFT;
	if (status & (STATUS_EXL | STATUS_ERL))
		ksu = KSU_KERNEL;

	/*  Assume KSU_USER.  */
	x_64 = status & STATUS_UX;

	if (ksu == KSU_KERNEL)
		x_64 = status & STATUS_KX;
	else if (ksu == KSU_SUPERVISOR)
		x_64 = status & STATUS_SX;

	/*  This suppresses compiler warning:  */
	pageshift = 12;

	/*
	 *  Special uncached access modes, for SGI machines
	 *  and others (any R10000 system?).
	 *
	 *  Probably only accessible in kernel mode.
	 *
	 *  0x9000000080000000 = disable L2 cache (?)
	 *  TODO:  Make this correct.
	 */
	switch (vaddr >> 60) {
	/*
	 *  TODO:  SGI-IP27 and others, when running Irix, seem to
	 *  use these kernel-virtual-addresses as if they were
	 *  physical addresses.  Maybe a static mapping in the
	 *  tlb, say entry #0, would be a good solution?
	 */
	case 0xc:
		if (cpu->emul->emulation_type != EMULTYPE_SGI ||
		    (cpu->emul->machine < 25 && cpu->emul->machine != 19))
			break;
	case 8:
	case 9:
	case 0xa:
		/*
		 *  On IP30, addresses such as 0x900000001f600050 are used,
		 *  but also things like 0x90000000a0000000.  (TODO)
		 */
		*return_addr = vaddr & (((uint64_t)1 << 44) - 1);
		return 2;
	default:
		;
	}

	/*  This is needed later:  */
	vaddr_asid = cp0->reg[COP0_ENTRYHI] & ENTRYHI_ASID;
	/*  vpn2 depends on pagemask, which is not fixed on R4000  */
#endif


	if (vaddr <= 0x7fffffff)
		use_tlb = 1;
	else {
		/*  Sign-extend vaddr, if necessary:  */
		if ((vaddr >> 32) == 0 && vaddr & (uint32_t)0x80000000ULL)
			vaddr |= 0xffffffff00000000ULL;

		if (ksu == KSU_KERNEL) {
			/*  kseg0, kseg1:  */
			if (vaddr >= (uint64_t)0xffffffff80000000ULL &&
			    vaddr <= (uint64_t)0xffffffffbfffffffULL) {
				*return_addr = vaddr & 0x1fffffff;
				return 2;
			}

			/*  TODO: supervisor stuff  */

			/*  other segments:  */
			use_tlb = 1;
		} else
			use_tlb = 0;
	}

	if (use_tlb) {
#ifndef V2P_MMU3K
		int odd = 0, cached_lo1 = 0;
#endif
		int g_bit, v_bit, d_bit;
		uint64_t cached_hi, cached_lo0;
		uint64_t entry_vpn2 = 0, entry_asid, pfn;

		for (i=0; i<n_tlbs; i++) {
#ifdef V2P_MMU3K
			/*  R3000 or similar:  */
			cached_hi = cp0->tlbs[i].hi;
			cached_lo0 = cp0->tlbs[i].lo0;

			entry_vpn2 = cached_hi & R2K3K_ENTRYHI_VPN_MASK;
			entry_asid = cached_hi & R2K3K_ENTRYHI_ASID_MASK;
			g_bit = cached_lo0 & R2K3K_ENTRYLO_G;
			v_bit = cached_lo0 & R2K3K_ENTRYLO_V;
			d_bit = cached_lo0 & R2K3K_ENTRYLO_D;
#else
			/*  R4000 or similar:  */
			pmask = (cp0->tlbs[i].mask &
			    PAGEMASK_MASK) | 0x1fff;
			cached_hi = cp0->tlbs[i].hi;
			cached_lo0 = cp0->tlbs[i].lo0;
			cached_lo1 = cp0->tlbs[i].lo1;

			/*  Optimized for 4KB page size:  */
			if (pmask == 0x1fff) {
				pageshift = 12;
				entry_vpn2 = (cached_hi &
				    ENTRYHI_VPN2_MASK) >> 13;
				vaddr_vpn2 = (vaddr &
				    ENTRYHI_VPN2_MASK) >> 13;
				pmask = 0xfff;
				odd = vaddr & 0x1000;
			} else {
				/*  Non-standard page mask:  */
				switch (pmask) {
				case 0x0007fff:	pageshift = 14; break;
				case 0x001ffff:	pageshift = 16; break;
				case 0x007ffff:	pageshift = 18; break;
				case 0x01fffff:	pageshift = 20; break;
				case 0x07fffff:	pageshift = 22; break;
				case 0x1ffffff:	pageshift = 24; break;
				case 0x7ffffff:	pageshift = 26; break;
				default:
					fatal("pmask=%08x\n", i, pmask);
					exit(1);
				}

				entry_vpn2 = (cached_hi &
				    ENTRYHI_VPN2_MASK) >>
				    (pageshift + 1);
				vaddr_vpn2 = (vaddr &
				    ENTRYHI_VPN2_MASK) >>
				    (pageshift + 1);
				pmask >>= 1;
				odd = (vaddr >> pageshift) & 1;
			}

			/*  Assume even virtual page...  */
			v_bit = cached_lo0 & ENTRYLO_V;
			d_bit = cached_lo0 & ENTRYLO_D;

			switch (cpu->cpu_type.mmu_model) {
			case MMU10K:
				entry_vpn2 = (cached_hi & ENTRYHI_VPN2_MASK_R10K) >> (pageshift + 1);
				vaddr_vpn2 = (vaddr & ENTRYHI_VPN2_MASK_R10K) >> (pageshift + 1);
				break;
			case MMU8K:
				/*
				 *  TODO:  I don't really know anything about the R8000.
				 *  http://futuretech.mirror.vuurwerk.net/i2sec7.html
				 *  says that it has a three-way associative TLB with
				 *  384 entries, 16KB page size, and some other things.
				 *
				 *  It feels like things like the valid bit (ala R4000)
				 *  and dirty bit are not implemented the same on R8000.
				 *
				 *  http://sgistuff.tastensuppe.de/documents/R8000_chipset.html
				 *  also has some info, but no details.
				 */
				v_bit = 1;	/*  Big TODO  */
				d_bit = 1;
			}

			entry_asid = cached_hi & ENTRYHI_ASID;
			g_bit = cached_hi & TLB_G;

			/*  ... reload pfn, v_bit, d_bit if
			    it was the odd virtual page:  */
			if (odd) {
				v_bit = cached_lo1 & ENTRYLO_V;
				d_bit = cached_lo1 & ENTRYLO_D;
			}
#endif

			/*  Is there a VPN and ASID match?  */
			if (entry_vpn2 == vaddr_vpn2 &&
			    (entry_asid == vaddr_asid || g_bit)) {
				/*  debug("OK MAP 1!!! { vaddr=%016llx ==> paddr %016llx v=%i d=%i asid=0x%02x }\n",
				    (long long)vaddr, (long long) *return_addr, v_bit?1:0, d_bit?1:0, vaddr_asid);  */
				if (v_bit) {
					if (d_bit || (!d_bit && writeflag==MEM_READ)) {
						uint64_t paddr;
						/*  debug("OK MAP 2!!! { w=%i vaddr=%016llx ==> d=%i v=%i paddr %016llx ",
						    writeflag, (long long)vaddr, d_bit?1:0, v_bit?1:0, (long long) *return_addr);
						    debug(", tlb entry %2i: mask=%016llx hi=%016llx lo0=%016llx lo1=%016llx\n",
							i, cp0->tlbs[i].mask, cp0->tlbs[i].hi, cp0->tlbs[i].lo0, cp0->tlbs[i].lo1);  */

#ifdef V2P_MMU3K
						pfn = cached_lo0 & R2K3K_ENTRYLO_PFN_MASK;
						paddr = pfn | (vaddr & pmask);
#else
						pfn = ((odd? cached_lo1 : cached_lo0)
						    & ENTRYLO_PFN_MASK)
						    >> ENTRYLO_PFN_SHIFT;
						paddr = (pfn << 12) |
						    (vaddr & pmask);
#endif

						/*
						 *  Enter into the tiny trans-
						 *  lation cache (if enabled)
						 *  and return:
						 */
						if (!bintrans_cached)
							insert_into_tiny_cache(cpu,
							    instr, d_bit? MEM_WRITE : MEM_READ,
							    vaddr, paddr);

						*return_addr = paddr;
						return d_bit? 2 : 1;
					} else {
						/*  TLB modification exception  */
						tlb_refill = 0;
						exccode = EXCEPTION_MOD;
						goto exception;
					}
				} else {
					/*  TLB invalid exception  */
					tlb_refill = 0;
					goto exception;
				}
			}
		}
	}

	/*
	 *  We are here if for example userland code tried to access
	 *  kernel memory.
	 */

	/*  TLB refill  */

exception:
	if (no_exceptions)
		return 0;

	/*  TLB Load or Store exception:  */
	if (exccode == -1) {
		if (writeflag == MEM_WRITE)
			exccode = EXCEPTION_TLBS;
		else
			exccode = EXCEPTION_TLBL;
	}

#ifdef V2P_MMU3K
	vaddr_asid >>= R2K3K_ENTRYHI_ASID_SHIFT;
	vaddr_vpn2 >>= 12;
#endif

	cpu_exception(cpu, exccode, tlb_refill, vaddr,
	    0, vaddr_vpn2, vaddr_asid, x_64);

	/*  Return failure:  */
	return 0;
}


