
/*
 * Copyright (C) 1998 Exotec, Inc.
 *
 * This software is being provided by the copyright holders under the
 * following license. By obtaining, using and/or copying this software,
 * you agree that you have read, understood, and will comply with the
 * following terms and conditions:
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose and without fee or royalty is
 * hereby granted, provided that the full text of this NOTICE appears on
 * ALL copies of the software and documentation or portions thereof,
 * including modifications, that you make.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS," AND COPYRIGHT HOLDERS MAKE NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED. BY WAY OF EXAMPLE,
 * BUT NOT LIMITATION, COPYRIGHT HOLDERS MAKE NO REPRESENTATIONS OR
 * WARRANTIES OF MERCHANTABILITY OR FITNESS FOR ANY PARTICULAR PURPOSE OR
 * THAT THE USE OF THE SOFTWARE OR DOCUMENTATION WILL NOT INFRINGE ANY
 * THIRD PARTY PATENTS, COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS. COPYRIGHT
 * HOLDERS WILL BEAR NO LIABILITY FOR ANY USE OF THIS SOFTWARE OR
 * DOCUMENTATION.
 *
 * The name and trademarks of copyright holders may NOT be used in
 * advertising or publicity pertaining to the software without specific,
 * written prior permission. Title to copyright in this software and any
 * associated documentation will at all times remain with Exotec, Inc..
 *
 * This file may be derived from previously copyrighted software. This
 * copyright applies only to those changes made by Exotec, Inc. The rest
 * of this file is covered by the copyright notices, if any, listed below.
 */

/*	$NetBSD: clock.c,v 1.39 1996/05/12 23:11:54 mycroft Exp $	*/

/*-
 * Copyright (c) 1993, 1994 Charles Hannum.
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz and Don Ahn.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)clock.c	7.2 (Berkeley) 5/12/91
 */
/* 
 * Mach Operating System
 * Copyright (c) 1991,1990,1989 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */
/*
  Copyright 1988, 1989 by Intel Corporation, Santa Clara, California.

		All Rights Reserved

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appears in all
copies and that both the copyright notice and this permission notice
appear in supporting documentation, and that the name of Intel
not be used in advertising or publicity pertaining to distribution
of the software without specific, written prior permission.

INTEL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS,
IN NO EVENT SHALL INTEL BE LIABLE FOR ANY SPECIAL, INDIRECT, OR
CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT,
NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

/*
 * Primitive clock interrupt routines.
 */
#if 0 /* EXOKERNEL USES DIFFERENT INCLUDE FILES */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/device.h>
#else

#ifndef __ENCAP__
#include <xok/sysinfoP.h>
#else
#include <xok/sysinfo.h>
#endif

#include <xok/cdefs.h>
#include <xok/types.h>
#include <xok/picirq.h>
#include <xok/printf.h>
#define splx(s)	
#endif

#if 0 /* NOT NEEDED IN EXOKERNEL */
#include <machine/cpu.h>
#endif
#include <machine/intr.h>
#include <machine/pio.h>
#include <machine/cpufunc.h>

#include <dev/isa/isareg.h>
#if 0 /* NOT NEEDED IN EXOKERNEL */
#include <dev/isa/isavar.h>
#endif
#include <dev/ic/mc146818reg.h>
#include <i386/isa/nvram.h>
#include <i386/isa/timerreg.h>
#if 0 /* NO BEEPING IN EXOKERNEL YET */
#include <i386/isa/spkrreg.h>
#endif

void	spinwait __P((int));
void	findcpuspeed __P((void));
int	clockintr __P((void *));
int	gettick __P((void));
void	sysbeepstop __P((void *));
void	sysbeep __P((int, int));
int	rtcget __P((mc_todregs *));
void	rtcput __P((mc_todregs *));
static int yeartoday __P((int));
int 	hexdectodec __P((int));
int	dectohexdec __P((int));
int	rtcintr __P((void *));

__inline u_int mc146818_read __P((void *, u_int));
__inline void mc146818_write __P((void *, u_int, u_int));

