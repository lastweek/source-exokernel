
/*
 * Copyright (C) 1997 Massachusetts Institute of Technology 
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
 * associated documentation will at all times remain with copyright
 * holders. See the file AUTHORS which should have accompanied this software
 * for a list of all copyright holders.
 *
 * This file may be derived from previously copyrighted software. This
 * copyright applies only to those changes made by the copyright
 * holders listed in the AUTHORS file. The rest of this file is covered by
 * the copyright notices, if any, listed below.
 */

/*	$OpenBSD: tty_subr.c,v 1.4 1996/06/06 09:52:07 deraadt Exp $	*/
/*	$NetBSD: tty_subr.c,v 1.13 1996/02/09 19:00:43 christos Exp $	*/

/*
 * Copyright (c) 1993, 1994 Theo de Raadt
 * All rights reserved.
 *
 * Per Lindqvist <pgd@compuram.bbt.se> supplied an almost fully working
 * set of true clist functions that this is very loosely based on.
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
 *	This product includes software developed by Theo de Raadt.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/proc.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <assert.h>
#include <memory.h>
#include <stdlib.h>

#include "npty.h"
/*
 * At compile time, choose:
 * There are two ways the TTY_QUOTE bit can be stored. If QBITS is
 * defined we allocate an array of bits -- 1/8th as much memory but
 * setbit(), clrbit(), and isset() take more cpu. If QBITS is
 * undefined, we just use an array of bytes.
 * 
 * If TTY_QUOTE functionality isn't required by a line discipline,
 * it can free c_cq and set it to NULL. This speeds things up,
 * and also does not use any extra memory. This is useful for (say)
 * a SLIP line discipline that wants a 32K ring buffer for data
 * but doesn't need quoting.
 */

void	cinit (void);
int	ndqb (struct clist *, int);
int	cl_putc (int, struct clist *);
#ifdef QBITS
void	clrbits (u_char *, int, int);
#endif
int	b_to_q (u_char *, int, struct clist *);
u_char *firstc (struct clist *, int *);

/*
 * Initialize clists.
 */
void
cinit()
{
#ifdef STATIC_ALLOCATION
  bzero(npty_shared_data->used,NCLISTSC);
#endif
}

/*
 * Initialize a particular clist. Ok, they are really ring buffers,
 * of the specified length, with/without quoting support.
 */
int
clalloc(clp, size, quot)
	struct clist *clp;
	int size;
	int quot;
{
#ifdef STATIC_ALLOCATION
  int i;
  demand(size == CLALLOCSZ, hard coded for this size using static allocation);
  for (i = 0; i < NCLISTS; i++) {
    if (isclr(npty_shared_data->used,i)) goto good;
  }
  return -1;
good:
  assert(i >= 0 && i < NCLISTS);
  setbit(npty_shared_data->used,i);

  clp->c_cs = npty_shared_data->c_cs[i];
  bzero(clp->c_cs, size);
  if(quot) {
    clp->c_cq = npty_shared_data->c_cq[i];
    bzero(clp->c_cq, QMEM(size));
  } else
    clp->c_cq = (u_char *)0;

#else
  MALLOC(clp->c_cs, u_char *, size, M_TTYS, M_WAITOK);
  if (!clp->c_cs)
    return (-1);
  bzero(clp->c_cs, size);
  
  if(quot) {
    MALLOC(clp->c_cq, u_char *, QMEM(size), M_TTYS, M_WAITOK);
    if (!clp->c_cq) {
      FREE(clp->c_cs, M_TTYS);
      clp->c_cs = (u_char *)0;
      return (-1);
    }
    bzero(clp->c_cq, QMEM(size));
  } else
    clp->c_cq = (u_char *)0;
  
#endif
  clp->c_cf = clp->c_cl = (u_char *)0;
  clp->c_ce = clp->c_cs + size;
  clp->c_cn = size;
  clp->c_cc = 0;
  return (0);
}

