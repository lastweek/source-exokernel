
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

/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char rcsid[] = "$OpenBSD: malloc.c,v 1.30 1998/01/02 05:32:49 deraadt Exp $";
#endif /* LIBC_SCCS and not lint */

/*
 * Defining MALLOC_EXTRA_SANITY will enable extra checks which are
 * related to internal conditions and consistency in malloc.c. This has
 * a noticeable runtime performance hit, and generally will not do you
 * any good unless you fiddle with the internals of malloc or want
 * to catch random pointer corruption as early as possible.
 */
#ifndef MALLOC_EXTRA_SANITY
#undef MALLOC_EXTRA_SANITY
#endif

/*
 * Defining MALLOC_STATS will enable you to call malloc_dump() and set
 * the [dD] options in the MALLOC_OPTIONS environment variable.
 * It has no run-time performance hit, but does pull in stdio...
 */
#ifndef MALLOC_STATS
#undef MALLOC_STATS
#endif

/*
 * What to use for Junk.  This is the byte value we use to fill with
 * when the 'J' option is enabled.
 */
#define SOME_JUNK	0xd0		/* as in "Duh" :-) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <exos/mallocs.h>


/*
 * The basic parameters you can tweak.
 *
 * malloc_pageshift	pagesize = 1 << malloc_pageshift
 *			It's probably best if this is the native
 *			page size, but it shouldn't have to be.
 *
 * malloc_minsize	minimum size of an allocation in bytes.
 *			If this is too small it's too much work
 *			to manage them.  This is also the smallest
 *			unit of alignment used for the storage
 *			returned by malloc/realloc.
 *
 */

#if defined(__i386__) && defined(__FreeBSD__)
#   define malloc_pageshift		12U
#   define malloc_minsize		16U
#endif /* __i386__ && __FreeBSD__ */

#if defined(__sparc__) && !defined(__OpenBSD__)
#   define malloc_pageshirt		12U
#   define malloc_minsize		16U
#   define MAP_ANON			(0)
#   define USE_DEV_ZERO
#   define MADV_FREE			MADV_DONTNEED
#endif /* __sparc__ */

/* Insert your combination here... */
#if defined(__FOOCPU__) && defined(__BAROS__)
#   define malloc_pageshift		12U
#   define malloc_minsize		16U
#endif /* __FOOCPU__ && __BAROS__ */

#ifdef __OpenBSD__
#   if defined(__alpha__) || defined(__m68k__) || defined(__mips__) || \
       defined(__i386__) || defined(__m88k__) || defined(__ns32k__) || \
       defined(__vax__)
#	define	malloc_pageshift	(PGSHIFT)
#	define	malloc_minsize		16U
#   endif /* __i386__ */
#endif /* __OpenBSD__ */

#ifdef _THREAD_SAFE
#include <pthread.h>
static pthread_mutex_t malloc_lock;
#define THREAD_LOCK()		pthread_mutex_lock(&malloc_lock)
#define THREAD_UNLOCK()		pthread_mutex_unlock(&malloc_lock)
#define THREAD_LOCK_INIT()	pthread_mutex_init(&malloc_lock, 0);
#else
#define THREAD_LOCK()
#define THREAD_UNLOCK()
#define THREAD_LOCK_INIT()
#endif

/*
 * No user serviceable parts behind this point.
 *
 * This structure describes a page worth of chunks.
 */

struct pginfo {
    struct pginfo	*next;	/* next on the free list */
    void		*page;	/* Pointer to the page */
    u_short		size;	/* size of this page's chunks */
    u_short		shift;	/* How far to shift for this size chunks */
    u_short		free;	/* How many free chunks */
    u_short		total;	/* How many chunk */
    u_long		bits[1]; /* Which chunks are free */
};

/*
 * This structure describes a number of free pages.
 */

struct pgfree {
    struct pgfree	*next;	/* next run of free pages */
    struct pgfree	*prev;	/* prev run of free pages */
    void		*page;	/* pointer to free pages */
    void		*end;	/* pointer to end of free pages */
    u_long		size;	/* number of bytes free */
};

/*
 * How many bits per u_long in the bitmap.
 * Change only if not 8 bits/byte
 */
#define	MALLOC_BITS	(8*sizeof(u_long))

/*
 * Magic values to put in the page_directory
 */
#define MALLOC_NOT_MINE	((struct pginfo*) 0)
#define MALLOC_FREE 	((struct pginfo*) 1)
#define MALLOC_FIRST	((struct pginfo*) 2)
#define MALLOC_FOLLOW	((struct pginfo*) 3)
#define MALLOC_MAGIC	((struct pginfo*) 4)