#define I586_CPU /* EXOKERNEL DOESN'T CURRENTLY WORK WITH <= 486 */
#define I686_CPU /* EXOKERNEL DOESN'T CURRENTLY WORK WITH <= 486 */

#if defined(I586_CPU) || defined(I686_CPU)
int pentium_mhz;
#endif

#define	SECMIN	((unsigned)60)			/* seconds per minute */
#define	SECHOUR	((unsigned)(60*SECMIN))		/* seconds per hour */
#define	SECDAY	((unsigned)(24*SECHOUR))	/* seconds per day */
#define	SECYR	((unsigned)(365*SECDAY))	/* seconds per common year */

__inline u_int
mc146818_read(sc, reg)
	void *sc;					/* XXX use it? */
	u_int reg;
{

	outb(IO_RTC, reg);
	return (inb(IO_RTC+1));
}

__inline void
mc146818_write(sc, reg, datum)
	void *sc;					/* XXX use it? */
	u_int reg, datum;
{

	outb(IO_RTC, reg);
	outb(IO_RTC+1, datum);
}

void
startrtclock()
{
	int s;

	findcpuspeed();		/* use the clock (while it's free)
					to find the cpu speed */
	/* initialize 8253 clock */
	outb(TIMER_MODE, TIMER_SEL0|TIMER_RATEGEN|TIMER_16BIT);

	/* Correct rounding will buy us a better precision in timekeeping */
#if 0 /* EXOKERNEL KEEPS HZ VALUE IN SYSINFO STRUCTURE */
	outb(IO_TIMER1, TIMER_DIV(hz) % 256);
	outb(IO_TIMER1, TIMER_DIV(hz) / 256);
#else
	outb(IO_TIMER1, TIMER_DIV(SI_HZ) % 256);
	outb(IO_TIMER1, TIMER_DIV(SI_HZ) / 256);
#endif
#if 1 /* EXOKERNEL: EXTRA SETUP */
	printf ("  Setup timer interrupts via 8259A\n");
	irq_setmask_8259A (irq_mask_8259A & 0xfffa);
	//printf ("  unmasked timer interrupt\n");
#endif

	/* Check diagnostic status */
	if ((s = mc146818_read(NULL, NVRAM_DIAG)) != 0)	/* XXX softc */
		printf("RTC BIOS diagnostic error %b\n", (unsigned int) s, 
		    NVRAM_DIAG_BITS);
}

#if 0 /* NOT USED IN EXOKERNEL */
int
clockintr(arg)
	void *arg;
{
	struct clockframe *frame = arg;		/* not strictly necessary */

	hardclock(frame);
	return 1;
}

int
rtcintr(arg)
	void *arg;
{
	struct clockframe *frame = arg;		/* not strictly neccecary */
	u_int stat;

	stat = mc146818_read(NULL, MC_REGC);
	if (stat & MC_REGC_PF) {
		statclock(frame);
		return 1;
	}
	return 0;
}
#endif

int
gettick()
{
	u_char lo, hi;

	/* Don't want someone screwing with the counter while we're here. */
	disable_intr();
	/* Select counter 0 and latch it. */
	outb(TIMER_MODE, TIMER_SEL0 | TIMER_LATCH);
	lo = inb(TIMER_CNTR0);
	hi = inb(TIMER_CNTR0);
	enable_intr();
	return ((hi << 8) | lo);
}

/*
 * Wait "n" microseconds.
 * Relies on timer 1 counting down from (TIMER_FREQ / hz) at TIMER_FREQ Hz.
 * Note: timer had better have been programmed before this is first used!
 * (Note that we use `rate generator' mode, which counts at 1:1; `square
 * wave' mode counts at 2:1).
 */
