
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

#include <stdio.h>
#include <string.h>
#include <sys/defs.h>
#include <xuser.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/mmu.h>
#include <sys/env.h>
#include <net/ether.h>
#include <net/ip.h>

u_int __envid;                       /* Cached environment id */

void
_____symbols (void)
{
  DEF_SYM (u, UADDR);
}

void die (void) __attribute__ ((noreturn));
void
die (void)
{
  printf ("$$ I'm dying (0x%x)\n", geteid ());
  sys_env_free (0, geteid ());
  printf ("$$ I'm dead, but I don't feel dead.\n");
  for (;;)
    ;
}



static inline void
udp_recv (struct ip_pkt *ip, u_int len)
{
  if (len < (sizeof (struct ether_pkt) + sizeof (struct ip_pkt)
	     + sizeof (struct udp_pkt))) {
    printf ("Short UDP header\n");
    return;
  }
  printf ("   Udp port %d\n", ntohs (ip->ip_udp.udp_dport));
}

static inline void
ip_recv (struct ip_pkt *ip, u_int len)
{
  if (len < sizeof (struct ether_pkt) + sizeof (struct ip_pkt)) {
    printf ("Short IP header\n");
    return;
  }

  switch (ip->ip_p) {
  case IPPROTO_UDP:
    udp_recv (ip, len);
    break;
  default:
    printf ("   IP protocol %d\n", ip->ip_p);
  }
}

char pkt[8192];
struct ether_pkt * const epkt = (void *) pkt;

void
pkt_recv (void)
{
  u_int len = UAREA.u_ash_arg;
  u_int sum;

  asm volatile ("pushl %0; popl %%fs" :: "i" (GD_AW));
  sum = ip_copy_and_sum (, pkt, "%%fs:", NULL, len);

  ip_recv (ether_ip (epkt), len);

  asm volatile ("int %0" :: "i" (T_ASHRET));
}
