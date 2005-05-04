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
 *  $Id: cpu_x86.c,v 1.37 2005-05-04 20:59:13 debug Exp $
 *
 *  x86 (and amd64) CPU emulation.
 *
 *
 *  TODO:  Pretty much everything that has to do with 64-bit and 32-bit modes,
 *  memory translation, flag bits, and so on.
 *
 *  See http://www.amd.com/us-en/Processors/DevelopWithAMD/
 *	0,,30_2252_875_7044,00.html for more info on AMD64.
 *
 *  http://www.cs.ucla.edu/~kohler/class/04f-aos/ref/i386/appa.htm has a
 *  nice overview of the standard i386 opcodes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "misc.h"


#ifndef	ENABLE_X86


#include "cpu_x86.h"


/*
 *  x86_cpu_family_init():
 *
 *  Bogus, when ENABLE_X86 isn't defined.
 */
int x86_cpu_family_init(struct cpu_family *fp)
{
	return 0;
}


#else	/*  ENABLE_X86  */


#include "cpu.h"
#include "cpu_x86.h"
#include "machine.h"
#include "memory.h"
#include "symbol.h"


extern volatile int single_step;
extern int old_show_trace_tree;   
extern int old_instruction_trace;
extern int old_quiet_mode;
extern int quiet_mode;


static struct x86_model models[] = x86_models;
static char *reg_names[N_X86_REGS] = x86_reg_names;
static char *reg_names_bytes[8] = x86_reg_names_bytes;
static char *seg_names[N_X86_SEGS] = x86_seg_names;
static char *cond_names[N_X86_CONDS] = x86_cond_names;


/*
 *  x86_cpu_new():
 *
 *  Create a new x86 cpu object.
 */
struct cpu *x86_cpu_new(struct memory *mem, struct machine *machine,
	int cpu_id, char *cpu_type_name)
{
	int i = 0;
	struct cpu *cpu;

	if (cpu_type_name == NULL)
		return NULL;

	/*  Try to find a match:  */
	while (models[i].model_number != 0) {
		if (strcasecmp(cpu_type_name, models[i].name) == 0)
			break;
		i++;
	}

	if (models[i].name == NULL)
		return NULL;

	cpu = malloc(sizeof(struct cpu));
	if (cpu == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(cpu, 0, sizeof(struct cpu));
	cpu->memory_rw          = x86_memory_rw;
	cpu->name               = cpu_type_name;
	cpu->mem                = mem;
	cpu->machine            = machine;
	cpu->cpu_id             = cpu_id;
	cpu->byte_order         = EMUL_LITTLE_ENDIAN;
	cpu->bootstrap_cpu_flag = 0;
	cpu->running            = 0;

	cpu->cd.x86.model = models[i];
	cpu->cd.x86.mode = 32;
	cpu->cd.x86.bits = 32;

	if (cpu->cd.x86.model.model_number == X86_MODEL_AMD64)
		cpu->cd.x86.bits = 64;

	cpu->cd.x86.r[X86_R_SP] = 0xff0;

	/*  Only show name and caches etc for CPU nr 0 (in SMP machines):  */
	if (cpu_id == 0) {
		debug("%s", cpu->name);
	}

	return cpu;
}


/*
 *  x86_cpu_dumpinfo():
 */
void x86_cpu_dumpinfo(struct cpu *cpu)
{
	debug(" (%i-bit)", cpu->cd.x86.bits);
	debug(", currently in %i-bit mode", cpu->cd.x86.mode);
	debug("\n");
}


/*
 *  x86_cpu_list_available_types():
 *
 *  Print a list of available x86 CPU types.
 */
void x86_cpu_list_available_types(void)
{
	int i = 0, j;

	while (models[i].model_number != 0) {
		debug("%s", models[i].name);

		for (j=0; j<10-strlen(models[i].name); j++)
			debug(" ");
		i++;
		if ((i % 6) == 0 || models[i].name == NULL)
			debug("\n");
	}
}


/*
 *  x86_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *  (gprs and coprocs are mostly useful for the MIPS version of this function.)
 */
void x86_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int i, x = cpu->cpu_id;

	if (cpu->cd.x86.mode == 16) {
		debug("cpu%i:  cs:ip = 0x%04x:0x%04x\n", x,
		    cpu->cd.x86.s[X86_S_CS], (int)cpu->pc);

		debug("cpu%i:  ax = 0x%04x  bx = 0x%04x  cx = 0x%04x  dx = "
		    "0x%04x\n", x,
		    (int)cpu->cd.x86.r[X86_R_AX], (int)cpu->cd.x86.r[X86_R_BX],
		    (int)cpu->cd.x86.r[X86_R_CX], (int)cpu->cd.x86.r[X86_R_DX]);
		debug("cpu%i:  si = 0x%04x  di = 0x%04x  bp = 0x%04x  sp = "
		    "0x%04x\n", x,
		    (int)cpu->cd.x86.r[X86_R_SI], (int)cpu->cd.x86.r[X86_R_DI],
		    (int)cpu->cd.x86.r[X86_R_BP], (int)cpu->cd.x86.r[X86_R_SP]);

		debug("cpu%i:  ds = 0x%04x  es = 0x%04x  ss = 0x%04x  flags "
		    "= 0x%04x\n", x,
		    (int)cpu->cd.x86.s[X86_S_DS], (int)cpu->cd.x86.s[X86_S_ES],
		    (int)cpu->cd.x86.s[X86_S_SS], (int)cpu->cd.x86.rflags);
	} else if (cpu->cd.x86.mode == 32) {
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);

		debug("cpu%i:  eip=0x", x);
	        debug("%08x", (int)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		debug("cpu%i:  eax=0x%08x  ebx=0x%08x  ecx=0x%08x  edx="
		    "0x%08x\n", x,
		    (int)cpu->cd.x86.r[X86_R_AX], (int)cpu->cd.x86.r[X86_R_BX],
		    (int)cpu->cd.x86.r[X86_R_CX], (int)cpu->cd.x86.r[X86_R_DX]);
		debug("cpu%i:  esi=0x%08x  edi=0x%08x  ebp=0x%08x  esp="
		    "0x%08x\n", x,
		    (int)cpu->cd.x86.r[X86_R_SI], (int)cpu->cd.x86.r[X86_R_DI],
		    (int)cpu->cd.x86.r[X86_R_BP], (int)cpu->cd.x86.r[X86_R_SP]);
	} else {
		/*  64-bit  */
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);

		debug("cpu%i:  rip = 0x", x);
	        debug("%016llx", (long long)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		for (i=0; i<N_X86_REGS; i++) {
			if ((i & 1) == 0)
				debug("cpu%i:", x);
			debug("  r%s = 0x%016llx", reg_names[i],
			    (long long)cpu->cd.x86.r[i]);
			if ((i & 1) == 1)
				debug("\n");
		}
	}

	if (cpu->cd.x86.mode >= 32) {
		debug("cpu%i:  cs=0x%04x  ds=0x%04x  es=0x%04x  "
		    "fs=0x%04x  gs=0x%04x  ss=0x%04x\n", x,
		    (int)cpu->cd.x86.s[X86_S_CS], (int)cpu->cd.x86.s[X86_S_DS],
		    (int)cpu->cd.x86.s[X86_S_ES], (int)cpu->cd.x86.s[X86_S_FS],
		    (int)cpu->cd.x86.s[X86_S_GS], (int)cpu->cd.x86.s[X86_S_SS]);
	}

	if (cpu->cd.x86.mode == 32) {
		debug("cpu%i:  cr0 = 0x%08x  cr3 = 0x%08x  eflags = 0x%08x\n",
		    x, (int)cpu->cd.x86.cr[0],
		    (int)cpu->cd.x86.cr[3], (int)cpu->cd.x86.rflags);
	}

	if (cpu->cd.x86.mode == 64) {
		debug("cpu%i:  cr0 = 0x%016llx  cr3 = 0x%016llx\n", x,
		    "0x%016llx\n", x, (long long)cpu->cd.x86.cr[0], (long long)
		    cpu->cd.x86.cr[3]);
		debug("cpu%i:  rflags = 0x%016llx\n", x,
		    (long long)cpu->cd.x86.rflags);
	}
}


/*
 *  x86_cpu_register_match():
 */
void x86_cpu_register_match(struct machine *m, char *name,
	int writeflag, uint64_t *valuep, int *match_register)
{
	int cpunr = 0;

	/*  CPU number:  */

	/*  TODO  */

	/*  Register name:  */
	if (strcasecmp(name, "pc") == 0 || strcasecmp(name, "ip") == 0
	    || strcasecmp(name, "eip") == 0) {
		if (writeflag) {
			m->cpus[cpunr]->pc = *valuep;
		} else
			*valuep = m->cpus[cpunr]->pc;
		*match_register = 1;
	}

#if 0
TODO: regmatch for 64, 32, 16, and 8 bit register names
#endif
}