void
delay(n)
	int n;
{
	int limit, tick, otick;

	/*
	 * Read the counter first, so that the rest of the setup overhead is
	 * counted.
	 */
	otick = gettick();

#ifdef __GNUC__
	/*
	 * Calculate ((n * TIMER_FREQ) / 1e6) using explicit assembler code so
	 * we can take advantage of the intermediate 64-bit quantity to prevent
	 * loss of significance.
	 */
	n -= 5;
	if (n < 0)
		return;
	{register int m;
	__asm __volatile("mul %3"
			 : "=a" (n), "=d" (m)
			 : "0" (n), "r" (TIMER_FREQ));
	__asm __volatile("div %3"
			 : "=a" (n)
			 : "0" (n), "d" (m), "r" (1000000)
			 : "%edx");}
#else
	/*
	 * Calculate ((n * TIMER_FREQ) / 1e6) without using floating point and
	 * without any avoidable overflows.
	 */
	n -= 20;
	{
		int sec = n / 1000000,
		    usec = n % 1000000;
		n = sec * TIMER_FREQ +
		    usec * (TIMER_FREQ / 1000000) +
		    usec * ((TIMER_FREQ % 1000000) / 1000) / 1000 +
		    usec * (TIMER_FREQ % 1000) / 1000000;
	}
#endif

#if 0 /* EXOKERNEL KEEPS HZ VALUE IN SYSINFO STRUCTURE */
	limit = TIMER_FREQ / hz;
#else
	limit = TIMER_FREQ / SI_HZ;
#endif

	while (n > 0) {
		tick = gettick();
		if (tick > otick)
			n -= limit - (tick - otick);
		else
			n -= otick - tick;
		otick = tick;
	}
}

#if 0 /* NO BEEPING IN EXOKERNEL YET */
static int beeping;

void
sysbeepstop(arg)
	void *arg;
{

	/* disable counter 2 */
	disable_intr();
	outb(PITAUX_PORT, inb(PITAUX_PORT) & ~PIT_SPKR);
	enable_intr();
	beeping = 0;
}

void
sysbeep(pitch, period)
	int pitch, period;
{
	static int last_pitch;
	extern int cold;

	if (cold)
		return;		/* Can't beep yet. */

	if (beeping)
		untimeout(sysbeepstop, 0);
	if (pitch == 0 || period == 0) {
		sysbeepstop(0);
		last_pitch = 0;
		return;
	}
	if (!beeping || last_pitch != pitch) {
		disable_intr();
		outb(TIMER_MODE, TIMER_SEL2 | TIMER_16BIT | TIMER_SQWAVE);
		outb(TIMER_CNTR2, TIMER_DIV(pitch) % 256);
		outb(TIMER_CNTR2, TIMER_DIV(pitch) / 256);
		outb(PITAUX_PORT, inb(PITAUX_PORT) | PIT_SPKR);	/* enable counter 2 */
		enable_intr();
	}
	last_pitch = pitch;
	beeping = 1;
	timeout(sysbeepstop, 0, period);
}
#endif

unsigned int delaycount;	/* calibrated loop variable (1 millisecond) */

#define FIRST_GUESS   0x2000

void
findcpuspeed()
{
	int i;
	int remainder;

	/* Put counter in count down mode */
	outb(TIMER_MODE, TIMER_SEL0 | TIMER_16BIT | TIMER_RATEGEN);
	outb(TIMER_CNTR0, 0xff);
	outb(TIMER_CNTR0, 0xff);
	for (i = FIRST_GUESS; i; i--)
		;
	/* Read the value left in the counter */
	remainder = gettick();
	/*
	 * Formula for delaycount is:
	 *  (loopcount * timer clock speed) / (counter ticks * 1000)
	 */
	delaycount = (FIRST_GUESS * TIMER_DIV(1000)) / (0xffff-remainder);
}

#if defined(I586_CPU) || defined(I686_CPU)
void
calibrate_cyclecounter()
{
	unsigned long long count, last_count;
#ifdef NTP
	extern long time_precision;
#endif

	__asm __volatile(".byte 0xf, 0x31" : "=A" (last_count));
	delay(1000000);
	__asm __volatile(".byte 0xf, 0x31" : "=A" (count));
	pentium_mhz = ((count - last_count) + 500000) / 1000000;
#ifdef NTP
	time_precision = 1;	/* XXX */
#endif
}
#endif

