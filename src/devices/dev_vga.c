/*
 *  Copyright (C) 2004  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_vga.c,v 1.26 2005-01-23 11:19:36 debug Exp $
 *  
 *  VGA text console device.
 *
 *  A few ugly hacks are used. The default resolution is 640x480, which
 *  means that the following font sizes and text resolutions can be used:
 *
 *	8x16						80 x 30
 *	8x10 (with the last line repeated twice)	80 x 43
 *	8x8						80 x 60
 *
 *  There is only a mode switch when actual non-space text is written outside
 *  the current window.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "devices.h"
#include "memory.h"
#include "mips_cpu.h"
#include "misc.h"

/*  These are generated from binary font files:  */
#include "fonts/font8x8.c"
#include "fonts/font8x10.c"
#include "fonts/font8x16.c"


#define	VGA_MEM_MAXY		60
#define	VGA_MEM_ALLOCY		67


#define	VGA_FB_ADDR	0x1230000000ULL

struct vga_data {
	uint64_t	videomem_base;
	uint64_t	control_base;

	struct vfb_data *fb;

	int		font_size;
	unsigned char	*font;

	int		max_x;
	int		max_y;
	size_t		videomem_size;
	unsigned char	*videomem;	/*  2 bytes per char  */

	unsigned char	selected_register;
	unsigned char	reg[256];

	int		cursor_x;
	int		cursor_y;
};


/*
 *  vga_update():
 *
 *  This function should be called whenever any part of d->videomem[] has
 *  been written to. It will redraw all characters within the range start..end
 *  using the right palette.
 */
void vga_update(struct cpu *cpu, struct vga_data *d, int start, int end)
{
	int fg, bg, i, x,y, subx, line;

	start &= ~1;
	end |= 1;

	for (i=start; i<=end; i+=2) {
		unsigned char ch = d->videomem[i];
		fg = d->videomem[i+1] & 15;
		bg = (d->videomem[i+1] >> 4) & 7;

		/*  Blink is hard to do :-), but inversion might be ok too:  */
		if (d->videomem[i+1] & 128) {
			int tmp = fg; fg = bg; bg = tmp;
		}

		x = (i/2) % d->max_x; x *= 8;
		y = (i/2) / d->max_x; y *= d->font_size;

		for (line = 0; line < d->font_size; line++) {
			for (subx = 0; subx < 8; subx++) {
				unsigned char pixel[3];
				int addr, line2readfrom = line;
				int actualfontheight = d->font_size;

				if (d->font_size == 11) {
					actualfontheight = 10;
					if (line == 10)
						line2readfrom = 9;
				}

				addr = (d->max_x*8 * (line+y) + x + subx)
				    * 3;

				pixel[0] = d->fb->rgb_palette[bg * 3 + 0];
				pixel[1] = d->fb->rgb_palette[bg * 3 + 1];
				pixel[2] = d->fb->rgb_palette[bg * 3 + 2];

				if (d->font[ch * actualfontheight +
				    line2readfrom] & (128 >> subx)) {
					pixel[0] = d->fb->rgb_palette
					    [fg * 3 + 0];
					pixel[1] = d->fb->rgb_palette
					    [fg * 3 + 1];
					pixel[2] = d->fb->rgb_palette
					    [fg * 3 + 2];
				}

				/*  TODO: don't hardcode  */
				if (addr < 480*640*3)
					dev_fb_access(cpu, cpu->mem, addr, &pixel[0],
					    sizeof(pixel), MEM_WRITE, d->fb);
			}
		}
	}
}


/*
 *  vga_update_cursor():
 */
static void vga_update_cursor(struct vga_data *d)
{
	/*  TODO: Don't hardcode the cursor size.  */
	dev_fb_setcursor(d->fb,
	    d->cursor_x * 8, d->cursor_y * d->font_size +
	    d->font_size - 4, 1, 8, 3);
}


/*
 *  dev_vga_access():
 *
 *  Reads and writes to the VGA video memory.
 */