#ifndef malloc_pageshift
#define malloc_pageshift		12U
#endif

#ifndef malloc_minsize
#define malloc_minsize			16U
#endif

#ifndef malloc_pageshift
#error	"malloc_pageshift undefined"
#endif

#if !defined(malloc_pagesize)
#define malloc_pagesize			(1UL<<malloc_pageshift)
#endif

#if ((1UL<<malloc_pageshift) != malloc_pagesize)
#error	"(1UL<<malloc_pageshift) != malloc_pagesize"
#endif

#ifndef malloc_maxsize
#define malloc_maxsize			((malloc_pagesize)>>1)
#endif

/* A mask for the offset inside a page.  */
#define malloc_pagemask	((malloc_pagesize)-1)

#define pageround(foo) (((foo) + (malloc_pagemask))&(~(malloc_pagemask)))
#define ptr2index(foo) (((u_long)(foo) >> malloc_pageshift)-malloc_origo)

/* fd of /dev/zero */
#ifdef USE_DEV_ZERO
static int fdzero;
#define	MMAP_FD	fdzero
#define INIT_MMAP() \
	{ if ((fdzero=open("/dev/zero", O_RDWR, 0000)) == -1) \
	    wrterror("open of /dev/zero"); }
#else
#define MMAP_FD (-1)
#define INIT_MMAP()
#endif

/* Set when initialization has been done */
static unsigned malloc_started;	

/* Number of free pages we cache */
static unsigned malloc_cache = 16;

/* The offset from pagenumber to index into the page directory */
static u_long malloc_origo;

/* The last index in the page directory we care about */
static u_long last_index;

/* Pointer to page directory. Allocated "as if with" malloc */
static struct	pginfo **page_dir;

/* How many slots in the page directory */
static size_t	malloc_ninfo;

/* Free pages line up here */
static struct pgfree free_list;

/* Abort(), user doesn't handle problems.  */
static int malloc_abort;

/* Are we trying to die ?  */
static int suicide;

#ifdef MALLOC_STATS
/* dump statistics */
static int malloc_stats;
#endif

/* avoid outputting warnings?  */
static int malloc_silent;

/* always realloc ?  */
static int malloc_realloc;

#ifdef __FreeBSD__
/* pass the kernel a hint on free pages ?  */
static int malloc_hint;
#endif

/* xmalloc behaviour ?  */
static int malloc_xmalloc;

/* zero fill ?  */
static int malloc_zero;

/* junk fill ?  */
static int malloc_junk;

#ifdef __FreeBSD__
/* utrace ?  */
static int malloc_utrace;

struct ut { void *p; size_t s; void *r; };

void utrace __P((struct ut *, int));

#define UTRACE(a, b, c) \
	if (malloc_utrace) \
		{struct ut u; u.p=a; u.s = b; u.r=c; utrace(&u, sizeof u);}
#else /* !__FreeBSD__ */
#define UTRACE(a,b,c)
#endif

/* my last break. */
static void *malloc_brk;

/* one location cache for free-list holders */
static struct pgfree *px;

/* compile-time options */
char *__malloc_options;

/* Name of the current public function */
static char *malloc_func;

/* Macro for mmap */
static inline void *MMAP(size_t size) {
  void *ret = exos_pinned_malloc(size);

  if (!ret) return (void*)-1;
  bzero(ret, size);

  return ret;
}

/*
 * Necessary function declarations
 */
static int extend_pgdir(u_long index);
static void *imalloc(size_t size);
static void ifree(void *ptr, int num_pages);
static void *irealloc(void *ptr, size_t size);
static void *malloc_bytes(size_t size);

