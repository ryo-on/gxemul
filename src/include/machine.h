#ifndef	MACHINE_H
#define	MACHINE_H

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
 *  $Id: machine.h,v 1.81 2005-11-13 22:34:25 debug Exp $
 */

#include <sys/types.h>
#include <sys/time.h>

#include "symbol.h"

#include "machine_arc.h"
#include "machine_x86.h"


#define	MAX_BREAKPOINTS		8
#define	BREAKPOINT_FLAG_R	1

#define	MAX_TICK_FUNCTIONS	14

struct cpu_family;
struct diskimage;
struct emul;
struct fb_window;
struct memory;

/*  Ugly:  */
struct kn230_csr;
struct kn02_csr;
struct dec_ioasic_data;
struct ps2_data;
struct footbridge_data;
struct dec5800_data;
struct au1x00_ic_data;
struct malta_data;
struct vr41xx_data;
struct jazz_data;
struct crime_data;
struct mace_data;
struct sgi_ip20_data;
struct sgi_ip22_data;
struct sgi_ip30_data;
struct isa_pic_data {
	struct pic8259_data	*pic1;
	struct pic8259_data	*pic2;
	int			last_int;
};


#define	MACHINE_NAME_MAXBUF		150

struct machine {
	/*  Pointer back to the emul struct we are in:  */
	struct emul *emul;

	/*  Name as choosen by the user:  */
	char	*name;

	int	arch;			/*  ARCH_MIPS, ARCH_PPC, ..  */
	int	machine_type;		/*  MACHINE_DEC, ..  */
	int	machine_subtype;	/*  MACHINE_DEC_3MAX_5000, ..  */

	char	*machine_name;

	/*  The serial number is mostly used when emulating multiple machines
	    in a network. nr_of_nics is the current nr of network cards, which
	    is useful when emulating multiple cards in one machine:  */
	int	serial_nr;
	int	nr_of_nics;

	struct cpu_family *cpu_family;

	/*
	 *  The "mainbus":
	 *
	 *	o)  memory
	 *	o)  devices
	 *	o)  CPUs
	 */

	struct memory *memory;

	int	main_console_handle;

	/*  Hardware devices, run every x clock cycles.  */
	int	n_tick_entries;
	int	ticks_till_next[MAX_TICK_FUNCTIONS];
	int	ticks_reset_value[MAX_TICK_FUNCTIONS];
	void	(*tick_func[MAX_TICK_FUNCTIONS])(struct cpu *, void *);
	void	*tick_extra[MAX_TICK_FUNCTIONS];

	void	(*md_interrupt)(struct machine *m, struct cpu *cpu,
		    int irq_nr, int assert);

	char	*cpu_name;  /*  TODO: remove this, there could be several
				cpus with different names in a machine  */
	int	byte_order_override;
	int	bootstrap_cpu;
	int	use_random_bootstrap_cpu;
	int	start_paused;
	int	ncpus;
	struct cpu **cpus;

	/*  These are used by stuff in cpu.c, mostly:  */
	int64_t ncycles;
	int64_t	ncycles_show;
	int64_t	ncycles_flush;
	int64_t	ncycles_since_gettimeofday;
	struct timeval starttime;
	int	a_few_cycles;
	int	a_few_instrs;

	struct diskimage *first_diskimage;

	struct symbol_context symbol_context;

	int	random_mem_contents;
	int	physical_ram_in_mb;
	int	memory_offset_in_mb;
	int	prom_emulation;
	int	register_dump;
	int	arch_pagesize;

	int	n_breakpoints;
	char	*breakpoint_string[MAX_BREAKPOINTS];
	uint64_t breakpoint_addr[MAX_BREAKPOINTS];
	int	breakpoint_flags[MAX_BREAKPOINTS];

	/*  Cache sizes: (1 << x) x=0 for default values  */
	/*  TODO: these are _PER CPU_!  */
	int	cache_picache;
	int	cache_pdcache;
	int	cache_secondary;
	int	cache_picache_linesize;
	int	cache_pdcache_linesize;
	int	cache_secondary_linesize;