/*  Macro which modifies the lower part of a value, or the entire value,
    depending on 'mode':  */
#define modify(old,new) (					\
		mode==16? (					\
			((old) & ~0xffff) + ((new) & 0xffff)	\
		) : (new) )

#define	HEXPRINT(x,n) { int j; for (j=0; j<(n); j++) debug("%02x",(x)[j]); }
#define	HEXSPACES(i) { int j; for (j=0; j<10-(i);j++) debug("  "); debug(" "); }
#define	SPACES	HEXSPACES(ilen)


static uint32_t read_imm_common(unsigned char **instrp, uint64_t *ilenp,
	int len, int printflag)
{
	uint32_t imm;
	unsigned char *instr = *instrp;

	if (len == 8)
		imm = instr[0];
	else if (len == 16)
		imm = instr[0] + (instr[1] << 8);
	else
		imm = instr[0] + (instr[1] << 8) +
		    (instr[2] << 16) + (instr[3] << 24);

	if (printflag)
		HEXPRINT(instr, len / 8);

	if (ilenp != NULL)
		(*ilenp) += len/8;

	(*instrp) += len/8;
	return imm;
}


static uint32_t read_imm_and_print(unsigned char **instrp, uint64_t *ilenp,
	int mode)
{
	return read_imm_common(instrp, ilenp, mode, 1);
}


static uint32_t read_imm(unsigned char **instrp, uint64_t *newpcp,
	int mode)
{
	return read_imm_common(instrp, newpcp, mode, 0);
}


static void print_csip(struct cpu *cpu)
{
	if (cpu->cd.x86.mode < 64)
		fatal("0x%04x:", cpu->cd.x86.s[X86_S_CS]);
	switch (cpu->cd.x86.mode) {
	case 16: fatal("0x%04x", (int)cpu->pc); break;
	case 32: fatal("0x%08x", (int)cpu->pc); break;
	case 64: fatal("0x%016llx", (long long)cpu->pc); break;
	}
}


/*
 *  x86_load():
 *
 *  Returns same error code as memory_rw().
 */
static int x86_load(struct cpu *cpu, uint64_t addr, uint64_t *data, int len)
{
	unsigned char databuf[8];
	int res;
	uint64_t d;

	res = cpu->memory_rw(cpu, cpu->mem, addr, &databuf[0], len,
	    MEM_READ, CACHE_DATA);

	d = databuf[0];
	if (len > 1) {
		d += ((uint64_t)databuf[1] << 8);
		if (len > 2) {
			d += ((uint64_t)databuf[2] << 16);
			d += ((uint64_t)databuf[3] << 24);
			if (len > 4) {
				d += ((uint64_t)databuf[4] << 32);
				d += ((uint64_t)databuf[5] << 40);
				d += ((uint64_t)databuf[6] << 48);
				d += ((uint64_t)databuf[7] << 56);
			}
		}
	}

	*data = d;
	return res;
}


/*
 *  x86_store():
 *
 *  Returns same error code as memory_rw().
 */
static int x86_store(struct cpu *cpu, uint64_t addr, uint64_t data, int len)
{
	unsigned char databuf[8];

	/*  x86 is always little-endian:  */
	databuf[0] = data;
	if (len > 1) {
		databuf[1] = data >> 8;
		if (len > 2) {
			databuf[2] = data >> 16;
			databuf[3] = data >> 24;
			if (len > 4) {
				databuf[4] = data >> 32;
				databuf[5] = data >> 40;
				databuf[6] = data >> 48;
				databuf[7] = data >> 56;
			}
		}
	}

	return cpu->memory_rw(cpu, cpu->mem, addr, &databuf[0], len,
	    MEM_WRITE, CACHE_DATA);
}


static char modrm_r[65];
static char modrm_rm[65];
#define MODRM_READ	0
#define MODRM_WRITE_RM	1
#define MODRM_WRITE_R	2
/*  eightbit flags:  */
#define	MODRM_EIGHTBIT	1
#define	MODRM_SEG	2
/*
 *  modrm():
 *
 *  Yuck. I have a feeling that this function will become really ugly.
 */
static int modrm(struct cpu *cpu, int writeflag, int mode, int eightbit,
	unsigned char **instrp, uint64_t *lenp, uint64_t *op1p, uint64_t *op2p)
{
	uint32_t imm, imm2;
	uint64_t addr;
	int mod, r, rm, res = 1, z, q = mode/8;
	int disasm = (op1p == NULL);

	if (disasm) {
		modrm_rm[0] = modrm_rm[sizeof(modrm_rm)-1] = '\0';
		modrm_r[0] = modrm_r[sizeof(modrm_r)-1] = '\0';
	}

	imm = read_imm_common(instrp, lenp, 8, disasm);
	mod = (imm >> 6) & 3; r = (imm >> 3) & 7; rm = imm & 7;

	if (eightbit & MODRM_EIGHTBIT)
		q = 1;

	/*
	 *  R/M:
	 */

	switch (mod) {
	case 0:
		if (disasm) {
			switch (rm) {
			case 0:	sprintf(modrm_rm, "[bx+si]");
				break;
			case 1:	sprintf(modrm_rm, "[bx+di]");
				break;
			case 2:	sprintf(modrm_rm, "[bp+si]");
				break;
			case 3:	sprintf(modrm_rm, "[bp+di]");
				break;
			case 4:	sprintf(modrm_rm, "[si]");
				break;
			case 5:	sprintf(modrm_rm, "[di]");
				break;
			case 6:	imm2 = read_imm_common(instrp, lenp,
				    mode, disasm);
				sprintf(modrm_rm, "[0x%x]", imm2);
				break;
			case 7:	sprintf(modrm_rm, "[bx]");
				break;
			}
		} else {
			switch (rm) {
			case 0:	addr = cpu->cd.x86.r[X86_R_BX] +
				    cpu->cd.x86.r[X86_R_SI]; break;
			case 1:	addr = cpu->cd.x86.r[X86_R_BX] +
				    cpu->cd.x86.r[X86_R_DI]; break;
			case 2:	addr = cpu->cd.x86.r[X86_R_BP] +
				    cpu->cd.x86.r[X86_R_SI]; break;
			case 3:	addr = cpu->cd.x86.r[X86_R_BP] +
				    cpu->cd.x86.r[X86_R_DI]; break;
			case 4:	addr = cpu->cd.x86.r[X86_R_SI]; break;
			case 5:	addr = cpu->cd.x86.r[X86_R_DI]; break;
			case 6:	addr = read_imm_common(instrp, lenp,
				    mode, disasm); break;
			case 7:	addr = cpu->cd.x86.r[X86_R_BX]; break;
			}

			switch (writeflag) {
			case MODRM_WRITE_RM:
				res = x86_store(cpu, addr, *op1p, q);
				break;
			case MODRM_READ:	/*  read  */
				res = x86_load(cpu, addr, op1p, q);
			}
		}
		break;
	case 1:
	case 2:
		z = (mod == 1)? 8 : mode;
		if (disasm) {
			switch (rm) {
			case 0:	imm2 = read_imm_common(instrp, lenp, z, disasm);
				sprintf(modrm_rm, "[bx+si+0x%x]", imm2); break;
			case 1:	imm2 = read_imm_common(instrp, lenp, z, disasm);
				sprintf(modrm_rm, "[bx+di+0x%x]", imm2); break;
			case 2:	imm2 = read_imm_common(instrp, lenp, z, disasm);
				sprintf(modrm_rm, "[bp+si+0x%x]", imm2); break;
			case 3:	imm2 = read_imm_common(instrp, lenp, z, disasm);
				sprintf(modrm_rm, "[bp+di+0x%x]", imm2); break;
			case 4:	imm2 = read_imm_common(instrp, lenp, z, disasm);
				sprintf(modrm_rm, "[si+0x%x]", imm2); break;
			case 5:	imm2 = read_imm_common(instrp, lenp, z, disasm);
				sprintf(modrm_rm, "[di+0x%x]", imm2); break;
			case 6:	imm2 = read_imm_common(instrp, lenp, z, disasm);
				sprintf(modrm_rm, "[bp+0x%x]", imm2); break;
			case 7:	imm2 = read_imm_common(instrp, lenp, z, disasm);
				sprintf(modrm_rm, "[bx+0x%x]", imm2); break;
			}
		} else {
			switch (rm) {
			case 0:	addr = (signed char)read_imm_common(instrp,
				    lenp, z, disasm) + cpu->cd.x86.r[X86_R_BX]
				    + cpu->cd.x86.r[X86_R_SI];
				break;
			case 1:	addr = (signed char)read_imm_common(instrp,
				    lenp, z, disasm) + cpu->cd.x86.r[X86_R_BX]
				    + cpu->cd.x86.r[X86_R_DI];
				break;
			case 2:	addr = (signed char)read_imm_common(instrp,
				    lenp, z, disasm) + cpu->cd.x86.r[X86_R_BP]
				    + cpu->cd.x86.r[X86_R_SI];
				break;
			case 3:	addr = (signed char)read_imm_common(instrp,
				    lenp, z, disasm) + cpu->cd.x86.r[X86_R_BP]
				    + cpu->cd.x86.r[X86_R_DI];
				break;
			case 4:	addr = (signed char)read_imm_common(instrp,
				    lenp, z, disasm) + cpu->cd.x86.r[X86_R_SI];
				break;
			case 5:	addr = (signed char)read_imm_common(instrp,
				    lenp, z, disasm) + cpu->cd.x86.r[X86_R_DI];
				break;
			case 6:	addr = (signed char)read_imm_common(instrp,
				    lenp, z, disasm) + cpu->cd.x86.r[X86_R_BP];
				break;
			case 7:	addr = (signed char)read_imm_common(instrp,
				    lenp, z, disasm) + cpu->cd.x86.r[X86_R_BX];
				break;
			}

			switch (writeflag) {
			case MODRM_WRITE_RM:
				res = x86_store(cpu, addr, *op1p, q);
				break;
			case MODRM_READ:	/*  read  */
				res = x86_load(cpu, addr, op1p, q);
			}
		}
		break;
	case 3:
		if (eightbit & MODRM_EIGHTBIT) {
			if (disasm) {
				strcpy(modrm_rm, reg_names_bytes[rm]);
			} else {
				switch (writeflag) {
				case MODRM_WRITE_RM:
					if (rm < 4)
						cpu->cd.x86.r[rm] =
						    (cpu->cd.x86.r[rm] &
						    ~0xff) | (*op1p & 0xff);
					else
						cpu->cd.x86.r[rm&3] = (cpu->
						    cd.x86.r[rm&3] & ~0xff00) |
						    ((*op1p & 0xff) << 8);
					break;
				case MODRM_READ:
					if (rm < 4)
						*op1p = cpu->cd.x86.r[rm] &
						    0xff;
					else
						*op1p = (cpu->cd.x86.r[rm&3] &
						     0xff00) >> 8;
				}
			}
		} else {
			if (disasm) {
				strcpy(modrm_rm, reg_names[rm]);
			} else {
				switch (writeflag) {
				case MODRM_WRITE_RM:
					cpu->cd.x86.r[rm] = *op1p;
					break;
				case MODRM_READ:	/*  read  */
					*op1p = cpu->cd.x86.r[rm];
				}
			}
		}
		break;
	default:
		fatal("modrm(): unimplemented mod %i\n", mod);
		exit(1);
	}


	/*
	 *  R:
	 */

	if (eightbit & MODRM_EIGHTBIT) {
		if (disasm) {
			strcpy(modrm_r, reg_names_bytes[r]);
		} else {
			switch (writeflag) {
			case MODRM_WRITE_R:
				if (r < 4)
					cpu->cd.x86.r[r] = (cpu->cd.x86.r[r] &
					    ~0xff) | (*op2p & 0xff);
				else
					cpu->cd.x86.r[r&3] = (cpu->cd.x86.r[r&3]
					    & ~0xff00) | ((*op2p & 0xff) << 8);
				break;
			case MODRM_READ:
				if (r < 4)
					*op2p = cpu->cd.x86.r[r] & 0xff;
				else
					*op2p = (cpu->cd.x86.r[r&3] &
					    0xff00) >>8;
			}
		}
	} else {
		if (disasm) {
			if (eightbit & MODRM_SEG)
				strcpy(modrm_r, seg_names[r]);
			else
				strcpy(modrm_r, reg_names[r]);
		} else {
			switch (writeflag) {
			case MODRM_WRITE_R:
				if (eightbit & MODRM_SEG)
					cpu->cd.x86.s[r] = *op2p;
				else
					cpu->cd.x86.r[r] =
					    modify(cpu->cd.x86.r[r], *op2p);
				break;
			case MODRM_READ:
				if (eightbit & MODRM_SEG)
					*op2p = cpu->cd.x86.s[r];
				else
					*op2p = cpu->cd.x86.r[r];
			}
		}
	}

	return res;
}