#ifdef MALLOC_STATS
void
__malloc_dump(fd)
    FILE *fd;
{
    struct pginfo **pd;
    struct pgfree *pf;
    int j;

    pd = page_dir;

    /* print out all the pages */
    for(j=0;j<=last_index;j++) {
	fprintf(fd, "%08lx %5d ", (j+malloc_origo) << malloc_pageshift, j);
	if (pd[j] == MALLOC_NOT_MINE) {
	    for(j++;j<=last_index && pd[j] == MALLOC_NOT_MINE;j++)
		;
	    j--;
	    fprintf(fd, ".. %5d not mine\n",	j);
	} else if (pd[j] == MALLOC_FREE) {
	    for(j++;j<=last_index && pd[j] == MALLOC_FREE;j++)
		;
	    j--;
	    fprintf(fd, ".. %5d free\n", j);
	} else if (pd[j] == MALLOC_FIRST) {
	    for(j++;j<=last_index && pd[j] == MALLOC_FOLLOW;j++)
		;
	    j--;
	    fprintf(fd, ".. %5d in use\n", j);
	} else if (pd[j] < MALLOC_MAGIC) {
	    fprintf(fd, "(%p)\n", pd[j]);
	} else {
	    fprintf(fd, "%p %d (of %d) x %d @ %p --> %p\n",
		pd[j], pd[j]->free, pd[j]->total,
		pd[j]->size, pd[j]->page, pd[j]->next);
	}
    }

    for(pf=free_list.next; pf; pf=pf->next) {
	fprintf(fd, "Free: @%p [%p...%p[ %ld ->%p <-%p\n",
		pf, pf->page, pf->end, pf->size, pf->prev, pf->next);
	if (pf == pf->next) {
 		fprintf(fd, "Free_list loops.\n");
		break;
	}
    }

    /* print out various info */
    fprintf(fd, "Minsize\t%d\n", malloc_minsize);
    fprintf(fd, "Maxsize\t%d\n", malloc_maxsize);
    fprintf(fd, "Pagesize\t%lu\n", (u_long)malloc_pagesize);
    fprintf(fd, "Pageshift\t%d\n", malloc_pageshift);
    fprintf(fd, "FirstPage\t%ld\n", malloc_origo);
    fprintf(fd, "LastPage\t%ld %lx\n", last_index+malloc_pageshift,
	(last_index + malloc_pageshift) << malloc_pageshift);
    fprintf(fd, "Break\t%ld\n", (u_long)__sbrk(0) >> malloc_pageshift);
}
#endif /* MALLOC_STATS */

extern char *__progname;

static void
wrterror(p)
    char *p;
{
#if 0
    char *q = " error: ";
    write(2, __progname, strlen(__progname));
    write(2, malloc_func, strlen(malloc_func));
    write(2, q, strlen(q));
    write(2, p, strlen(p));
#endif
    suicide = 1;
#ifdef MALLOC_STATS
    if (malloc_stats)
	__malloc_dump(stderr);
#endif /* MALLOC_STATS */
    abort();
}

static void
wrtwarning(p)
    char *p;
{
#if 0
    char *q = " warning: ";
    if (malloc_abort)
	wrterror(p);
    else if (malloc_silent)
	return;
    write(2, __progname, strlen(__progname));
    write(2, malloc_func, strlen(malloc_func));
    write(2, q, strlen(q));
    write(2, p, strlen(p));
#endif
}

#ifdef MALLOC_STATS
static void
malloc_exit()
{
#if 0
    FILE *fd = fopen("malloc.out", "a");
    char *q = "malloc() warning: Couldn't dump stats.\n";
    if (fd) {
        __malloc_dump(fd);
	fclose(fd);
    } else
	write(2, q, strlen(q));
#endif
}
#endif /* MALLOC_STATS */


/*
 * Allocate a number of pages from the OS
 */
static void *
map_pages(pages)
    int pages;
{
    caddr_t result, tail;

    result = (caddr_t)pageround((u_long)__sbrk(0));
    tail = result + (pages << malloc_pageshift);

    if (__brk(tail)) {
#ifdef MALLOC_EXTRA_SANITY
	wrterror("(ES): map_pages fails\n");
#endif /* MALLOC_EXTRA_SANITY */
	return 0;
    }

    last_index = ptr2index(tail) - 1;
    malloc_brk = tail;

    if ((last_index+1) >= malloc_ninfo && !extend_pgdir(last_index))
	return 0;

    return result;
}

/*
 * Extend page directory
 */