	int	dbe_on_nonexistant_memaccess;
	int	dyntrans_alignment_check;
	int	bintrans_enable;
	int	old_bintrans_enable;
	int	bintrans_enabled_from_start;
	int	bintrans_size;
	int	instruction_trace;
	int	single_step_on_bad_addr;
	int	show_nr_of_instructions;
	int	show_symbolic_register_names;
	int64_t	max_instructions;
	int	emulated_hz;
	int	max_random_cycles_per_chunk;
	int	speed_tricks;
	char	*userland_emul;		/*  NULL for no userland emulation  */
	int	force_netboot;
	int	slow_serial_interrupts_hack_for_linux;
	uint64_t file_loaded_end_addr;
	char	*boot_kernel_filename;
	char	*boot_string_argument;

	int	automatic_clock_adjustment;
	int	exit_without_entering_debugger;
	int	show_trace_tree;

	int	n_gfx_cards;

	/*  Machine-dependent: (PROM stuff, etc.)  */
	union {
		struct machine_arcbios	arc;
		struct machine_pc	pc;
	} md;

	/*  Bus-specific interrupt data:  */
	struct isa_pic_data isa_pic_data;

	/*  Machine-dependent interrupt specific structs:  */
	union {
		struct kn230_csr *kn230_csr;
		struct kn02_csr *kn02_csr;
		struct dec_ioasic_data *dec_ioasic_data;
		struct ps2_data *ps2_data;
		struct dec5800_data *dec5800_csr;
		struct au1x00_ic_data *au1x00_ic_data;
		struct vr41xx_data *vr41xx_data;       
		struct jazz_data *jazz_data;
		struct malta_data *malta_data;
		struct sgi_ip20_data *sgi_ip20_data;
		struct sgi_ip22_data *sgi_ip22_data;
		struct sgi_ip30_data *sgi_ip30_data;
		struct {
			struct crime_data *crime_data;
			struct mace_data *mace_data;
		} ip32;
		struct footbridge_data *footbridge_data;
	} md_int;

	/*  X11/framebuffer stuff:  */
	int	use_x11;
	int	x11_scaledown;
	int	x11_scaleup;
	int	x11_n_display_names;
	char	**x11_display_names;
	int	x11_current_display_name_nr;	/*  updated by x11.c  */

	int	n_fb_windows;
	struct fb_window **fb_windows;
};


/*
 *  Machine emulation types:
 */

#define	ARCH_NOARCH		0
#define	ARCH_MIPS		1
#define	ARCH_PPC		2
#define	ARCH_SPARC		3
#define	ARCH_ALPHA		4
#define	ARCH_X86		5
#define	ARCH_ARM		6
#define	ARCH_IA64		7
#define	ARCH_M68K		8
#define	ARCH_SH			9
#define	ARCH_HPPA		10
#define	ARCH_I960		11
#define	ARCH_AVR		12
#define	ARCH_NEWMIPS		13

/*  MIPS:  */
#define	MACHINE_BAREMIPS	1000
#define	MACHINE_TESTMIPS	1001
#define	MACHINE_DEC		1002
#define	MACHINE_COBALT		1003
#define	MACHINE_HPCMIPS		1004
#define	MACHINE_PS2		1005
#define	MACHINE_SGI		1006
#define	MACHINE_ARC		1007
#define	MACHINE_MESHCUBE	1008
#define	MACHINE_NETGEAR		1009
#define	MACHINE_SONYNEWS	1010
#define	MACHINE_EVBMIPS		1011
#define	MACHINE_PSP		1012

/*  PPC:  */
#define	MACHINE_BAREPPC		2000
#define	MACHINE_TESTPPC		2001
#define	MACHINE_WALNUT		2002
#define	MACHINE_PMPPC		2003
#define	MACHINE_SANDPOINT	2004
#define	MACHINE_BEBOX		2005
#define	MACHINE_PREP		2006
#define	MACHINE_MACPPC		2007
#define	MACHINE_DB64360		2008

/*  SPARC:  */
#define	MACHINE_BARESPARC	3000
#define	MACHINE_TESTSPARC	3001
#define	MACHINE_ULTRA1		3002

