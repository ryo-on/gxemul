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
 *  $Id: device.c,v 1.1 2005-02-22 05:53:01 debug Exp $
 *
 *  Device registry framework.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device.h"
#include "misc.h"


static struct device_entry *device_entries = NULL;
static int device_entries_sorted = 0;
static int n_device_entries = 0;


static int device_entry_compar(const void *a, const void *b)
{
	struct device_entry *pa = (struct device_entry *) a;
	struct device_entry *pb = (struct device_entry *) b;

	return strcmp(pa->name, pb->name);
}


/*
 *  sort_entries():
 *
 *  Internal function. Sorts the device_entries array in alphabetic order.
 */
static void sort_entries(void)
{
	qsort(device_entries, n_device_entries, sizeof(struct device_entry),
	    device_entry_compar);

	device_entries_sorted = 1;
}


/*
 *  device_register():
 *
 *  Registers a device. The device is added to the end of the device_entries
 *  array, and the sorted flag is set to zero.
 *
 *  NOTE: It would be a bad thing if two devices had the same name. However,
 *        that isn't checked here, it is up to the caller!
 *
 *  Return value is 1 if the device was registered, 0 otherwise.
 */
int device_register(char *name)
{
	device_entries = realloc(device_entries, sizeof(struct device_entry)
	    * (n_device_entries + 1));
	if (device_entries == NULL) {
		fprintf(stderr, "device_register(): out of memory\n");
		exit(1);
	}

	/*  TODO  */
	device_entries[n_device_entries].name = strdup(name);

	device_entries_sorted = 0;
	n_device_entries ++;
	return 1;
}


/*
 *  device_lookup():
 *
 *  Lookup a device name by scanning the device_entries array (as a binary
 *  search tree).
 *
 *  Return value is a pointer to the device_entry on success, or a NULL pointer
 *  if there was no such device.
 */
struct device_entry *device_lookup(char *name)
{
	int i, step, r;

	if (!device_entries_sorted)
		sort_entries();

	if (n_device_entries == 0)
		return NULL;

	i = n_device_entries / 2;
	step = i/2 + 1;

	for (;;) {
		if (i < 0)
			i = 0;
		if (i >= n_device_entries)
			i = n_device_entries - 1;

		printf("device_lookup(): i=%i step=%i\n", i, step);

		r = strcmp(name, device_entries[i].name);

		if (r < 0) {
			/*  Go left:  */
			i -= step;
		} else if (r > 0) {
			/*  Go right:  */
			i += step;
		} else {
			/*  Found it!  */
			return &device_entries[i];
		}

		if (step == 0)
			return NULL;

		step /= 2;
	}
}


/*
 *  device_unregister():
 *
 *  Unregisters a device.
 *
 *  Return value is 1 if a device was unregistered, 0 otherwise.
 */
int device_unregister(char *name)
{
	fatal("device_unregister(): TODO\n");
	return 0;
}


/*
 *  device_dumplist():
 *
 *  Dump a list of all registered devices.  (If the list is not sorted when
 *  this function is called, it is implicitly sorted.)
 */
void device_dumplist(void)
{
	int i;

	if (!device_entries_sorted)
		sort_entries();

	for (i=0; i<n_device_entries; i++)
		fatal("device %3i: %s\n", i, device_entries[i].name);
}


/*
 *  device_init():
 *
 *  Initialize the device registry, and call autodev_init() to automatically
 *  add all normal devices (from the devices/ directory).
 *
 *  This function should be called before any other device_*() function is used.
 */
void device_init(void)
{
	device_entries = NULL;
	device_entries_sorted = 0;
	n_device_entries = 0;

	autodev_init();
}