static int
extend_pgdir(index)
    u_long index;
{
    struct  pginfo **new, **old;
    size_t i, oldlen;

    /* Make it this many pages */
    i = index * sizeof *page_dir;
    i /= malloc_pagesize;
    i += 2;

    /* remember the old mapping size */
    oldlen = malloc_ninfo * sizeof *page_dir;

    /*
     * NOTE: we allocate new pages and copy the directory rather than tempt
     * fate by trying to "grow" the region.. There is nothing to prevent
     * us from accidently re-mapping space that's been allocated by our caller
     * via dlopen() or other mmap().
     *
     * The copy problem is not too bad, as there is 4K of page index per
     * 4MB of malloc arena.
     *
     * We can totally avoid the copy if we open a file descriptor to associate
     * the anon mappings with.  Then, when we remap the pages at the new
     * address, the old pages will be "magically" remapped..  But this means
     * keeping open a "secret" file descriptor.....
     */

#if 0
    /* Get new pages */
    new = (struct pginfo**) MMAP(i * malloc_pagesize);
    if (new == (struct pginfo **)-1)
	return 0;

    /* Copy the old stuff */
    memcpy(new, page_dir,
	    malloc_ninfo * sizeof *page_dir);
#else
    new = (struct pginfo**) exos_pinned_realloc(page_dir,
						i * malloc_pagesize);
    if (new == (struct pginfo **)-1)
        return 0;
#endif

    /* register the new size */
    malloc_ninfo = i * malloc_pagesize / sizeof *page_dir;

    /* swap the pointers */
    old = page_dir;
    page_dir = new;

#if 0
    /* Now free the old stuff */
    munmap(old, oldlen);
#endif
    return 1;
}

/*
 * Initialize the world
 */
static void
malloc_init ()
{
    char *p = NULL/*, b[64]*/;
    int i, j;
    int save_errno = errno;

    THREAD_LOCK_INIT();

    INIT_MMAP();

#ifdef MALLOC_EXTRA_SANITY
    malloc_junk = 1;
#endif /* MALLOC_EXTRA_SANITY */

    for (i = 0; i < 3; i++) {
	if (i == 0) {
	  continue;
#if 0
	    j = readlink("/etc/malloc.conf", b, sizeof b - 1);
	    if (j <= 0)
		continue;
	    b[j] = '\0';
	    p = b;
#endif
	} else if (i == 1) {
	    if (issetugid() == 0)
		p = getenv("MALLOC_OPTIONS");
	    else
	    	continue;
	} else if (i == 2) {
	    p = __malloc_options;
	}
	for (; p && *p; p++) {
	    switch (*p) {
		case '>': malloc_cache   <<= 1; break;
		case '<': malloc_cache   >>= 1; break;
		case 'a': malloc_abort   = 0; break;
		case 'A': malloc_abort   = 1; break;
#ifdef MALLOC_STATS
		case 'd': malloc_stats   = 0; break;
		case 'D': malloc_stats   = 1; break;
#endif /* MALLOC_STATS */
#ifdef __FreeBSD__
		case 'h': malloc_hint    = 0; break;
		case 'H': malloc_hint    = 1; break;
#endif /* __FreeBSD__ */
		case 'r': malloc_realloc = 0; break;
		case 'R': malloc_realloc = 1; break;
		case 'j': malloc_junk    = 0; break;
		case 'J': malloc_junk    = 1; break;
		case 'n': malloc_silent  = 0; break;
		case 'N': malloc_silent  = 1; break;
#ifdef __FreeBSD__
		case 'u': malloc_utrace  = 0; break;
		case 'U': malloc_utrace  = 1; break;
#endif /* __FreeBSD__ */
		case 'x': malloc_xmalloc = 0; break;
		case 'X': malloc_xmalloc = 1; break;
		case 'z': malloc_zero    = 0; break;
		case 'Z': malloc_zero    = 1; break;
		default:
		    j = malloc_abort;
		    malloc_abort = 0;
		    wrtwarning("unknown char in MALLOC_OPTIONS\n");
		    malloc_abort = j;
		    break;
	    }
	}
    }

    UTRACE(0, 0, 0);

    /*
     * We want junk in the entire allocation, and zero only in the part
     * the user asked for.
     */
    if (malloc_zero)
	malloc_junk=1;

#ifdef MALLOC_STATS
    if (malloc_stats)
	atexit(malloc_exit);
#endif /* MALLOC_STATS */

    /* Allocate one page for the page directory */
    page_dir = (struct pginfo **) MMAP(malloc_pagesize);

    if (page_dir == (struct pginfo **) -1)
	wrterror("mmap(2) failed, check limits.\n");

    /*
     * We need a maximum of malloc_pageshift buckets, steal these from the
     * front of the page_directory;
     */
    malloc_origo = ((u_long)pageround((u_long)__sbrk(0))) >> malloc_pageshift;
    malloc_origo -= malloc_pageshift;

    malloc_ninfo = malloc_pagesize / sizeof *page_dir;

    /* Been here, done that */
    malloc_started++;

    /* Recalculate the cache size in bytes, and make sure it's nonzero */

    if (!malloc_cache)
	malloc_cache++;

    malloc_cache <<= malloc_pageshift;

    /*
     * This is a nice hack from Kaleb Keithly (kaleb@x.org).
     * We can sbrk(2) further back when we keep this on a low address.
     */
    px = (struct pgfree *) imalloc (sizeof *px);
    errno = save_errno;
}