/*  Alpha:  */
#define	MACHINE_BAREALPHA	4000
#define	MACHINE_TESTALPHA	4001
#define	MACHINE_ALPHA		4002

/*  X86:  */
#define	MACHINE_BAREX86		5000
#define	MACHINE_X86		5001

/*  ARM:  */
#define	MACHINE_BAREARM		6000
#define	MACHINE_TESTARM		6001
#define	MACHINE_CATS		6002
#define	MACHINE_HPCARM		6003
#define	MACHINE_ZAURUS		6004
#define	MACHINE_NETWINDER	6005
#define	MACHINE_SHARK		6006
#define	MACHINE_IQ80321		6007
#define	MACHINE_IYONIX		6008

/*  IA64:  */
#define	MACHINE_BAREIA64	7000
#define	MACHINE_TESTIA64	7001

/*  M68K:  */
#define	MACHINE_BAREM68K	8000
#define	MACHINE_TESTM68K	8001

/*  SH:  */
#define	MACHINE_BARESH		9000
#define	MACHINE_TESTSH		9001
#define	MACHINE_HPCSH		9002

/*  HPPA:  */
#define	MACHINE_BAREHPPA	10000
#define	MACHINE_TESTHPPA	10001

/*  I960:  */
#define	MACHINE_BAREI960	11000
#define	MACHINE_TESTI960	11001

/*  AVR:  */
#define	MACHINE_BAREAVR		12000

/*  NEWMIPS:  */
#define	MACHINE_BARENEWMIPS	13000
#define	MACHINE_TESTNEWMIPS	13001

/*  Other "pseudo"-machines:  */
#define	MACHINE_NONE		0
#define	MACHINE_USERLAND	100000

/*  DEC:  */
#define	MACHINE_DEC_PMAX_3100		1
#define	MACHINE_DEC_3MAX_5000		2
#define	MACHINE_DEC_3MIN_5000		3
#define	MACHINE_DEC_3MAXPLUS_5000	4
#define	MACHINE_DEC_5800		5
#define	MACHINE_DEC_5400		6
#define	MACHINE_DEC_MAXINE_5000		7
#define	MACHINE_DEC_5500		11
#define	MACHINE_DEC_MIPSMATE_5100	12

#define	DEC_PROM_CALLBACK_STRUCT	0xffffffffbfc04000ULL
#define	DEC_PROM_EMULATION		0xffffffffbfc08000ULL
#define	DEC_PROM_INITIAL_ARGV		(INITIAL_STACK_POINTER + 0x80)
#define	DEC_PROM_STRINGS		0xffffffffbfc20000ULL
#define	DEC_PROM_TCINFO			0xffffffffbfc2c000ULL
#define	DEC_MEMMAP_ADDR			0xffffffffbfc30000ULL

/*  HPCmips:  */
#define	MACHINE_HPCMIPS_CASIO_BE300		1
#define	MACHINE_HPCMIPS_CASIO_E105		2
#define	MACHINE_HPCMIPS_NEC_MOBILEPRO_770	3
#define	MACHINE_HPCMIPS_NEC_MOBILEPRO_780	4
#define	MACHINE_HPCMIPS_NEC_MOBILEPRO_800	5
#define	MACHINE_HPCMIPS_NEC_MOBILEPRO_880	6
#define	MACHINE_HPCMIPS_AGENDA_VR3		7
#define	MACHINE_HPCMIPS_IBM_WORKPAD_Z50		8

/*  HPCarm:  */
#define	MACHINE_HPCARM_IPAQ			1
#define	MACHINE_HPCARM_JORNADA720		2

/*  HPCsh:  */
#define	MACHINE_HPCSH_JORNADA680		1
#define	MACHINE_HPCSH_JORNADA690		2

/*  Playstation 2:  */
#define	PLAYSTATION2_BDA	0xffffffffa0001000ULL
#define	PLAYSTATION2_OPTARGS	0xffffffff81fff100ULL
#define	PLAYSTATION2_SIFBIOS	0xffffffffbfc10000ULL