void
cpu_initclocks()
{
#if 0 /* EXOKERNEL: USER CONTROLS SPEED OF RTC INTERRUPT */
	stathz = 128;
	profhz = 1024;
#endif

	/*
	 * XXX If you're doing strange things with multiple clocks, you might
	 * want to keep track of clock handlers.
	 */
#if 0 /* EXOKERNEL REGISTERS HANDLERS DIFFERENTLY */
	(void)isa_intr_establish(NULL, 0, IST_PULSE, IPL_CLOCK, clockintr,
	    0, "clock");
	(void)isa_intr_establish(NULL, 8, IST_PULSE, IPL_CLOCK, rtcintr,
	    0, "rtc");

	mc146818_write(NULL, MC_REGA, MC_BASE_32_KHz | MC_RATE_128_Hz);
	mc146818_write(NULL, MC_REGB, MC_REGB_24HR | MC_REGB_PIE);
#else
	/* EXOKERNEL: THE FOLLOWING LINE SHOULD BE IN OPENBSD, TOO! */
	mc146818_read(NULL, MC_REGC); /* clear the way for future interrupts */
	/* Use default values for the real time clock.
	   Enable the periodic interrupts in REGB but disable them in
	   REGA - to be set by processes.
	*/
	mc146818_write(NULL, MC_REGA, 0);
	mc146818_write(NULL, MC_REGB, MC_REGB_24HR | MC_REGB_PIE);
	printf ("  Setup rtc interrupt via 8259A\n");
	irq_setmask_8259A(irq_mask_8259A & 0xfeff);
	// printf ("  unmasked rtc interrupt\n");
#endif
}

int
rtcget(regs)
	mc_todregs *regs;
{
	if ((mc146818_read(NULL, MC_REGD) & MC_REGD_VRT) == 0) /* XXX softc */
		return (-1);
	MC146818_GETTOD(NULL, regs);			/* XXX softc */
	return (0);
}	

void
rtcput(regs)
	mc_todregs *regs;
{
	MC146818_PUTTOD(NULL, regs);			/* XXX softc */
}

static int month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static int
yeartoday(year)
	int year;
{

	return (((year % 4) == 0 &&
		 ((year % 100) != 0 || (year % 400) == 0))? 366 : 365);
}

int
hexdectodec(n)
	int n;
{

	return (((n >> 4) & 0x0f) * 10 + (n & 0x0f));
}

int
dectohexdec(n)
	int n;
{

	return ((u_char)(((n / 10) << 4) & 0xf0) | ((n % 10) & 0x0f));
}

static int timeset;

/*
 * Initialize the time of day register, based on the time base which is, e.g.
 * from a filesystem.
 */
void
inittodr(base)
	time_t base;
{
	mc_todregs rtclk;
	time_t n;
	int sec, min, hr, dom, mon, yr;
	int i, days = 0;
	int s;

#if 0 /* EXOKERNEL: CURRENTLY INITS CLOCKS BEFORE READING DISK */
	/*
	 * We mostly ignore the suggested time and go for the RTC clock time
	 * stored in the CMOS RAM.  If the time can't be obtained from the
	 * CMOS, or if the time obtained from the CMOS is 5 or more years
	 * less than the suggested time, we used the suggested time.  (In
	 * the latter case, it's likely that the CMOS battery has died.)
	 */

	if (base < 15*SECYR) {	/* if before 1985, something's odd... */
		printf("WARNING: preposterous time in file system\n");
		/* read the system clock anyway */
		base = 17*SECYR + 186*SECDAY + SECDAY/2;
	}
#endif

	s = splclock();
	if (rtcget(&rtclk)) {
		splx(s);
		printf("WARNING: invalid time in clock chip\n");
		goto fstime;
	}
	splx(s);

	sec = hexdectodec(rtclk[MC_SEC]);
	min = hexdectodec(rtclk[MC_MIN]);
	hr = hexdectodec(rtclk[MC_HOUR]);
	dom = hexdectodec(rtclk[MC_DOM]);
	mon = hexdectodec(rtclk[MC_MONTH]);
	yr = hexdectodec(rtclk[MC_YEAR]);
	yr = (yr < 70) ? yr+100 : yr;

	n = sec + 60 * min + 3600 * hr;
	n += (dom - 1) * 3600 * 24;

	if (yeartoday(yr) == 366)
		month[1] = 29;
	for (i = mon - 2; i >= 0; i--)
		days += month[i];
	month[1] = 28;
	for (i = 70; i < yr; i++)
		days += yeartoday(i);
	n += days * 3600 * 24;

#if 0 /* EXOKERNEL: NO TIMEZONE INFO IN KERNEL */
	n += tz.tz_minuteswest * 60;
	if (tz.tz_dsttime)
		n -= 3600;
#endif

#if 0 /* EXOKERNEL: CURRENTLY INITS CLOCKS BEFORE READING DISK */
	if (base < n - 5*SECYR)
		printf("WARNING: file system time much less than clock time\n");
	else if (base > n + 5*SECYR) {
		printf("WARNING: clock time much less than file system time\n");
		printf("WARNING: using file system time\n");
		goto fstime;
	}
#endif

	timeset = 1;
#if 0 /* EXOKERNEL: DIFFERENT LOCATION FOR KERNEL TIME */
	time.tv_sec = n;
	time.tv_usec = 0;
#else
	SYSINFO_ASSIGN(si_startup_time, n);
#endif
	return;

fstime:
	timeset = 1;
#if 0 /* EXOKERNEL: DIFFERENT LOCATION FOR KERNEL TIME */
      /* EXOKERNEL: CURRENTLY INITS CLOCKS BEFORE READING DISK */
	time.tv_sec = base;
	time.tv_usec = 0;
#else
	SYSINFO_ASSIGN(si_startup_time, 0);
#endif
	printf("WARNING: CHECK AND RESET THE DATE!\n");
}