/*
 * Allocate a number of complete pages
 */
static void *
malloc_pages(size)
    size_t size;
{
    void *p, *delay_free = 0;
    int i;
    struct pgfree *pf;
    u_long index;

    size = pageround(size);

    p = 0;
    /* Look for free pages before asking for more */
    for(pf = free_list.next; pf; pf = pf->next) {

#ifdef MALLOC_EXTRA_SANITY
	if (pf->size & malloc_pagemask)
	    wrterror("(ES): junk length entry on free_list\n");
	if (!pf->size)
	    wrterror("(ES): zero length entry on free_list\n");
	if (pf->page == pf->end)
	    wrterror("(ES): zero entry on free_list\n");
	if (pf->page > pf->end) 
	    wrterror("(ES): sick entry on free_list\n");
	if ((void*)pf->page >= (void*)__sbrk(0))
	    wrterror("(ES): entry on free_list past brk\n");
	if (page_dir[ptr2index(pf->page)] != MALLOC_FREE) 
	    wrterror("(ES): non-free first page on free-list\n");
	if (page_dir[ptr2index(pf->end)-1] != MALLOC_FREE)
	    wrterror("(ES): non-free last page on free-list\n");
#endif /* MALLOC_EXTRA_SANITY */

	if (pf->size < size)
	    continue;

	if (pf->size == size) {
	    p = pf->page;
	    if (pf->next)
		    pf->next->prev = pf->prev;
	    pf->prev->next = pf->next;
	    delay_free = pf;
	    break;
	} 

	p = pf->page;
	pf->page = (char *)pf->page + size;
	pf->size -= size;
	break;
    }

#ifdef MALLOC_EXTRA_SANITY
    if (p && page_dir[ptr2index(p)] != MALLOC_FREE)
	wrterror("(ES): allocated non-free page on free-list\n");
#endif /* MALLOC_EXTRA_SANITY */

    size >>= malloc_pageshift;

    /* Map new pages */
    if (!p)
	p = map_pages(size);

    if (p) {

	index = ptr2index(p);
	page_dir[index] = MALLOC_FIRST;
	for (i=1;i<size;i++)
	    page_dir[index+i] = MALLOC_FOLLOW;

	if (malloc_junk)
	    memset(p, SOME_JUNK, size << malloc_pageshift);
    }

    if (delay_free) {
	if (!px)
	    px = delay_free;
	else
	    ifree(delay_free, 0);
    }

    return p;
}

/*
 * Allocate a page of fragments
 */

static __inline__ int
malloc_make_chunks(bits)
    int bits;
{
    struct  pginfo *bp;
    void *pp;
    int i, k, l;

    /* Allocate a new bucket */
    pp = malloc_pages((size_t)malloc_pagesize);
    if (!pp)
	return 0;

    /* Find length of admin structure */
    l = sizeof *bp - sizeof(u_long);
    l += sizeof(u_long) *
	(((malloc_pagesize >> bits)+MALLOC_BITS-1) / MALLOC_BITS);

    /* Don't waste more than two chunks on this */
    if ((1UL<<(bits)) <= l+l) {
	bp = (struct  pginfo *)pp;
    } else {
	bp = (struct  pginfo *)imalloc(l);
	if (!bp) {
	    ifree(pp, 0);
	    return 0;
	}
    }

    bp->size = (1UL<<bits);
    bp->shift = bits;
    bp->total = bp->free = malloc_pagesize >> bits;
    bp->page = pp;

    /* set all valid bits in the bitmap */
    k = bp->total;
    i = 0;

    /* Do a bunch at a time */
    for(;k-i >= MALLOC_BITS; i += MALLOC_BITS)
	bp->bits[i / MALLOC_BITS] = ~0UL;

    for(; i < k; i++)
        bp->bits[i/MALLOC_BITS] |= 1UL<<(i%MALLOC_BITS);

    if (bp == bp->page) {
	/* Mark the ones we stole for ourselves */
	for(i=0;l > 0;i++) {
	    bp->bits[i/MALLOC_BITS] &= ~(1UL<<(i%MALLOC_BITS));
	    bp->free--;
	    bp->total--;
	    l -= (1 << bits);
	}
    }

    /* MALLOC_LOCK */

    page_dir[ptr2index(pp)] = bp;

    bp->next = page_dir[bits];
    page_dir[bits] = bp;

    /* MALLOC_UNLOCK */

    return 1;
}