void
clfree(clp)
	struct clist *clp;
{
#ifdef STATIC_ALLOCATION
  unsigned int i;
  i = ((unsigned int) clp->c_cs - (unsigned int)npty_shared_data->c_cs)
    /CLALLOCSZ;
  assert(i >= 0 && i < NCLISTS);
  clrbit(npty_shared_data->used,i);
  clp->c_cs = clp->c_cq = (u_char *)0;
#else
	if(clp->c_cs)
		FREE(clp->c_cs, M_TTYS);
	if(clp->c_cq)
		FREE(clp->c_cq, M_TTYS);
	clp->c_cs = clp->c_cq = (u_char *)0;
#endif

}


/*
 * Get a character from a clist.
 */
int
cl_getc(clp)
	struct clist *clp;
{
	register int c = -1;
	int s;

	s = spltty();
	if (clp->c_cc == 0)
		goto out;

	c = *clp->c_cf & 0xff;
	if (clp->c_cq) {
#ifdef QBITS
		if (isset(clp->c_cq, clp->c_cf - clp->c_cs) )
			c |= TTY_QUOTE;
#else
		if (*(clp->c_cf - clp->c_cs + clp->c_cq))
			c |= TTY_QUOTE;
#endif
	}
	if (++clp->c_cf == clp->c_ce)
		clp->c_cf = clp->c_cs;
	if (--clp->c_cc == 0)
		clp->c_cf = clp->c_cl = (u_char *)0;
out:
	splx(s);
	return c;
}

/*
 * Copy clist to buffer.
 * Return number of bytes moved.
 */
int
q_to_b(clp, cp, count)
	struct clist *clp;
	u_char *cp;
	int count;
{
	register int cc;
	u_char *p = cp;
	int s;

	s = spltty();
	/* optimize this while loop */
	while (count > 0 && clp->c_cc > 0) {
		cc = clp->c_cl - clp->c_cf;
		if (clp->c_cf >= clp->c_cl)
			cc = clp->c_ce - clp->c_cf;
		if (cc > count)
			cc = count;
		bcopy(clp->c_cf, p, cc);
		count -= cc;
		p += cc;
		clp->c_cc -= cc;
		clp->c_cf += cc;
		if (clp->c_cf == clp->c_ce)
			clp->c_cf = clp->c_cs;
	}
	if (clp->c_cc == 0)
		clp->c_cf = clp->c_cl = (u_char *)0;
	splx(s);
	return p - cp;
}

/*
 * Return count of contiguous characters in clist.
 * Stop counting if flag&character is non-null.
 */
int
ndqb(clp, flag)
	struct clist *clp;
	int flag;
{
	int count = 0;
	register int i;
	register int cc;
	int s;

	s = spltty();
	if ((cc = clp->c_cc) == 0)
		goto out;

	if (flag == 0) {
		count = clp->c_cl - clp->c_cf;
		if (count <= 0)
			count = clp->c_ce - clp->c_cf;
		goto out;
	}

	i = clp->c_cf - clp->c_cs;
	if (flag & TTY_QUOTE) {
		while (cc-- > 0 && !(clp->c_cs[i++] & (flag & ~TTY_QUOTE) ||
		    isset(clp->c_cq, i))) {
			count++;
			if (i == clp->c_cn)
				break;
		}
	} else {
		while (cc-- > 0 && !(clp->c_cs[i++] & flag)) {
			count++;
			if (i == clp->c_cn)
				break;
		}
	}
out:
	splx(s);
	return count;
}

/*
 * Flush count bytes from clist.
 */
