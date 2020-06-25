/* KallistiOS ##version##

   mm.c
   (c)2000-2001 Dan Potter
*/

/* Defines a simple UNIX-style memory pool system. Since the Dreamcast has
   multiple distinct areas of memory used for different things, we'll
   want to keep seperate pools. Mainly this will be used with the PowerVR
   and the system RAM, since the SPU has its own program (that can do its
   own memory management). */


/* Note: right now we only support system RAM */


#include <arch/types.h>
#include <arch/arch.h>
#include <arch/irq.h>
#include <stdio.h>

#define MCR (*((volatile uint32_t *) 0xff800014))

/* The end of the program is always marked by the '_end' symbol. So we'll
   longword-align that and add a little for safety. sbrk() calls will
   move up from there. */
extern unsigned long end;
static void *sbrk_base;

/* MM-wide initialization */
int mm_init() {
    int base = (int)(&end);
    base = (base / 4) * 4 + 4;  /* longword align */
    sbrk_base = (void*)base;

    return 0;
}

/* Return address at top of system RAM */
uint32 mm_top() {
	if (((MCR >> 3) & 0x07) == 3) {
		/* AMX 3, assuming 32MB SDRAM */
		return 0x8e000000;
	}
	else {
#ifndef _arch_sub_naomi
		return 0x8d000000;
#else
		return 0x8e000000;
#endif
	}
}

/* Simple sbrk function */
void* mm_sbrk(unsigned long increment) {
    int old;
    void *base = sbrk_base;

    old = irq_disable();

    if(increment & 3)
        increment = (increment + 4) & ~3;

    sbrk_base = (void *)(increment + (unsigned long)sbrk_base);

    if(((uint32)sbrk_base) >= (mm_top() - 65536)) {
        dbglog(DBG_DEAD, "Requested sbrk_base %p, was %p, diff %lu\n",
               sbrk_base, base, increment);
        arch_panic("out of memory; about to run over kernel stack");
    }

    irq_restore(old);

    return base;
}