/*
 * Allocate a fragment
 */
static void *
malloc_bytes(size)
    size_t size;
{
    int i,j;
    u_long u;
    struct  pginfo *bp;
    int k;
    u_long *lp;

    /* Don't bother with anything less than this */
    if (size < malloc_minsize)
	size = malloc_minsize;

    /* Find the right bucket */
    j = 1;
    i = size-1;
    while (i >>= 1)
	j++;

    /* If it's empty, make a page more of that size chunks */
    if (!page_dir[j] && !malloc_make_chunks(j))
	return 0;

    bp = page_dir[j];

    /* Find first word of bitmap which isn't empty */
    for (lp = bp->bits; !*lp; lp++)
	;

    /* Find that bit, and tweak it */
    u = 1;
    k = 0;
    while (!(*lp & u)) {
	u += u;
	k++;
    }
    *lp ^= u;

    /* If there are no more free, remove from free-list */
    if (!--bp->free) {
	page_dir[j] = bp->next;
	bp->next = 0;
    }

    /* Adjust to the real offset of that chunk */
    k += (lp-bp->bits)*MALLOC_BITS;
    k <<= bp->shift;

    if (malloc_junk)
	memset((char *)bp->page + k, SOME_JUNK, bp->size);

    return (u_char *)bp->page + k;
}

/*
 * Allocate a piece of memory
 */
static void *
imalloc(size)
    size_t size;
{
    void *result;

    if (!malloc_started)
	malloc_init();

    if (suicide)
	abort();

    if ((size + malloc_pagesize) < size)       /* Check for overflow */
	result = 0;
    else if (size <= malloc_maxsize)
	result =  malloc_bytes(size);
    else
	result =  malloc_pages(size);

    if (malloc_abort && !result)
	wrterror("allocation failed.\n");

    if (malloc_zero && result)
	memset(result, 0, size);

    return result;
}

/*
 * Change the size of an allocation.
 */
static void *
irealloc(ptr, size)
    void *ptr;
    size_t size;
{
    void *p;
    u_long osize, index;
    struct pginfo **mp;
    int i;

    if (suicide)
	abort();

    if (!malloc_started) {
	wrtwarning("malloc() has never been called.\n");
	return 0;
    }

    index = ptr2index(ptr);

    if (index < malloc_pageshift) {
	wrtwarning("junk pointer, too low to make sense.\n");
	return 0;
    }

    if (index > last_index) {
	wrtwarning("junk pointer, too high to make sense.\n");
	return 0;
    }

    mp = &page_dir[index];

    if (*mp == MALLOC_FIRST) {			/* Page allocation */

	/* Check the pointer */
	if ((u_long)ptr & malloc_pagemask) {
	    wrtwarning("modified (page-) pointer.\n");
	    return 0;
	}

	/* Find the size in bytes */
	for (osize = malloc_pagesize; *++mp == MALLOC_FOLLOW;)
	    osize += malloc_pagesize;

        if (!malloc_realloc && 			/* unless we have to, */
	  size <= osize && 			/* .. or are too small, */
	  size > (osize - malloc_pagesize)) {	/* .. or can free a page, */
	    return ptr;				/* don't do anything. */
	}

    } else if (*mp >= MALLOC_MAGIC) {		/* Chunk allocation */

	/* Check the pointer for sane values */
	if (((u_long)ptr & ((*mp)->size-1))) {
	    wrtwarning("modified (chunk-) pointer.\n");
	    return 0;
	}

	/* Find the chunk index in the page */
	i = ((u_long)ptr & malloc_pagemask) >> (*mp)->shift;

	/* Verify that it isn't a free chunk already */
        if ((*mp)->bits[i/MALLOC_BITS] & (1UL<<(i%MALLOC_BITS))) {
	    wrtwarning("chunk is already free.\n");
	    return 0;
	}

	osize = (*mp)->size;

	if (!malloc_realloc &&		/* Unless we have to, */
	  size < osize && 		/* ..or are too small, */
	  (size > osize/2 ||	 	/* ..or could use a smaller size, */
	  osize == malloc_minsize)) {	/* ..(if there is one) */
	    return ptr;			/* ..Don't do anything */
	}

    } else {
	wrtwarning("pointer to wrong page.\n");
	return 0;
    }

    p = imalloc(size);

    if (p) {
	/* copy the lesser of the two sizes, and free the old one */
	if (osize < size)
	    memcpy(p, ptr, osize);
	else
	    memcpy(p, ptr, size);
	ifree(ptr, 0);
    } 
    return p;
}