/*
 *  x86_cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing.
 *
 *  If running is 1, cpu->pc should be the address of the instruction.
 *
 *  If running is 0, things that depend on the runtime environment (eg.
 *  register contents) will not be shown, and addr will be used instead of
 *  cpu->pc for relative addresses.
 */
int x86_cpu_disassemble_instr(struct cpu *cpu, unsigned char *instr,
	int running, uint64_t dumpaddr, int bintrans)
{
	int op, rep = 0, n_prefix_bytes = 0;
	uint64_t ilen = 0, offset;
	uint32_t imm=0, imm2, mode = cpu->cd.x86.mode;
	char *symbol, *tmp = "ERROR", *mnem = "ERROR", *e = "e",
	    *prefix = NULL;

	if (running)
		dumpaddr = cpu->pc;

	symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset==0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

	if (mode == 32)
		debug("%08x:  ", (int)dumpaddr);
	else if (mode == 64)
		debug("%016llx:  ", (long long)dumpaddr);
	else { /*  16-bit mode  */
		if (running)
			debug("%04x:%04x  ", cpu->cd.x86.s[X86_S_CS],
			    (int)dumpaddr);
		else
			debug("%08x:  ", (int)dumpaddr);
	}

	/*
	 *  Decode the instruction:
	 */

	/*  All instructions are at least 1 byte long:  */
	HEXPRINT(instr,1);
	ilen = 1;

	/*  Any prefix?  */
	for (;;) {
		if (instr[0] == 0x66) {
			if (mode == 32)
				mode = 16;
			else
				mode = 32;
		} else if (instr[0] == 0xf3) {
			rep = 1;
		} else if (instr[0] == 0x26) {
			prefix = "es:";
		} else
			break;

		if (++n_prefix_bytes > 4) {
			SPACES; debug("more than 4 prefix bytes?\n");
			return 4;
		}

		/*  TODO: lock, segment overrides etc  */
		instr ++; ilen ++;
		debug("%02x", instr[0]);
	}

	if (mode == 16)
		e = "";

	op = instr[0];
	instr ++;

	if ((op & 0xf0) <= 0x30 && (op & 7) <= 5) {
		switch (op & 0x38) {
		case 0x00: mnem = "add"; break;
		case 0x08: mnem = "or"; break;
		case 0x10: mnem = "adc"; break;
		case 0x18: mnem = "sbb"; break;
		case 0x20: mnem = "and"; break;
		case 0x28: mnem = "sub"; break;
		case 0x30: mnem = "xor"; break;
		case 0x38: mnem = "cmp"; break;
		}
		switch (op & 7) {
		case 4:	imm = read_imm_and_print(&instr, &ilen, 8);
			SPACES; debug("%s\tal,0x%02x", mnem, imm);
			break;
		case 5:	imm = read_imm_and_print(&instr, &ilen, mode);
			SPACES; debug("%s\t%sax,0x%x", mnem, e, imm);
			break;
		default:
			modrm(cpu, MODRM_READ, mode, !(op & 1), &instr, &ilen,
			    NULL, NULL);
			SPACES; debug("%s\t", mnem);
			if (op & 2)
				debug("%s,%s", modrm_r, modrm_rm);
			else
				debug("%s,%s", modrm_rm, modrm_r);
		}
	} else if (op == 0xf) {
		/*  "pop cs" on 8086  */
		if (cpu->cd.x86.model.model_number == X86_MODEL_8086) {
			SPACES; debug("pop\tcs");
		} else {
			SPACES; debug("UNIMPLEMENTED 0x0f");
		}
	} else if (op < 0x20 && (op & 7) == 6) {
		SPACES; debug("push\t%s", seg_names[op/8]);
	} else if (op < 0x20 && (op & 7) == 7) {
		SPACES; debug("pop\t%s", seg_names[op/8]);
	} else if (op >= 0x20 && op < 0x40 && (op & 7) == 7) {
		SPACES; debug("%sa%s", op < 0x30? "d" : "a",
		    (op & 0xf)==7? "a" : "s");
	} else if (op >= 0x40 && op <= 0x5f) {
		switch (op & 0x38) {
		case 0x00: mnem = "inc"; break;
		case 0x08: mnem = "dec"; break;
		case 0x10: mnem = "push"; break;
		case 0x18: mnem = "pop"; break;
		}
		SPACES; debug("%s\t%s%s", mnem, e, reg_names[op & 7]);
	} else if (op == 0x60) {
		SPACES; debug("pusha");
	} else if (op == 0x61) {
		SPACES; debug("popa");
	} else if ((op & 0xf0) == 0x70) {
		imm = (signed char)read_imm_and_print(&instr, &ilen, 8);
		imm = dumpaddr + 2 + imm;
		SPACES; debug("j%s%s\t0x%x", op&1? "n" : "",
		    cond_names[(op/2) & 0x7], imm);
	} else if (op == 0x80 || op == 0x81) {
		/*  /0=add, /4=and  */
		switch ((*instr >> 3) & 0x7) {
		case 0:	mnem = "add"; break;
		case 4:	mnem = "and"; break;
		case 7:	mnem = "cmp"; break;
		default:
			SPACES; debug("UNIMPLEMENTED 0x%02x", op);
		}
		modrm(cpu, MODRM_READ, mode, op == 0x80? MODRM_EIGHTBIT : 0,
		    &instr, &ilen, NULL, NULL);
		imm = read_imm_and_print(&instr, &ilen, op==0x80? 8 : mode);
		SPACES; debug("%s\t%s,0x%x", mnem, modrm_rm, imm);
	} else if (op == 0x83) {
		/*  /0=add, /4=and  */
		switch ((*instr >> 3) & 0x7) {
		case 0:	mnem = "add"; break;
		case 4:	mnem = "and"; break;
		default:
			SPACES; debug("UNIMPLEMENTED 0x%02x", op);
		}
		modrm(cpu, MODRM_READ, mode, 0, &instr, &ilen, NULL, NULL);
		imm = (signed char)read_imm_and_print(&instr, &ilen, 8);
		SPACES; debug("%s\t%s,0x%x", mnem, modrm_rm, imm);
	} else if (op == 0x84 || op == 0x85) {
		modrm(cpu, MODRM_READ, mode, op == 0x84? MODRM_EIGHTBIT : 0,
		    &instr, &ilen, NULL, NULL);
		SPACES; debug("test\t%s,%s", modrm_rm, modrm_r);
	} else if (op == 0x88 || op == 0x89) {
		modrm(cpu, MODRM_READ, mode, op == 0x88? MODRM_EIGHTBIT : 0,
		    &instr, &ilen, NULL, NULL);
		SPACES; debug("mov\t%s,%s", modrm_rm, modrm_r);
	} else if (op == 0x8a || op == 0x8b) {
		modrm(cpu, MODRM_READ, mode, op == 0x8a? MODRM_EIGHTBIT : 0,
		    &instr, &ilen, NULL, NULL);
		SPACES; debug("mov\t%s,%s", modrm_r, modrm_rm);
	} else if (op == 0x8c || op == 0x8e) {
		modrm(cpu, MODRM_READ, mode, MODRM_SEG, &instr, &ilen,
		    NULL, NULL);
		SPACES; debug("mov\t");
		if (op == 0x8c)
			debug("%s,%s", modrm_rm, modrm_r);
		else
			debug("%s,%s", modrm_r, modrm_rm);
	} else if (op == 0x90) {
		SPACES; debug("nop");
	} else if (op >= 0x91 && op <= 0x97) {
		SPACES; debug("xchg\t%sax,%s%s", e, e, reg_names[op & 7]);
	} else if (op == 0x98) {
		SPACES; debug("cbw");
	} else if (op == 0x99) {
		SPACES; debug("cwd");
	} else if (op == 0x9b) {
		SPACES; debug("wait");
	} else if (op == 0x9c) {
		SPACES; debug("pushf");
	} else if (op == 0x9d) {
		SPACES; debug("popf");
	} else if (op == 0x9e) {
		SPACES; debug("sahf");
	} else if (op == 0x9f) {
		SPACES; debug("lahf");
	} else if (op == 0xa2) {
		imm = read_imm_and_print(&instr, &ilen, mode);
		SPACES; debug("mov\t[0x%x],al", imm);
	} else if (op == 0xa3) {
		imm = read_imm_and_print(&instr, &ilen, mode);
		SPACES; debug("mov\t[0x%x],%sax", imm, e);
	} else if (op == 0xa4) {
		SPACES; debug("movsb");
	} else if (op == 0xa5) {
		SPACES; debug("movs%s", mode==16? "w" : (mode==32? "d" : "q"));
	} else if (op == 0xaa) {
		SPACES; debug("stosb");
	} else if (op == 0xab) {
		SPACES; debug("stos%s", mode==16? "w" : (mode==32? "d" : "q"));
	} else if (op == 0xac) {
		SPACES; debug("lodsb");
	} else if (op == 0xad) {
		SPACES; debug("lods%s", mode==16? "w" : (mode==32? "d" : "q"));
	} else if (op >= 0xb0 && op <= 0xb7) {
		imm = read_imm_and_print(&instr, &ilen, 8);
		switch (op & 7) {
		case 0: tmp = "al"; break;
		case 1: tmp = "cl"; break;
		case 2: tmp = "dl"; break;
		case 3: tmp = "bl"; break;
		case 4: tmp = "ah"; break;
		case 5: tmp = "ch"; break;
		case 6: tmp = "dh"; break;
		case 7: tmp = "bh"; break;
		}
		SPACES; debug("mov\t%s,0x%x", tmp, imm);
	} else if (op >= 0xb8 && op <= 0xbf) {
		imm = read_imm_and_print(&instr, &ilen, mode);
		SPACES; debug("mov\t%s%s,0x%x", e, reg_names[op & 7], imm);
	} else if (op == 0xc3) {
		SPACES; debug("ret");
	} else if (op == 0xc9) {
		SPACES; debug("leave");
	} else if (op == 0xcb) {
		SPACES; debug("retf");
	} else if (op == 0xcc) {
		SPACES; debug("int3");
	} else if (op == 0xcd) {
		imm = read_imm_and_print(&instr, &ilen, 8);
		SPACES; debug("int\t0x%x", imm);
	} else if (op == 0xce) {
		SPACES; debug("into");
	} else if (op == 0xcf) {
		SPACES; debug("iret");
	} else if (op == 0xd0) {
		switch ((*instr >> 3) & 0x7) {
		case 5:	modrm(cpu, MODRM_READ, mode, MODRM_EIGHTBIT,
			    &instr, &ilen, NULL, NULL);
			SPACES; debug("shr\t%s,1", modrm_rm);
			break;
		default:
			SPACES; debug("UNIMPLEMENTED 0x%02x", op);
		}
	} else if (op == 0xd1) {
		switch ((*instr >> 3) & 0x7) {
		case 5:	modrm(cpu, MODRM_READ, mode, 0, &instr, &ilen,
			    NULL, NULL);
			SPACES; debug("shr\t%s,1", modrm_rm);
			break;
		default:
			SPACES; debug("UNIMPLEMENTED 0x%02x", op);
		}
	} else if (op == 0xd2) {
		switch ((*instr >> 3) & 0x7) {
		case 4:	modrm(cpu, MODRM_READ, mode, MODRM_EIGHTBIT,
			    &instr, &ilen, NULL, NULL);
			SPACES; debug("shl\t%s,cl", modrm_rm);
			break;
		case 5:	modrm(cpu, MODRM_READ, mode, MODRM_EIGHTBIT,
			    &instr, &ilen, NULL, NULL);
			SPACES; debug("shr\t%s,cl", modrm_rm);
			break;
		default:
			SPACES; debug("UNIMPLEMENTED 0x%02x", op);
		}
	} else if (op == 0xd3) {
		switch ((*instr >> 3) & 0x7) {
		case 4:	modrm(cpu, MODRM_READ, mode, 0, &instr, &ilen,
			    NULL, NULL);
			SPACES; debug("shl\t%s,cl", modrm_rm);
			break;
		case 5:	modrm(cpu, MODRM_READ, mode, 0, &instr, &ilen,
			    NULL, NULL);
			SPACES; debug("shr\t%s,cl", modrm_rm);
			break;
		default:
			SPACES; debug("UNIMPLEMENTED 0x%02x", op);
		}
	} else if (op == 0xd4) {
		SPACES; debug("aam");
	} else if (op == 0xd5) {
		SPACES; debug("aad");
	} else if (op == 0xd7) {
		SPACES; debug("xlat");
	} else if (op == 0xe8 || op == 0xe9) {
		imm = read_imm_and_print(&instr, &ilen, mode);
		if (mode == 16)
			imm = (int16_t)imm;
		imm = dumpaddr + ilen + imm;
		switch (op) {
		case 0xe8: mnem = "call"; break;
		case 0xe9: mnem = "jmp"; break;
		}
		SPACES; debug("%s\t0x%x", mnem, imm);
	} else if (op == 0xea) {
		imm = read_imm_and_print(&instr, &ilen, mode);
		imm2 = read_imm_and_print(&instr, &ilen, 16);
		SPACES; debug("jmp\t0x%04x:", imm2);
		if (mode == 16)
			debug("0x%04x", imm);
		else
			debug("0x%08x", imm);
	} else if (op == 0xeb) {
		imm = read_imm_and_print(&instr, &ilen, 8);
		imm = dumpaddr + ilen + (signed char)imm;
		SPACES; debug("jmp\t0x%x", imm);
	} else if (op == 0xf4) {
		SPACES; debug("hlt");
	} else if (op == 0xf5) {
		SPACES; debug("cmc");
	} else if (op == 0xf8) {
		SPACES; debug("clc");
	} else if (op == 0xf9) {
		SPACES; debug("stc");
	} else if (op == 0xfa) {
		SPACES; debug("cli");
	} else if (op == 0xfb) {
		SPACES; debug("sti");
	} else if (op == 0xfc) {
		SPACES; debug("cld");
	} else if (op == 0xfd) {
		SPACES; debug("std");
	} else if (op == 0xf6 || op == 0xf7) {
		/*  F6 /4 = mul  */
		/*  F6 /6 = div  */
		switch ((*instr >> 3) & 0x7) {
		case 4:	modrm(cpu, MODRM_READ, mode, op == 0xf6?
			    MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
			SPACES; debug("mul\t%s", modrm_rm);
			break;
		case 6:	modrm(cpu, MODRM_READ, mode, op == 0xf6?
			    MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
			SPACES; debug("div\t%s", modrm_rm);
			break;
		default:
			SPACES; debug("UNIMPLEMENTED 0x%02x", op);
		}
	} else if (op == 0xfe || op == 0xff) {
		/*  FE /0 = inc r/m8 */
		/*  FE /1 = dec r/m8 */
		switch ((*instr >> 3) & 0x7) {
		case 0:	modrm(cpu, MODRM_READ, mode, op == 0xfe?
			    MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
			SPACES; debug("inc\t%s", modrm_rm);
			break;
		case 1:	modrm(cpu, MODRM_READ, mode, op == 0xfe?
			    MODRM_EIGHTBIT : 0, &instr, &ilen, NULL, NULL);
			SPACES; debug("dec\t%s", modrm_rm);
			break;
		default:
			SPACES; debug("UNIMPLEMENTED 0x%02x", op);
		}
	} else {
		SPACES; debug("UNIMPLEMENTED 0x%02x", op);
	}

	if (rep)
		debug(" (rep)");
	if (prefix != NULL)
		debug(" (%s)", prefix);

	debug("\n");
	return ilen;
}


#define MEMORY_RW	x86_memory_rw
#define MEM_X86
#include "memory_rw.c"
#undef MEM_X86
#undef MEMORY_RW


/*
 *  x86_push():
 */
static int x86_push(struct cpu *cpu, uint64_t value)
{
	int res = 1;

	cpu->cd.x86.r[X86_R_SP] -= (cpu->cd.x86.mode / 8);
	res = x86_store(cpu, cpu->cd.x86.r[X86_R_SP], value,
	    cpu->cd.x86.mode / 8);
	return res;
}


/*
 *  x86_pop():
 */
static int x86_pop(struct cpu *cpu, uint64_t *valuep)
{
	int res = 1;

	res = x86_load(cpu, cpu->cd.x86.r[X86_R_SP], valuep,
	    cpu->cd.x86.mode / 8);
	cpu->cd.x86.r[X86_R_SP] += (cpu->cd.x86.mode / 8);
	return res;
}


/*
 *  x86_interrupt():
 *
 *  NOTE/TODO: Only for 16-bit mode so far.
 */
static int x86_interrupt(struct cpu *cpu, int nr)
{
	uint64_t seg, ofs;
	const int len = sizeof(uint16_t);

	if (cpu->cd.x86.mode != 16) {
		fatal("x86 'int' only implemented for 16-bit so far\n");
		exit(1);
	}

	/*  Read the interrupt vector from beginning of RAM:  */
	cpu->cd.x86.cursegment = 0;
	x86_load(cpu, nr * 4 + 0, &ofs, sizeof(uint16_t));
	x86_load(cpu, nr * 4 + 2, &seg, sizeof(uint16_t));

	/*  Push flags, cs, and ip (pc):  */
	cpu->cd.x86.cursegment = cpu->cd.x86.s[X86_S_SS];
	if (x86_store(cpu, cpu->cd.x86.r[X86_R_SP] - len * 1,
	    cpu->cd.x86.rflags, len) != MEMORY_ACCESS_OK)
		fatal("x86_interrupt(): TODO: how to handle this\n");
	if (x86_store(cpu, cpu->cd.x86.r[X86_R_SP] - len * 2,
	    cpu->cd.x86.s[X86_S_CS], len) != MEMORY_ACCESS_OK)
		fatal("x86_interrupt(): TODO: how to handle this\n");
	if (x86_store(cpu, cpu->cd.x86.r[X86_R_SP] - len * 3, cpu->pc,
	    len) != MEMORY_ACCESS_OK)
		fatal("x86_interrupt(): TODO: how to handle this\n");

	cpu->cd.x86.r[X86_R_SP] = (cpu->cd.x86.r[X86_R_SP] & ~0xffff)
	    | ((cpu->cd.x86.r[X86_R_SP] - len*3) & 0xffff);

	/*  TODO: clear the Interrupt Flag?  */

	cpu->cd.x86.s[X86_S_CS] = seg;
	cpu->pc = ofs;

	return 1;
}


/*
 *  x86_cmp():
 */
static void x86_cmp(struct cpu *cpu, uint64_t a, uint64_t b)
{
	if (a == b)
		cpu->cd.x86.rflags |= X86_FLAGS_ZF;
	else
		cpu->cd.x86.rflags &= ~X86_FLAGS_ZF;

	if (a < b)
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
	else
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;

	/*  TODO: other bits?  */
}


/*
 *  x86_test():
 */
static void x86_test(struct cpu *cpu, uint64_t a, uint64_t b)
{
	a &= b;

	if (a == 0)
		cpu->cd.x86.rflags |= X86_FLAGS_ZF;
	else
		cpu->cd.x86.rflags &= ~X86_FLAGS_ZF;

	if ((int32_t)a < 0)
		cpu->cd.x86.rflags |= X86_FLAGS_SF;
	else
		cpu->cd.x86.rflags &= ~X86_FLAGS_SF;

	cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
	cpu->cd.x86.rflags &= ~X86_FLAGS_OF;
	/*  TODO: PF  */
}


/*
 *  x86_cpu_run_instr():
 *
 *  Execute one instruction on a specific CPU.
 *
 *  Return value is the number of instructions executed during this call,   
 *  0 if no instruction was executed.
 */
int x86_cpu_run_instr(struct emul *emul, struct cpu *cpu)
{
	int i, r, rep = 0, op, len, diff, mode = cpu->cd.x86.mode;
	int mode_addr = mode, nprefixbytes = 0, success;
	uint32_t imm, imm2, value;
	unsigned char buf[16];
	unsigned char *instr = buf, *instr_orig;
	uint64_t newpc = cpu->pc;
	unsigned char databuf[8];
	uint64_t tmp, op1, op2;

	/*  Check PC against breakpoints:  */
	if (!single_step)
		for (i=0; i<cpu->machine->n_breakpoints; i++)
			if (cpu->pc == cpu->machine->breakpoint_addr[i]) {
				fatal("Breakpoint reached, pc=0x%llx",
				    (long long)cpu->pc);
				single_step = 1;
				return 0;
			}

	/*  16-bit BIOS emulation:  */
	if (mode == 16 && ((newpc + (cpu->cd.x86.s[X86_S_CS] << 4)) & 0xff000)
	    == 0xf8000 && cpu->machine->prom_emulation) {
		pc_bios_emul(cpu);
		return 1;
	}

	/*  Read an instruction from memory:  */
	cpu->cd.x86.cursegment = cpu->cd.x86.s[X86_S_CS];

	r = cpu->memory_rw(cpu, cpu->mem, cpu->pc, &buf[0], sizeof(buf),
	    MEM_READ, CACHE_INSTRUCTION);
	if (!r)
		return 0;

	if (cpu->machine->instruction_trace)
		x86_cpu_disassemble_instr(cpu, instr, 1, 0, 0);

	/*  All instructions are at least one byte long :-)  */
	newpc ++;

	/*  Default is to use the data segment, or the stack segment:  */
	cpu->cd.x86.cursegment = cpu->cd.x86.s[X86_S_DS];

	/*  Any prefix?  */
	for (;;) {
		if (instr[0] == 0x66) {
			if (mode == 16)
				mode = 32;
			else
				mode = 16;
		} else if (instr[0] == 0x67) {
			if (mode_addr == 16)
				mode_addr = 32;
			else
				mode_addr = 16;
		} else if (instr[0] == 0x26)
			cpu->cd.x86.cursegment = cpu->cd.x86.s[X86_S_ES];
		else if (instr[0] == 0xf3)
			rep = 1;
		else
			break;
		/*  TODO: repnz, lock etc  */
		instr ++;
		newpc ++;
		if (++nprefixbytes > 4) {
			fatal("x86: too many prefix bytes at ");
			print_csip(cpu); fatal("\n");
			cpu->running = 0;
			return 0;
		}
	}

	op = instr[0];
	instr ++;

	if ((op & 0xf0) <= 0x30 && (op & 7) <= 5) {
		success = 1;
		instr_orig = instr;
		switch (op & 7) {
		case 4:	imm = read_imm(&instr, &newpc, 8);
			op1 = cpu->cd.x86.r[X86_R_AX]; op2 = (signed char)imm;
			mode = 8;
			break;
		case 5:	imm = read_imm(&instr, &newpc, mode);
			op1 = cpu->cd.x86.r[X86_R_AX]; op2 = imm;
			break;
		default:
			success = modrm(cpu, MODRM_READ, mode, !(op & 1),
			    &instr, &newpc, &op1, &op2);
			if (!success)
				return 0;
		}

		if ((op & 6) == 2) {
			uint64_t tmp = op1; op1 = op2; op2 = tmp;
		}

		/*  printf("op1=0x%x op2=0x%x => ", (int)op1, (int)op2);  */

		switch (op & 0x38) {
		case 0x00:	op1 = op1 + op2; break;
		case 0x08:	op1 = op1 | op2; break;
		case 0x10:	op1 = op1 + op2;
				if (cpu->cd.x86.rflags & X86_FLAGS_CF)
					op1 ++;
				break;
		case 0x18:	op1 = op1 - op2;
				if (cpu->cd.x86.rflags & X86_FLAGS_CF)
					op1 --;
				break;
		case 0x20:	op1 = op1 & op2; break;
		case 0x28:	op1 = op1 - op2; break;
		case 0x30:	op1 = op1 ^ op2; break;
		case 0x38:	break;
		default:
			fatal("not yet\n");
			exit(1);
		}

		switch (mode) {
		case 16: op1 &= 0xffff; op2 &= 0xffff; break;
		case 32: op1 &= 0xffffffffULL; op2 &= 0xffffffffULL; break;
		}

		x86_cmp(cpu, op1, op2);

		/*  printf("op1=0x%x op2=0x%x\n", (int)op1, (int)op2);  */

		if ((op & 6) == 2) {
			uint64_t tmp = op1; op1 = op2; op2 = tmp;
		}

		/*  Write back the result: (for all cases except CMP)  */
		if ((op & 0x38) != 0x38) {
			switch (op & 7) {
			case 4:
				cpu->cd.x86.r[X86_R_AX] = (cpu->cd.x86.r[
				    X86_R_AX] & ~0xff) | (op1 & 0xff);
				break;
			case 5:
				cpu->cd.x86.r[X86_R_AX] = modify(cpu->
				    cd.x86.r[X86_R_AX], op1);
				break;
			default:
				success = modrm(cpu, (op & 6) == 2?
				    MODRM_WRITE_R : MODRM_WRITE_RM, mode,
				    !(op & 1), &instr_orig, NULL, &op1, &op2);
				if (!success)
					return 0;
			}
		}
	} else if ((op & 0xf0) < 0x20 && (op & 7) == 6) {
		success = x86_push(cpu, cpu->cd.x86.s[op / 8]);
		if (!success)
			return 0;
	} else if (op == 0x0f) {
		fatal("TODO: 0x0f\n");
		exit(1);
	} else if ((op & 0xf0) < 0x20 && (op & 7) == 7) {
		uint64_t tmp;
		success = x86_pop(cpu, &tmp);
		if (!success)
			return 0;
		cpu->cd.x86.s[op / 8] = tmp;
	} else if (op >= 0x40 && op <= 0x4f) {
		if (op < 0x48)
			cpu->cd.x86.r[op & 7] = modify(cpu->cd.x86.r[op & 7],
			    cpu->cd.x86.r[op & 7] + 1);
		else
			cpu->cd.x86.r[op & 7] = modify(cpu->cd.x86.r[op & 7],
			    cpu->cd.x86.r[op & 7] - 1);
		cpu->cd.x86.rflags &= ~X86_FLAGS_ZF;
		if (mode != 16 && cpu->cd.x86.r[op & 7] == 0)
			cpu->cd.x86.rflags |= X86_FLAGS_ZF;
		if (mode == 16 && (cpu->cd.x86.r[op & 7] & 0xffff) == 0)
			cpu->cd.x86.rflags |= X86_FLAGS_ZF;
		/*  TODO: more flags: SF,AF,PF  */
	} else if (op >= 0x50 && op <= 0x57) {
		if (!x86_push(cpu, cpu->cd.x86.r[op & 7]))
			return 0;
	} else if (op >= 0x58 && op <= 0x5f) {
		uint64_t tmp;
		success = x86_pop(cpu, &tmp);
		if (!success)
			return 0;
		cpu->cd.x86.r[op & 7] = tmp;
	} else if ((op & 0xf0) == 0x70) {
		success = 0;
		imm = read_imm(&instr, &newpc, 8);
		switch (op & 0xe) {
		case 0x00:	/*  o  */
			success = cpu->cd.x86.rflags & X86_FLAGS_OF;
			break;
		case 0x02:	/*  c  */
			success = cpu->cd.x86.rflags & X86_FLAGS_CF;
			break;
		case 0x04:	/*  z  */
			success = cpu->cd.x86.rflags & X86_FLAGS_ZF;
			break;
		case 0x06:	/*  be  */
			success = (cpu->cd.x86.rflags & X86_FLAGS_ZF) ||
			    (cpu->cd.x86.rflags & X86_FLAGS_CF);
			break;
		case 0x08:	/*  s  */
			success = cpu->cd.x86.rflags & X86_FLAGS_SF;
			break;
		case 0x0a:	/*  p  */
			success = cpu->cd.x86.rflags & X86_FLAGS_PF;
			break;
		case 0x0c:	/*  nge  */
			success = (cpu->cd.x86.rflags & X86_FLAGS_SF? 1 : 0)
			    != (cpu->cd.x86.rflags & X86_FLAGS_OF? 1 : 0);
			break;
		case 0x0e:	/*  ng  */
			success = (cpu->cd.x86.rflags & X86_FLAGS_SF? 1 : 0)
			    != (cpu->cd.x86.rflags & X86_FLAGS_OF? 1 : 0);
			success |= (cpu->cd.x86.rflags & X86_FLAGS_ZF ? 1 : 0);
			break;
		default:
			fatal("unimplemented condition\n");
			exit(1);
		}
		if (op & 1)
			success = !success;
		if (success)
			newpc = modify(newpc, newpc + (signed char)imm);
	} else if (op == 0x80 || op == 0x81) {	/*  add/and r/m, imm  */
		instr_orig = instr;
		success = modrm(cpu, MODRM_READ, mode, op == 0x80?
		    MODRM_EIGHTBIT : 0, &instr, &newpc, &op1, &op2);
		if (!success)
			return 0;
		imm = read_imm(&instr, &newpc, op==0x80? 8 : mode);
		switch ((*instr_orig >> 3) & 0x7) {
		case 0:	op1 += imm; break;
		case 4:	op1 &= imm; break;
		case 7:	break;	/*  cmp  */
		default:
			fatal("UNIMPLEMENTED 0x%02x,0x%02x", op, *instr_orig);
			exit(1);
		}
		x86_cmp(cpu, op1, imm);
		if (((*instr_orig >> 3) & 0x7) != 7) {
			success = modrm(cpu, MODRM_WRITE_RM, mode, op == 0x80?
			    MODRM_EIGHTBIT : 0, &instr_orig, NULL, &op1, &op2);
			if (!success)
				return 0;
		}
	} else if (op == 0x83) {	/*  add/and r/m1632, imm8  */
		instr_orig = instr;
		success = modrm(cpu, MODRM_READ, mode, 0, &instr,
		    &newpc, &op1, &op2);
		if (!success)
			return 0;
		imm = read_imm(&instr, &newpc, 8);
		switch ((*instr_orig >> 3) & 0x7) {
		case 0:	op1 += (signed char)imm; break;
		case 4:	op1 &= (signed char)imm; break;
		default:
			fatal("UNIMPLEMENTED 0x%02x,0x%02x", op, *instr_orig);
			exit(1);
		}
		success = modrm(cpu, MODRM_WRITE_RM,
		    mode, 0, &instr_orig, NULL, &op1, &op2);
		if (!success)
			return 0;
	} else if (op == 0x84 || op == 0x85) {		/*  test  */
		success = modrm(cpu, MODRM_READ, mode, op == 0x84?
		    MODRM_EIGHTBIT : 0, &instr, &newpc, &op1, &op2);
		if (!success)
			return 0;
		op1 &= op2;
		if (op == 0x84)
			op1 &= 0xff;
		x86_cmp(cpu, op1, 0);
	} else if (op >= 0x88 && op <= 0x8b) {
		instr_orig = instr;
		success = modrm(cpu, MODRM_READ, mode, (op & 1) == 0?
		    MODRM_EIGHTBIT : 0, &instr, &newpc, &op1, &op2);
		if (!success)
			return 0;
		if (op < 0x8a) {
			op1 = op2;
			success = modrm(cpu, MODRM_WRITE_RM,
			    mode, op == 0x88? MODRM_EIGHTBIT : 0, &instr_orig,
			    NULL, &op1, &op2);
		} else {
			op2 = op1;
			success = modrm(cpu, MODRM_WRITE_R,
			    mode, op == 0x8a? MODRM_EIGHTBIT : 0, &instr_orig,
			    NULL, &op1, &op2);
		}
		if (!success)
			return 0;
	} else if (op == 0x8c || op == 0x8e) {
		instr_orig = instr;
		success = modrm(cpu, MODRM_READ, mode, MODRM_SEG, &instr,
		    &newpc, &op1, &op2);
		if (!success)
			return 0;
		if (op == 0x8c)
			op1 = op2;
		else
			op2 = op1;
		success = modrm(cpu, op == 0x8e? MODRM_WRITE_R : MODRM_WRITE_RM,
		    mode, MODRM_SEG, &instr_orig, NULL, &op1, &op2);
		if (!success)
			return 0;
	} else if (op == 0x90) {		/*  NOP  */
	} else if (op >= 0x91 && op <= 0x97) {	/*  XCHG  */
		uint64_t tmp;
		if (mode == 16) {
			tmp = cpu->cd.x86.r[X86_R_AX];
			cpu->cd.x86.r[X86_R_AX] = modify(
			    cpu->cd.x86.r[X86_R_AX], cpu->cd.x86.r[op & 7]);
			cpu->cd.x86.r[op & 7] = modify(
			    cpu->cd.x86.r[op & 7], tmp);
		} else {
			tmp = cpu->cd.x86.r[X86_R_AX];
			cpu->cd.x86.r[X86_R_AX] = cpu->cd.x86.r[op & 7];
			cpu->cd.x86.r[op & 7] = tmp;
		}
	} else if (op == 0x9c) {		/*  PUSHF  */
		if (!x86_push(cpu, cpu->cd.x86.rflags))
			return 0;
	} else if (op == 0x9d) {		/*  POPF  */
		uint64_t tmp;
		if (!x86_pop(cpu, &tmp))
			return 0;
		cpu->cd.x86.rflags = tmp;
		/*  TODO: only affect some bits?  */
	} else if (op == 0xa2) {		/*  MOV [addr],AL  */
		imm = read_imm(&instr, &newpc, mode);
		if (!x86_store(cpu, imm, cpu->cd.x86.r[X86_R_AX], 1))
			return 0;
	} else if (op == 0xa3) {		/*  MOV [addr],AX  */
		imm = read_imm(&instr, &newpc, mode);
		if (!x86_store(cpu, imm, cpu->cd.x86.r[X86_R_AX], mode/8))
			return 0;
	} else if (op == 0xa4 || op == 0xa5 ||		/*  MOVS  */
	    op == 0xaa || op == 0xab ||			/*  STOS  */
	    op == 0xac || op == 0xad) {			/*  LODS  */
		int dir = 1, movs = 1, lods = 0;

		len = 1;
		if (op & 1)
			len = mode / 8;
		if (op >= 0xaa)
			movs = 0;
		if (op >= 0xac)
			lods = 1;
		if (cpu->cd.x86.rflags & X86_FLAGS_DF)
			dir = -1;

		do {
			uint64_t value;

			if (movs || lods) {
				cpu->cd.x86.cursegment =
				    cpu->cd.x86.s[X86_S_DS];
				if (!x86_load(cpu, cpu->cd.x86.r[X86_R_SI],
				    &value, len))
					return 0;
			} else
				value = cpu->cd.x86.r[X86_R_AX];
			if (lods)
				cpu->cd.x86.r[X86_R_AX] = value;

			if (!lods) {
				cpu->cd.x86.cursegment = cpu->cd.x86.s[
				    X86_S_ES];
				if (!x86_store(cpu, cpu->cd.x86.r[X86_R_DI],
				    value, len))
					return 0;
			}

			if (movs || lods) {
				/*  Modify esi:  */
				cpu->cd.x86.r[X86_R_SI] = modify(cpu->cd.
				    x86.r[X86_R_SI], cpu->cd.x86.r[X86_R_SI]
				    + len*dir);
			}

			if (!lods) {
				/*  Modify edi:  */
				cpu->cd.x86.r[X86_R_DI] = modify(cpu->cd.x86.r[
				    X86_R_DI], cpu->cd.x86.r[X86_R_DI] +
				    len*dir);
			}

			if (rep) {
				/*  Decrement ecx:  */
				cpu->cd.x86.r[X86_R_CX] = modify(cpu->cd.x86.r[
				    X86_R_CX], cpu->cd.x86.r[X86_R_CX] - 1);
				if (mode == 16 && (cpu->cd.x86.r[X86_R_CX] &
				    0xffff) == 0)
					rep = 0;
				if (mode != 16 && cpu->cd.x86.r[X86_R_CX] == 0)
					rep = 0;
			}
		} while (rep);
	} else if (op >= 0xb0 && op <= 0xb3) {
		imm = read_imm(&instr, &newpc, 8);
		cpu->cd.x86.r[op & 3] = (cpu->cd.x86.r[op & 3] & ~0xff)
		    | (imm & 0xff);
	} else if (op >= 0xb4 && op <= 0xb7) {
		imm = read_imm(&instr, &newpc, 8);
		cpu->cd.x86.r[op & 3] = (cpu->cd.x86.r[op & 3] & ~0xff00)
		    | ((imm & 0xff) << 8);
	} else if (op >= 0xb8 && op <= 0xbf) {
		imm = read_imm(&instr, &newpc, mode);
		cpu->cd.x86.r[op & 7] = imm;
	} else if (op == 0xc3) {	/*  RET near  */
		uint64_t tmp;
		success = x86_pop(cpu, &tmp);
		if (!success)
			return 0;
		newpc = modify(newpc, tmp);
	} else if (op == 0xcc) {	/*  INT3  */
		cpu->pc = newpc;
		return x86_interrupt(cpu, 3);
	} else if (op == 0xcd) {	/*  INT  */
		imm = read_imm(&instr, &newpc, 8);
		cpu->pc = newpc;
		return x86_interrupt(cpu, imm);
	} else if (op == 0xd0) {
		switch ((*instr >> 3) & 0x7) {
		case 5:	/*  shr op1,1  */
			instr_orig = instr;
			success = modrm(cpu, MODRM_READ, mode, MODRM_EIGHTBIT,
			    &instr, &newpc, &op1, &op2);
			if (!success)
				return 0;
			cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
			if (op1 & 1)
				cpu->cd.x86.rflags |= X86_FLAGS_CF;
			op1 >>= 1;
			success = modrm(cpu, MODRM_WRITE_RM, mode,
			    MODRM_EIGHTBIT, &instr_orig, NULL, &op1, &op2);
			if (!success)
				return 0;
			break;
		default:
			fatal("UNIMPLEMENTED 0x%02x, 0x%02x", op, *instr);
			exit(1);
		}
	} else if (op == 0xd1) {
		switch ((*instr >> 3) & 0x7) {
		case 5:	/*  shr op1,1  */
			instr_orig = instr;
			success = modrm(cpu, MODRM_READ, mode, 0, &instr,
			    &newpc, &op1, &op2);
			if (!success)
				return 0;
			cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
			if (op1 & 1)
				cpu->cd.x86.rflags |= X86_FLAGS_CF;
			op1 >>= 1;
			success = modrm(cpu, MODRM_WRITE_RM, mode,
			    0, &instr_orig, NULL, &op1, &op2);
			if (!success)
				return 0;
			break;
		default:
			fatal("UNIMPLEMENTED 0x%02x, 0x%02x", op, *instr);
			exit(1);
		}
	} else if (op == 0xd2) {
		switch ((*instr >> 3) & 0x7) {
		case 4:	/*  shl op1,cl  */
		case 5:	/*  shr op1,cl  */
			instr_orig = instr;
			success = modrm(cpu, MODRM_READ, mode, MODRM_EIGHTBIT,
			    &instr, &newpc, &op1, &op2);
			if (!success)
				return 0;
			if (((*instr >> 3) & 0x7) == 4)
				op1 <<= (cpu->cd.x86.r[X86_R_CX] & 31);
			else
				op1 >>= (cpu->cd.x86.r[X86_R_CX] & 31);
			success = modrm(cpu, MODRM_WRITE_RM, mode,
			    MODRM_EIGHTBIT, &instr_orig, NULL, &op1, &op2);
			if (!success)
				return 0;
			break;
		default:
			fatal("UNIMPLEMENTED 0x%02x, 0x%02x", op, *instr);
			exit(1);
		}
	} else if (op == 0xd3) {
		switch ((*instr >> 3) & 0x7) {
		case 4:	/*  shl op1,cl  */
		case 5:	/*  shr op1,cl  */
			instr_orig = instr;
			success = modrm(cpu, MODRM_READ, mode, 0, &instr,
			    &newpc, &op1, &op2);
			if (!success)
				return 0;
			if (((*instr >> 3) & 0x7) == 4)
				op1 <<= (cpu->cd.x86.r[X86_R_CX] & 31);
			else
				op1 >>= (cpu->cd.x86.r[X86_R_CX] & 31);
			success = modrm(cpu, MODRM_WRITE_RM, mode,
			    0, &instr_orig, NULL, &op1, &op2);
			if (!success)
				return 0;
			break;
		default:
			fatal("UNIMPLEMENTED 0x%02x, 0x%02x", op, *instr);
			exit(1);
		}
	} else if (op == 0xe8 || op == 0xe9) {	/*  CALL/JMP near  */
		imm = read_imm(&instr, &newpc, mode);
		if (mode == 16)
			imm = (int16_t)imm;
		success = x86_push(cpu, newpc);
		if (!success)
			return 0;
		newpc = modify(newpc, newpc + imm);
	} else if (op == 0xea) {	/*  JMP seg:ofs  */
		imm = read_imm(&instr, &newpc, mode);
		imm2 = read_imm(&instr, &newpc, 16);
		cpu->cd.x86.s[X86_S_CS] = imm2;
		newpc = modify(cpu->pc, imm);
	} else if (op == 0xeb) {	/*  JMP short  */
		imm = read_imm(&instr, &newpc, 8);
		newpc = modify(newpc, newpc + (signed char)imm);
	} else if (op == 0xf5) {	/*  CMC  */
		cpu->cd.x86.rflags ^= X86_FLAGS_CF;
	} else if (op == 0xf8) {	/*  CLC  */
		cpu->cd.x86.rflags &= ~X86_FLAGS_CF;
	} else if (op == 0xf9) {	/*  STC  */
		cpu->cd.x86.rflags |= X86_FLAGS_CF;
	} else if (op == 0xfa) {	/*  CLI  */
		cpu->cd.x86.rflags &= ~X86_FLAGS_IF;
	} else if (op == 0xfb) {	/*  STI  */
		cpu->cd.x86.rflags |= X86_FLAGS_IF;
	} else if (op == 0xfc) {	/*  CLD  */
		cpu->cd.x86.rflags &= ~X86_FLAGS_DF;
	} else if (op == 0xfd) {	/*  STD  */
		cpu->cd.x86.rflags |= X86_FLAGS_DF;
	} else if (op == 0xf6 || op == 0xf7) {		/*  MUL, DIV etc  */
		uint64_t res;
		/*  F6 /4 = mul  */
		switch ((*instr >> 3) & 0x7) {
		case 4:	success = modrm(cpu, MODRM_READ, mode, op == 0xf6?
			    MODRM_EIGHTBIT : 0, &instr, &newpc, &op1, &op2);
			if (!success)
				return 0;
			if (op == 0xf6) {
				res = (cpu->cd.x86.r[X86_R_AX] & 0xff) * (op1
				    & 0xff);
				cpu->cd.x86.r[X86_R_AX] = (cpu->cd.x86.r[
				    X86_R_AX] & 0xffff) | (res & 0xffff);
			} else if (mode == 16) {
				res = (cpu->cd.x86.r[X86_R_AX] & 0xffff)
				    * (op1 & 0xffff);
				cpu->cd.x86.r[X86_R_AX] = modify(cpu->
				    cd.x86.r[X86_R_AX], res & 0xffff);
				cpu->cd.x86.r[X86_R_DX] = modify(cpu->cd.x86
				    .r[X86_R_DX], (res>>16) & 0xffff);
			} else if (mode == 32) {
				res = (cpu->cd.x86.r[X86_R_AX] & 0xffffffff)
				    * (op1 & 0xffffffff);
				cpu->cd.x86.r[X86_R_AX] = res & 0xffffffff;
				cpu->cd.x86.r[X86_R_DX] = (res >> 32) &
				    0xffffffff;
			}
			break;
		case 6:	success = modrm(cpu, MODRM_READ, mode, op == 0xf6?
			    MODRM_EIGHTBIT : 0, &instr, &newpc, &op1, &op2);
			if (!success)
				return 0;
			if (op1 == 0) {
				fatal("TODO: division by zero\n");
				exit(1);
			}
			if (op == 0xf6) {
				int al = (cpu->cd.x86.r[X86_R_AX] & 0xffff)
				    / op1;
				int ah = (cpu->cd.x86.r[X86_R_AX] & 0xffff)
				    % op1;
				cpu->cd.x86.r[X86_R_AX] = modify(
				    cpu->cd.x86.r[X86_R_AX], ah<<8 + al);
			} else if (mode == 16) {
				uint64_t a = (cpu->cd.x86.r[X86_R_AX] & 0xffff)
				    + ((cpu->cd.x86.r[X86_R_DX] & 0xffff)<<16);
				int ax = a / op1, dx = a % op1;
				cpu->cd.x86.r[X86_R_AX] = modify(
				    cpu->cd.x86.r[X86_R_AX], ax);
				cpu->cd.x86.r[X86_R_DX] = modify(
				    cpu->cd.x86.r[X86_R_DX], dx);
			} else if (mode == 32) {
				fatal("Todo: 32bitdiv\n");
				exit(1);
			}
			break;
		default:
			fatal("UNIMPLEMENTED 0x%02x,0x%02x", op, *instr);
		}
	} else if (op == 0xfe || op == 0xff) {		/*  INC, DEC etc  */
		switch ((*instr >> 3) & 0x7) {
		case 0:
		case 1:	instr_orig = instr;
			success = modrm(cpu, MODRM_READ, mode, op == 0xfe?
			    MODRM_EIGHTBIT : 0, &instr, &newpc, &op1, &op2);
			if (!success)
				return 0;
			switch ((*instr >> 3) & 0x7) {
			case 0:	op1 = modify(op1, op1 + 1); break; /* inc */
			case 1:	op1 = modify(op1, op1 - 1); break; /* dec */
			}
			success = modrm(cpu, MODRM_WRITE_RM, mode, op == 0xfe?
			    MODRM_EIGHTBIT : 0, &instr_orig, NULL, &op1, &op2);
			if (!success)
				return 0;
			x86_cmp(cpu, op1, 0);
			break;
		default:
			fatal("UNIMPLEMENTED 0x%02x,0x%02x", op, *instr);
		}
	} else {
		fatal("x86_cpu_run_instr(): unimplemented opcode 0x%02x"
		    " at ", op); print_csip(cpu); fatal("\n");
		cpu->running = 0;
		return 0;
	}

	cpu->pc = newpc;

	return 1;
}


#define CPU_RUN         x86_cpu_run
#define CPU_RINSTR      x86_cpu_run_instr
#define CPU_RUN_X86
#include "cpu_run.c"
#undef CPU_RINSTR
#undef CPU_RUN_X86
#undef CPU_RUN


/*
 *  x86_cpu_family_init():
 *
 *  Fill in the cpu_family struct for x86.
 */
int x86_cpu_family_init(struct cpu_family *fp)
{
	fp->name = "x86";
	fp->cpu_new = x86_cpu_new;
	fp->list_available_types = x86_cpu_list_available_types;
	fp->register_match = x86_cpu_register_match;
	fp->disassemble_instr = x86_cpu_disassemble_instr;
	fp->register_dump = x86_cpu_register_dump;
	fp->run = x86_cpu_run;
	fp->dumpinfo = x86_cpu_dumpinfo;
	/*  fp->show_full_statistics = x86_cpu_show_full_statistics;  */
	/*  fp->tlbdump = x86_cpu_tlbdump;  */
	/*  fp->interrupt = x86_cpu_interrupt;  */
	/*  fp->interrupt_ack = x86_cpu_interrupt_ack;  */
	return 1;
}

#endif	/*  ENABLE_X86  */