int dev_vga_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr,
	unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct vga_data *d = extra;
	uint64_t idata = 0, odata = 0;
	int modified, i;
	int y;

	idata = memory_readmax64(cpu, data, len);

	y = relative_addr / (d->max_x * 2);

	/*
	 *  Switch fonts?   This is an ugly hack which only switches when
	 *  parts of the video ram is accessed that are outside the current
	 *  screen. (Specially "crafted" :-) to work with Windows NT.)
	 */
	if (writeflag && (idata & 255) != 0x20 && (relative_addr & 1) == 0) {
		if (y >= 43 && d->font_size > 8) {
			/*  Switch to 8x8 font:  */
			debug("SWITCHING to 8x8 font\n");
			d->font_size = 8;
			d->font = font8x8;
			d->max_y = VGA_MEM_MAXY;
			vga_update(cpu, d, 0, d->max_x * d->max_y * 2 -1);
			vga_update_cursor(d);
		} else if (y >= 30 && d->font_size > 11) {
			/*  Switch to 8x10 font:  */
			debug("SWITCHING to 8x10 font\n");
			d->font_size = 11;	/*  NOTE! 11  */
			d->font = font8x10;
			vga_update(cpu, d, 0, d->max_x * 44 * 2 -1);
			d->max_y = 43;
			vga_update_cursor(d);
		}
	}

	if (relative_addr < d->videomem_size) {
		if (writeflag == MEM_WRITE) {
			modified = 0;
			for (i=0; i<len; i++) {
				int old = d->videomem[relative_addr + i];
				if (old != data[i]) {
					d->videomem[relative_addr + i] =
					    data[i];
					modified = 1;
				}
			}

			if (modified)
				vga_update(cpu, d, relative_addr,
				    relative_addr + len-1);
		} else
			memcpy(data, d->videomem + relative_addr, len);
		return 1;
	}

	switch (relative_addr) {
	default:
		if (writeflag==MEM_READ) {
			debug("[ vga: read from 0x%08lx ]\n",
			    (long)relative_addr);
		} else {
			debug("[ vga: write to  0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  vga_reg_write():
 *
 *  Writes to VGA control registers.
 */
static void vga_reg_write(struct vga_data *d, int regnr, int idata)
{
	int ofs;

	switch (regnr) {
	case 0x0e:
	case 0x0f:
		ofs = d->reg[0x0e] * 256 + d->reg[0x0f];
		d->cursor_x = ofs % d->max_x;
		d->cursor_y = ofs / d->max_x;
		vga_update_cursor(d);
		break;
	default:
		debug("[ vga_reg_write: regnr=0x%02x idata=0x%02x ]\n",
		    regnr, idata);
	}
}


/*
 *  dev_vga_ctrl_access():
 *
 *  Reads and writes of the VGA control registers.
 */
int dev_vga_ctrl_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct vga_data *d = extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 0x01:	/*  "Other video attributes"  */
		odata = 0xff;	/*  ?  */
		break;
	case 0x0c:	/*  VGA graphics 1 position  */
		odata = 1;	/*  ?  */
		break;
	case 0x14:	/*  register select  */
		if (writeflag == MEM_READ)
			odata = d->selected_register;
		else
			d->selected_register = idata;
		break;
	case 0x15:	if (writeflag == MEM_READ)
			odata = d->reg[d->selected_register];
		else {
			d->reg[d->selected_register] = idata;
			vga_reg_write(d, d->selected_register, idata);
		}
		break;
	case 0x1a:	/*  Status register  */
		odata = 1;	/*  Display enabled  */
		/*  odata |= 16;  */  /*  Vertical retrace  */
		break;
	default:
		if (writeflag==MEM_READ) {
			fatal("[ vga_ctrl: read from 0x%08lx ]\n",
			    (long)relative_addr);
		} else {
			fatal("[ vga_ctrl: write to  0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_vga_init():
 *
 *  Register a VGA text console device. max_x and max_y could be something
 *  like 80 and 25, respectively.
 */
void dev_vga_init(struct cpu *cpu, struct memory *mem, uint64_t videomem_base,
	uint64_t control_base, int max_x, int max_y, char *name)
{
	struct vga_data *d;
	int r,g,b,i, x,y;

	d = malloc(sizeof(struct vga_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct vga_data));
	d->videomem_base = videomem_base;
	d->control_base  = control_base;
	d->max_x         = max_x;
	d->max_y         = max_y;
	d->videomem_size = max_x * VGA_MEM_MAXY * 2;
	d->videomem = malloc(d->videomem_size);
	if (d->videomem == NULL) {
		fprintf(stderr, "out of memory in dev_vga_init()\n");
		exit(1);
	}

	for (y=0; y<VGA_MEM_MAXY; y++) {
		char s[81];
#ifdef VERSION
		strcpy(s, " mips64emul-" VERSION);
#else
		strcpy(s, " mips64emul");
#endif
		memset(s+strlen(s), ' ', 80 - strlen(s));
		memcpy(s+79-strlen(name), name, strlen(name));
		s[80] = 0;

		for (x=0; x<max_x; x++) {
			char ch = ' ';
			if (y == 0)
				ch = s[x];
			i = (x + max_x * y) * 2;
			d->videomem[i] = ch;
			d->videomem[i+1] = y==0? 0x70 : 0x07;	/*  Default color  */
		}
	}

	d->font_size = 16;
	d->font = font8x16;

	d->fb = dev_fb_init(cpu, mem, VGA_FB_ADDR, VFB_GENERIC,
	    8*max_x, 16*max_y, 8*max_x, 16*max_y, 24, "VGA", 0);

	i = 0;
	for (r=0; r<2; r++)
		for (g=0; g<2; g++)
			for (b=0; b<2; b++) {
				d->fb->rgb_palette[i + 0] = r * 0xaa;
				d->fb->rgb_palette[i + 1] = g * 0xaa;
				d->fb->rgb_palette[i + 2] = b * 0xaa;
				i+=3;
			}
	for (r=0; r<2; r++)
		for (g=0; g<2; g++)
			for (b=0; b<2; b++) {
				d->fb->rgb_palette[i + 0] = r * 0xaa + 0x55;
				d->fb->rgb_palette[i + 1] = g * 0xaa + 0x55;
				d->fb->rgb_palette[i + 2] = b * 0xaa + 0x55;
				i+=3;
			}

	memory_device_register(mem, "vga_mem", videomem_base,
	    VGA_MEM_ALLOCY * max_x * 2,
	    dev_vga_access, d, MEM_DEFAULT, NULL);	/*  TODO: BINTRANS  */
	memory_device_register(mem, "vga_ctrl", control_base,
	    32, dev_vga_ctrl_access, d, MEM_DEFAULT, NULL);

	/*  Make sure that the first line is in synch.  */
	vga_update(cpu, d, 0, max_x * 2 -1);
}