/*
 * Free a sequence of pages
 */

static __inline__ void
free_pages(ptr, index, info, num_pages)
    void *ptr;
    int index;
    struct pginfo *info;
    int num_pages;
{
    int i;
    struct pgfree *pf, *pt=0;
    u_long l;
    void *tail;

    if (info == MALLOC_FREE) {
	wrtwarning("page is already free.\n");
	return;
    }

    if ((info != MALLOC_FIRST && num_pages == 0) ||
	(info != MALLOC_FIRST && info != MALLOC_FOLLOW &&
	 num_pages != 0)) {
        wrtwarning("pointer to wrong page.\n");
	return;
    }

    if ((u_long)ptr & malloc_pagemask) {
	wrtwarning("modified (page-) pointer.\n");
	return;
    }

    page_dir[index] = MALLOC_FREE;
    if (num_pages <= 0) 
      /* Count how many pages and mark them free at the same time */
      for (i = 1; page_dir[index+i] == MALLOC_FOLLOW; i++)
	page_dir[index + i] = MALLOC_FREE;
    else {
      for (i = 1; page_dir[index+i] == MALLOC_FOLLOW && i < num_pages; i++)
	page_dir[index + i] = MALLOC_FREE;
      if (i == num_pages && index+i <= last_index &&
	  page_dir[index+i] == MALLOC_FOLLOW)
	page_dir[index+i] = MALLOC_FIRST;
    }

    l = i << malloc_pageshift;

    if (malloc_junk)
	memset(ptr, SOME_JUNK, l);

#ifdef __FreeBSD__
    if (malloc_hint)
	madvise(ptr, l, MADV_FREE);
#endif

    tail = (char *)ptr+l;

    /* add to free-list */
    if (!px)
	px = imalloc(sizeof *px);	/* This cannot fail... */
    px->page = ptr;
    px->end =  tail;
    px->size = l;
    if (!free_list.next) {

	/* Nothing on free list, put this at head */
	px->next = free_list.next;
	px->prev = &free_list;
	free_list.next = px;
	pf = px;
	px = 0;

    } else {

	/* Find the right spot, leave pf pointing to the modified entry. */
	tail = (char *)ptr+l;

	for(pf = free_list.next; pf->end < ptr && pf->next; pf = pf->next)
	    ; /* Race ahead here */

	if (pf->page > tail) {
	    /* Insert before entry */
	    px->next = pf;
	    px->prev = pf->prev;
	    pf->prev = px;
	    px->prev->next = px;
	    pf = px;
	    px = 0;
	} else if (pf->end == ptr ) {
	    /* Append to the previous entry */
	    pf->end = (char *)pf->end + l;
	    pf->size += l;
	    if (pf->next && pf->end == pf->next->page ) {
		/* And collapse the next too. */
		pt = pf->next;
		pf->end = pt->end;
		pf->size += pt->size;
		pf->next = pt->next;
		if (pf->next)
		    pf->next->prev = pf;
	    }
	} else if (pf->page == tail) {
	    /* Prepend to entry */
	    pf->size += l;
	    pf->page = ptr;
	} else if (!pf->next) {
	    /* Append at tail of chain */
	    px->next = 0;
	    px->prev = pf;
	    pf->next = px;
	    pf = px;
	    px = 0;
	} else {
	    wrterror("freelist is destroyed.\n");
	}
    }
    
    /* Return something to OS ? */
    if (!pf->next &&				/* If we're the last one, */
      pf->size > malloc_cache &&		/* ..and the cache is full, */
      pf->end == malloc_brk &&			/* ..and none behind us, */
      malloc_brk == __sbrk(0)) {			/* ..and it's OK to do... */

	/*
	 * Keep the cache intact.  Notice that the '>' above guarantees that
	 * the pf will always have at least one page afterwards.
	 */
	pf->end = (char *)pf->page + malloc_cache;
	pf->size = malloc_cache;

	__brk(pf->end);
	malloc_brk = pf->end;

	index = ptr2index(pf->end);
	last_index = index - 1;

	for(i=index;i <= last_index;)
	    page_dir[i++] = MALLOC_NOT_MINE;

	/* XXX: We could realloc/shrink the pagedir here I guess. */
    }
    if (pt)
	ifree(pt, 0);
}

/*
 * Free a chunk, and possibly the page it's on, if the page becomes empty.
 */

/* ARGSUSED */
static __inline__ void
free_bytes(ptr, index, info)
    void *ptr;
    int index;
    struct pginfo *info;
{
    int i;
    struct pginfo **mp;
    void *vp;