/*  SGI and ARC:  */
#define	MACHINE_ARC_NEC_RD94		1
#define	MACHINE_ARC_JAZZ_PICA		2
#define	MACHINE_ARC_NEC_R94		3
#define	MACHINE_ARC_DESKTECH_TYNE	4
#define	MACHINE_ARC_JAZZ_MAGNUM		5
#define	MACHINE_ARC_NEC_R98		6
#define	MACHINE_ARC_JAZZ_M700		7
#define	MACHINE_ARC_NEC_R96		8

/*  EVBMIPS:  */
#define	MACHINE_EVBMIPS_MALTA		1
#define	MACHINE_EVBMIPS_MALTA_BE	2
#define	MACHINE_EVBMIPS_PB1000		3

/*  MacPPC:  TODO: Real model names  */
#define	MACHINE_MACPPC_G4		1
#define	MACHINE_MACPPC_G5		2

/*  X86:  */
#define	MACHINE_X86_GENERIC		1
#define	MACHINE_X86_XT			2


/*
 *  Problem: kernels seem to be loaded at low addresses in RAM, so
 *  storing environment strings and memory descriptors there is a bad
 *  idea. They are stored at 0xbfc..... instead.  The ARC SPB must
 *  be at physical address 0x1000 though.
 */
#define	SGI_SPB_ADDR		0xffffffff80001000ULL
/*  0xbfc10000 is firmware callback vector stuff  */
#define	ARC_FIRMWARE_VECTORS	0xffffffffbfc80000ULL
#define	ARC_FIRMWARE_ENTRIES	0xffffffffbfc88000ULL
#define	ARC_ARGV_START		0xffffffffbfc90000ULL
#define	ARC_ENV_STRINGS		0xffffffffbfc98000ULL
#define	ARC_ENV_POINTERS	0xffffffffbfc9d000ULL
#define	SGI_SYSID_ADDR		0xffffffffbfca1800ULL
#define	ARC_DSPSTAT_ADDR	0xffffffffbfca1c00ULL
#define	ARC_MEMDESC_ADDR	0xffffffffbfca1c80ULL
#define	ARC_CONFIG_DATA_ADDR	0xffffffffbfca2000ULL
#define	FIRST_ARC_COMPONENT	0xffffffffbfca8000ULL
#define	ARC_PRIVATE_VECTORS	0xffffffffbfcb0000ULL
#define	ARC_PRIVATE_ENTRIES	0xffffffffbfcb8000ULL


/*  machine.c:  */
struct machine *machine_new(char *name, struct emul *emul);
int machine_name_to_type(char *stype, char *ssubtype,
	int *type, int *subtype, int *arch);
void machine_add_tickfunction(struct machine *machine,
	void (*func)(struct cpu *, void *), void *extra, int clockshift);
void dump_mem_string(struct cpu *cpu, uint64_t addr);
void store_string(struct cpu *cpu, uint64_t addr, char *s);
int store_64bit_word(struct cpu *cpu, uint64_t addr, uint64_t data64);
int store_32bit_word(struct cpu *cpu, uint64_t addr, uint64_t data32);
int store_16bit_word(struct cpu *cpu, uint64_t addr, uint64_t data16);
void store_byte(struct cpu *cpu, uint64_t addr, uint8_t data);
void store_64bit_word_in_host(struct cpu *cpu, unsigned char *data,
	uint64_t data32);
void store_32bit_word_in_host(struct cpu *cpu, unsigned char *data,
	uint64_t data32);
void store_16bit_word_in_host(struct cpu *cpu, unsigned char *data,
	uint16_t data16);
uint32_t load_32bit_word(struct cpu *cpu, uint64_t addr);
uint16_t load_16bit_word(struct cpu *cpu, uint64_t addr);
void store_buf(struct cpu *cpu, uint64_t addr, char *s, size_t len);
void machine_setup(struct machine *);
void machine_memsize_fix(struct machine *);
void machine_default_cputype(struct machine *);
void machine_dumpinfo(struct machine *);
void machine_list_available_types_and_cpus(void);
void machine_init(void);


#endif	/*  MACHINE_H  */
