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
 *  $Id: dev_dec21143.c,v 1.4 2005-11-09 17:14:21 debug Exp $
 *
 *  DEC 21143 ("Tulip") ethernet.
 *
 *  TODO:  This is just a dummy device, so far.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "net.h"

#include "tulipreg.h"


#define debug fatal

#define	N_REGS		32
#define	ROM_WIDTH	6

struct dec21143_data {
	uint32_t	reg[N_REGS];

	uint8_t		mac[6];
	uint16_t	rom[1 << ROM_WIDTH];

	int		srom_curbit;
	int		srom_command;
	int		srom_command_has_started;
	int		rom_addr;
};


/*
 *  srom_access():
 *
 *  This function handles reads from the Ethernet Address ROM. This is not a
 *  100% correct implementation, as it was reverse-engineered from OpenBSD
 *  sources; it seems to work with OpenBSD, NetBSD, and Linux.
 *
 *  Each transfer (if I understood this correctly) is of the following format:
 *
 *	1xx yyyyyy zzzzzzzzzzzzzzzz
 *
 *  where 1xx    = operation (6 means a Read),
 *        yyyyyy = ROM address
 *        zz...z = data
 *
 *  y and z are _both_ read and written to at the same time; this enables the
 *  operating system to sense the number of bits in y (when reading, all y bits
 *  are 1 except the last one).
 */
static void srom_access(struct cpu *cpu, struct dec21143_data *d,
	uint32_t oldreg, uint32_t idata)
{
	int obit, ibit;

	/*  debug("CSR9 WRITE! 0x%08x\n", (int)idata);  */

	/*  New selection? Then reset internal state.  */
	if (idata & MIIROM_SR && !(oldreg & MIIROM_SR)) {
		d->srom_curbit = 0;
		d->srom_command = 0;
		d->srom_command_has_started = 0;
		d->rom_addr = 0;
	}

	/*  Only care about data during clock cycles:  */
	if (!(idata & MIIROM_SROMSK))
		return;

	obit = 0;
	ibit = idata & MIIROM_SROMDI? 1 : 0;
	/*  debug("CLOCK CYCLE! (bit %i): ", d->srom_curbit);  */

	/*
	 *  Linux sends more zeroes before starting the actual command, than
	 *  OpenBSD and NetBSD. Hopefully this is correct. (I'm just guessing
	 *  that all commands should start with a 1, perhaps that's not really
	 *  the case.)
	 */
	if (!ibit && !d->srom_command_has_started)
		return;

	if (d->srom_curbit < 3) {
		d->srom_command_has_started = 1;
		d->srom_command <<= 1;
		d->srom_command |= ibit;
		/*  debug("command input '%i'\n", ibit);  */
	} else {
		switch (d->srom_command) {
		case 6:	if (d->srom_curbit < ROM_WIDTH + 3) {
				obit = d->srom_curbit < ROM_WIDTH + 2;
				d->rom_addr <<= 1;
				d->rom_addr |= ibit;
			} else {
				if (d->srom_curbit == ROM_WIDTH + 3)
					debug("[ dec21143: ROM read from offset"
					    " 0x%03x: 0x%04x ]\n", d->rom_addr,
					    d->rom[d->rom_addr]);
				obit = d->rom[d->rom_addr] & (0x8000 >>
				    (d->srom_curbit - ROM_WIDTH - 3))? 1 : 0;
			}
			break;
		default:fatal("[ dec21243: unimplemented EEPROM "
			    "command %i ]\n", d->srom_command);
		}
		d->reg[CSR_MIIROM / 8] &= ~MIIROM_SROMDO;
		if (obit)
			d->reg[CSR_MIIROM / 8] |= MIIROM_SROMDO;
		/*  debug("input '%i', output '%i'\n", ibit, obit);  */
	}

	d->srom_curbit ++;

	/*
	 *  Done command + addr + data? Then restart. (At least NetBSD does
	 *  sequential reads without turning selection off and then on.)
	 */
	if (d->srom_curbit >= 3 + ROM_WIDTH + 16) {
		d->srom_curbit = 0;
		d->srom_command = 0;
		d->srom_command_has_started = 0;
		d->rom_addr = 0;
	}
}


/*
 *  dev_dec21143_access():
 */
int dev_dec21143_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct dec21143_data *d = extra;
	uint64_t idata = 0, odata = 0;
	uint32_t oldreg = 0;
	int regnr = relative_addr >> 3;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	if ((relative_addr & 3) == 0 && regnr < N_REGS) {
		if (writeflag == MEM_READ)
			odata = d->reg[regnr];
		else {
			oldreg = d->reg[regnr];
			d->reg[regnr] = idata;
		}
	} else
		fatal("[ dec21143: WARNING! unaligned access (0x%x) ]\n",
		    (int)relative_addr);

	switch (relative_addr) {

	case CSR_BUSMODE:	/*  csr0  */
		break;

	case CSR_MIIROM:	/*  csr9  */
		if (writeflag == MEM_WRITE)
			srom_access(cpu, d, oldreg, idata);
		break;

	default:if (writeflag == MEM_READ)
			fatal("[ dec21143: read from unimplemented 0x%02x ]\n",
			    (int)relative_addr);
		else
			fatal("[ dec21143: write to unimplemented 0x%02x: "
			    "0x%02x ]\n", (int)relative_addr, (int)idata);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  devinit_dec21143():
 */
int devinit_dec21143(struct devinit *devinit)
{
	struct dec21143_data *d;
	char name2[100];

	d = malloc(sizeof(struct dec21143_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct dec21143_data));

	net_generate_unique_mac(devinit->machine, d->mac);

	/*  Version (= 1) and Chip count (= 1):  */
	d->rom[TULIP_ROM_SROM_FORMAT_VERION / 2] = 0x101;

	/*  MAC:  */
	d->rom[10] = (d->mac[1] << 8) + d->mac[0];
	d->rom[11] = (d->mac[3] << 8) + d->mac[2];
	d->rom[12] = (d->mac[5] << 8) + d->mac[4];

	snprintf(name2, sizeof(name2), "%s [%02x:%02x:%02x:%02x:%02x:%02x]",
	    devinit->name, d->mac[0], d->mac[1], d->mac[2], d->mac[3],
	    d->mac[4], d->mac[5]);

	memory_device_register(devinit->machine->memory, name2,
	    devinit->addr, 0x100, dev_dec21143_access, d, MEM_DEFAULT, NULL);

	/*
	 *  TODO: don't hardcode this! NetBSD/cats uses mem accesses at
	 *  0x80020000, OpenBSD/cats uses i/o at 0x7c010000.
	 */
	if (devinit->machine->machine_type != MACHINE_CATS) {
		fatal("TODO: dec21143 for non-cats\n");
		exit(1);
	}

	dev_ram_init(devinit->machine, devinit->addr + 0x04010000, 0x100,
	    DEV_RAM_MIRROR | DEV_RAM_MIGHT_POINT_TO_DEVICES, devinit->addr);

	return 1;
}