    /* Find the chunk number on the page */
    i = ((u_long)ptr & malloc_pagemask) >> info->shift;

    if (((u_long)ptr & (info->size-1))) {
	wrtwarning("modified (chunk-) pointer.\n");
	return;
    }

    if (info->bits[i/MALLOC_BITS] & (1UL<<(i%MALLOC_BITS))) {
	wrtwarning("chunk is already free.\n");
	return;
    }

    if (malloc_junk)
	memset(ptr, SOME_JUNK, info->size);

    info->bits[i/MALLOC_BITS] |= 1UL<<(i%MALLOC_BITS);
    info->free++;

    mp = page_dir + info->shift;

    if (info->free == 1) {

	/* Page became non-full */

	mp = page_dir + info->shift;
	/* Insert in address order */
	while (*mp && (*mp)->next && (*mp)->next->page < info->page)
	    mp = &(*mp)->next;
	info->next = *mp;
	*mp = info;
	return;
    }

    if (info->free != info->total)
	return;

    /* Find & remove this page in the queue */
    while (*mp != info) {
	mp = &((*mp)->next);
#ifdef MALLOC_EXTRA_SANITY
	if (!*mp)
		wrterror("(ES): Not on queue\n");
#endif /* MALLOC_EXTRA_SANITY */
    }
    *mp = info->next;

    /* Free the page & the info structure if need be */
    page_dir[ptr2index(info->page)] = MALLOC_FIRST;
    vp = info->page;		/* Order is important ! */
    if(vp != (void*)info) 
	ifree(info, 0);
    ifree(vp, 0);
}

static void
ifree(ptr, num_pages)
    void *ptr;
    int num_pages;
{
    struct pginfo *info;
    int index;

    /* This is legal */
    if (!ptr)
	return;

    if (!malloc_started) {
	wrtwarning("malloc() has never been called.\n");
	return;
    }

    /* If we're already sinking, don't make matters any worse. */
    if (suicide)
	return;

    index = ptr2index(ptr);

    if (index < malloc_pageshift) {
	wrtwarning("junk pointer, too low to make sense.\n");
	return;
    }

    if (index > last_index) {
	wrtwarning("junk pointer, too high to make sense.\n");
	return;
    }

    info = page_dir[index];

    if (info < MALLOC_MAGIC)
        free_pages(ptr, index, info, num_pages);
    else
	free_bytes(ptr, index, info);
    return;
}

/*
 * These are the public exported interface routines.
 */

static int malloc_active;

void *
__malloc(size_t size)
{
    register void *r;

    malloc_func = " in __malloc():";
    THREAD_LOCK();
    if (malloc_active++) {
	wrtwarning("recursive call.\n");
        malloc_active--;
	return (0);
    }
    r = imalloc(size);
    UTRACE(0, size, r);
    malloc_active--;
    THREAD_UNLOCK();
    if (malloc_xmalloc && !r)
	wrterror("out of memory.\n");
    return (r);
}

void
__free(void *ptr)
{
    malloc_func = " in free():";
    THREAD_LOCK();
    if (malloc_active++) {
	wrtwarning("recursive call.\n");
        malloc_active--;
	return;
    }
    ifree(ptr, 0);
    UTRACE(ptr, 0, 0);
    malloc_active--;
    THREAD_UNLOCK();
    return;
}

/* like __free, but only for pages and doesn't have to be at the
   beginning of a malloc'd region.  If size == 0, then free until
   end of malloc'd region */
void
__free2(void *ptr, size_t size)
{
    malloc_func = " in free():";
    THREAD_LOCK();
    if (malloc_active++) {
	wrtwarning("recursive call.\n");
        malloc_active--;
	return;
    }
    ifree(ptr, size ? (pageround(size) >> malloc_pageshift) : -1);
    UTRACE(ptr, 0, 0);
    malloc_active--;
    THREAD_UNLOCK();
    return;
}

void *
__realloc(void *ptr, size_t size)
{
    register void *r;

    malloc_func = " in realloc():";
    THREAD_LOCK();
    if (malloc_active++) {
	wrtwarning("recursive call.\n");
        malloc_active--;
	return (0);
    }
    if (!ptr) {
	r = imalloc(size);
    } else {
        r = irealloc(ptr, size);
    }
    UTRACE(ptr, size, r);
    malloc_active--;
    THREAD_UNLOCK();
    if (malloc_xmalloc && !r)
	wrterror("out of memory.\n");
    return (r);
}