void
ndflush(clp, count)
	struct clist *clp;
	int count;
{
	register int cc;
	int s;

	s = spltty();
	if (count == clp->c_cc) {
		clp->c_cc = 0;
		clp->c_cf = clp->c_cl = (u_char *)0;
		goto out;
	}
	/* optimize this while loop */
	while (count > 0 && clp->c_cc > 0) {
		cc = clp->c_cl - clp->c_cf;
		if (clp->c_cf >= clp->c_cl)
			cc = clp->c_ce - clp->c_cf;
		if (cc > count)
			cc = count;
		count -= cc;
		clp->c_cc -= cc;
		clp->c_cf += cc;
		if (clp->c_cf == clp->c_ce)
			clp->c_cf = clp->c_cs;
	}
	if (clp->c_cc == 0)
		clp->c_cf = clp->c_cl = (u_char *)0;
out:
	splx(s);
}

/*
 * Put a character into the output queue.
 */
int
cl_putc(c, clp)
	int c;
	struct clist *clp;
{
	register int i;
	int s;

	s = spltty();
	if (clp->c_cc == clp->c_cn)
		goto out;

	if (clp->c_cc == 0) {
		if (!clp->c_cs) {
#if defined(DIAGNOSTIC) || 1
			printf("putc: required clalloc\n");
#endif
			if(clalloc(clp, CLALLOCSZ, 1)) {
out:
				splx(s);
				return -1;
			}
		}
		clp->c_cf = clp->c_cl = clp->c_cs;
	}

	*clp->c_cl = c & 0xff;
	i = clp->c_cl - clp->c_cs;
	if (clp->c_cq) {
#ifdef QBITS
		if (c & TTY_QUOTE)
			setbit(clp->c_cq, i); 
		else
			clrbit(clp->c_cq, i);
#else
		q = clp->c_cq + i;
		*q = (c & TTY_QUOTE) ? 1 : 0;
#endif
	}
	clp->c_cc++;
	clp->c_cl++;
	if (clp->c_cl == clp->c_ce)
		clp->c_cl = clp->c_cs;
	splx(s);
	return 0;
}

#ifdef QBITS
/*
 * optimized version of
 *
 * for (i = 0; i < len; i++)
 *	clrbit(cp, off + len);
 */
void
clrbits(cp, off, len)
	u_char *cp;
	int off;
	int len;
{
	int sby, sbi, eby, ebi;
	register int i;
	u_char mask;

	if(len==1) {
		clrbit(cp, off);
		return;
	}

	sby = off / NBBY;
	sbi = off % NBBY;
	eby = (off+len) / NBBY;
	ebi = (off+len) % NBBY;
	if (sby == eby) {
		mask = ((1 << (ebi - sbi)) - 1) << sbi;
		cp[sby] &= ~mask;
	} else {
		mask = (1<<sbi) - 1;
		cp[sby++] &= mask;

		mask = (1<<ebi) - 1;
		cp[eby] &= ~mask;

		for (i = sby; i < eby; i++)
			cp[i] = 0x00;
	}
}
#endif

/*
 * Copy buffer to clist.
 * Return number of bytes not transfered.
 */
int
b_to_q(cp, count, clp)
	u_char *cp;
	int count;
	struct clist *clp;
{
	register int cc;
	register u_char *p = cp;
	int s;

	if (count <= 0)
		return 0;

	s = spltty();
	if (clp->c_cc == clp->c_cn)
		goto out;

	if (clp->c_cc == 0) {
		if (!clp->c_cs) {
#if defined(DIAGNOSTIC) || 1
			printf("b_to_q: required clalloc\n");
#endif
			if(clalloc(clp, CLALLOCSZ, 1))
				goto out;
		}
		clp->c_cf = clp->c_cl = clp->c_cs;
	}

	/* optimize this while loop */
	while (count > 0 && clp->c_cc < clp->c_cn) {
		cc = clp->c_ce - clp->c_cl;
		if (clp->c_cf > clp->c_cl)
			cc = clp->c_cf - clp->c_cl;
		if (cc > count)
			cc = count;
		bcopy(p, clp->c_cl, cc);
		if (clp->c_cq) {
#ifdef QBITS
			clrbits(clp->c_cq, clp->c_cl - clp->c_cs, cc);
#else
			bzero(clp->c_cl - clp->c_cs + clp->c_cq, cc);
#endif
		}
		p += cc;
		count -= cc;
		clp->c_cc += cc;
		clp->c_cl += cc;
		if (clp->c_cl == clp->c_ce)
			clp->c_cl = clp->c_cs;
	}
out:
	splx(s);
	return count;
}