/*
 * Reset the clock.
 */
void
resettodr()
{
	mc_todregs rtclk;
	time_t n;
	int diff, i, j;
	int s;

	/*
	 * We might have been called by boot() due to a crash early
	 * on.  Don't reset the clock chip in this case.
	 */
	if (!timeset)
		return;

	s = splclock();
	if (rtcget(&rtclk))
		bzero(&rtclk, sizeof(rtclk));
	splx(s);

#if 0 /* EXOKERNEL: NO TIMEZONE INFO IN KERNEL */
	diff = tz.tz_minuteswest * 60;
	if (tz.tz_dsttime)
		diff -= 3600;
#else
	diff = 0;
#endif
#if 0 /* EXOKERNEL: DIFFERENT LOCATION FOR KERNEL TIME */
	n = (time.tv_sec - diff) % (3600 * 24);   /* hrs+mins+secs */
#else
	n = (SI_MAKETIME - diff) % (3600 * 24);   /* hrs+mins+secs */
#endif
	rtclk[MC_SEC] = dectohexdec(n % 60);
	n /= 60;
	rtclk[MC_MIN] = dectohexdec(n % 60);
	rtclk[MC_HOUR] = dectohexdec(n / 60);

#if 0 /* EXOKERNEL: DIFFERENT LOCATION FOR KERNEL TIME */
	n = (time.tv_sec - diff) / (3600 * 24);	/* days */
#else
	n = (SI_MAKETIME - diff) / (3600 * 24);	/* days */
#endif
	rtclk[MC_DOW] = (n + 4) % 7;  /* 1/1/70 is Thursday */

	for (j = 1970, i = yeartoday(j); n >= i; j++, i = yeartoday(j))
		n -= i;

	rtclk[MC_YEAR] = dectohexdec(j - 1900);

	if (i == 366)
		month[1] = 29;
	for (i = 0; n >= month[i]; i++)
		n -= month[i];
	month[1] = 28;
	rtclk[MC_MONTH] = dectohexdec(++i);

	rtclk[MC_DOM] = dectohexdec(++n);

	s = splclock();
	rtcput(&rtclk);
	splx(s);
}

#if 0 /* EXOKERNEL: USER HAS FULL CONTROL OVER RTC INTERRUPT RATE */
void
setstatclockrate(arg)
	int arg;
{
	if (arg == stathz)
		mc146818_write(NULL, MC_REGA, MC_BASE_32_KHz | MC_RATE_128_Hz);
	else
		mc146818_write(NULL, MC_REGA, MC_BASE_32_KHz | MC_RATE_1024_Hz);
}
#endif


#ifdef __ENCAP__
#include <xok/sysinfoP.h>
#endif