static int cc;

/*
 * Given a non-NULL pointer into the clist return the pointer
 * to the next character in the list or return NULL if no more chars.
 *
 * Callers must not allow getc's to happen between firstc's and getc's
 * so that the pointer becomes invalid.  Note that interrupts are NOT
 * masked.
 */
u_char *
nextc(clp, cp, c)
	struct clist *clp;
	register u_char *cp;
	int *c;
{

	if (clp->c_cf == cp) {
		/*
		 * First time initialization.
		 */
		cc = clp->c_cc;
	}
	if (cc == 0 || cp == NULL)
		return NULL;
	if (--cc == 0)
		return NULL;
	if (++cp == clp->c_ce)
		cp = clp->c_cs;
	*c = *cp & 0xff;
	if (clp->c_cq) {
#ifdef QBITS
		if (isset(clp->c_cq, cp - clp->c_cs))
			*c |= TTY_QUOTE;
#else
		if (*(clp->c_cf - clp->c_cs + clp->c_cq))
			*c |= TTY_QUOTE;
#endif
	}
	return cp;
}

/*
 * Given a non-NULL pointer into the clist return the pointer
 * to the first character in the list or return NULL if no more chars.
 *
 * Callers must not allow getc's to happen between firstc's and getc's
 * so that the pointer becomes invalid.  Note that interrupts are NOT
 * masked.
 *
 * *c is set to the NEXT character
 */
u_char *
firstc(clp, c)
	struct clist *clp;
	int *c;
{
	register u_char *cp;

	cc = clp->c_cc;
	if (cc == 0)
		return NULL;
	cp = clp->c_cf;
	*c = *cp & 0xff;
	if(clp->c_cq) {
#ifdef QBITS
		if (isset(clp->c_cq, cp - clp->c_cs))
			*c |= TTY_QUOTE;
#else
		if (*(cp - clp->c_cs + clp->c_cq))
			*c |= TTY_QUOTE;
#endif
	}
	return clp->c_cf;
}

/*
 * Remove the last character in the clist and return it.
 */
int
cl_unputc(clp)
	struct clist *clp;
{
	unsigned int c = -1;
	int s;

	s = spltty();
	if (clp->c_cc == 0)
		goto out;

	if (clp->c_cl == clp->c_cs)
		clp->c_cl = clp->c_ce - 1;
	else
		--clp->c_cl;
	clp->c_cc--;

	c = *clp->c_cl & 0xff;
	if (clp->c_cq) {
#ifdef QBITS
		if (isset(clp->c_cq, clp->c_cl - clp->c_cs))
			c |= TTY_QUOTE;
#else
		if (*(clp->c_cf - clp->c_cs + clp->c_cq))
			c |= TTY_QUOTE;
#endif
	}
	if (clp->c_cc == 0)
		clp->c_cf = clp->c_cl = (u_char *)0;
out:
	splx(s);
	return c;
}

/*
 * Put the chars in the from queue on the end of the to queue.
 */
void
catq(from, to)
	struct clist *from, *to;
{
	int c;
	int s;

	s = spltty();
	if (from->c_cc == 0) {	/* nothing to move */
		splx(s);
		return;
	}

	/*
	 * if `to' queue is empty and the queues are the same max size,
	 * it is more efficient to just swap the clist structures.
	 */
	if (to->c_cc == 0 && from->c_cn == to->c_cn) {
		struct clist tmp;

		tmp = *from;
		*from = *to;
		*to = tmp;
		splx(s);
		return;
	}
	splx(s);

	while ((c = cl_getc(from)) != -1)
	  cl_putc(c, to);
}

