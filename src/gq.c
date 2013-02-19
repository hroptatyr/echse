/*** gq.c -- generic queues, or pools of data elements
 *
 * Copyright (C) 2012-2013 Sebastian Freundt
 *
 * Author:  Sebastian Freundt <freundt@ga-group.nl>
 *
 * This file is part of echse.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the author nor the names of any contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ***/
#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <unistd.h>
#include <string.h>
#include <stddef.h>
#include <sys/mman.h>
#include "gq.h"

#if defined DEBUG_FLAG
# include <assert.h>
#else  /* !DEBUG_FLAG */
# define assert(x)
#endif	/* DEBUG_FLAG */
#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */

/* just a service for mmap based allocators */
#if !defined MAP_ANON && defined MAP_ANONYMOUS
# define MAP_ANON	MAP_ANONYMOUS
#endif	/* MAP_ANON && !MAP_ANONYMOUS */
#if !defined MAP_MEM
# define MAP_MEM	(MAP_PRIVATE | MAP_ANON)
#endif	/* MAP_MEM */
#if !defined PROT_MEM
# define PROT_MEM	(PROT_READ | PROT_WRITE)
#endif	/* PROT_MEM */


static size_t __attribute__((const, pure))
gq_nmemb(size_t mbsz, size_t n)
{
	static size_t pgsz = 0;

	if (!pgsz) {
		pgsz = sysconf(_SC_PAGESIZE);
	}
	return (n * mbsz + (pgsz - 1)) & ~(pgsz - 1);
}

void
gq_rbld_ll(gq_ll_t dll, ptrdiff_t df)
{
	if (UNLIKELY(df == 0)) {
		/* thank you thank you thank you */
		return;
	}

	if (dll->i1st) {
		dll->i1st += df;
	}
	if (dll->ilst) {
		dll->ilst += df;
	}
	return;
}

static ptrdiff_t
gq_rbld(gq_t q, gq_item_t nu, size_t mbsz)
{
	ptrdiff_t df = nu - q->items;

	if (UNLIKELY(df == 0)) {
		/* thank you thank you thank you */
		return 0;
	}

	/* rebuild the free dll */
	gq_rbld_ll(q->free, df);
	/* hop along all items and up the next and prev pointers */
	for (char *sp = (void*)nu, *ep = sp + q->nitems; sp < ep; sp += mbsz) {
		gq_item_t ip = (void*)sp;

		if (ip->next) {
			ip->next += df;
		}
		if (ip->prev) {
			ip->prev += df;
		}
	}
	return df;
}

ptrdiff_t
init_gq(gq_t q, size_t mbsz, size_t at_least)
{
	size_t nusz = gq_nmemb(mbsz, at_least);
	size_t olsz = q->nitems;
	gq_item_t ol_items = q->items;
	gq_item_t nu_items;
	ptrdiff_t res = 0;

	if (q->nitems > nusz) {
		return 0;
	}
	/* get some new items */
	nu_items = mmap(ol_items, nusz, PROT_MEM, MAP_MEM, -1, 0);

	if (q->items) {
		/* aha, resize */
		memcpy(nu_items, ol_items, olsz);

		/* fix up all lists */
		res = gq_rbld(q, nu_items, mbsz);

		/* unmap the old guy */
		munmap(ol_items, olsz);
	}
	/* reassign */
	q->items = nu_items;
	q->nitems = nusz;

	/* fixup sizes for the free list fiddling */
	olsz -= olsz % mbsz;
	nusz -= nusz % mbsz;
	/* fill up the free list */
	{
		char *const ep = (char*)nu_items + olsz;
		char *const eep = ep + (nusz - olsz);
		gq_item_t eip = (void*)ep;

		for (char *sp = ep; sp < eep; sp += mbsz) {
			gq_item_t ip = (void*)sp;

			if (sp + mbsz < eep) {
				ip->next = (void*)(sp + mbsz);
			}
			if (sp > ep) {
				ip->prev = (void*)(sp - mbsz);
			}
		}
		/* attach new list to free list */
		eip->prev = q->free->ilst;
		if (q->free->ilst) {
			q->free->ilst->next = eip;
		} else {
			assert(q->free->i1st == NULL);
		}
		if (q->free->i1st == NULL) {
			q->free->i1st = eip;
			q->free->ilst = (void*)(eep - mbsz);
		}
	}
	return res;
}

void
fini_gq(gq_t q)
{
	if (q->items) {
		munmap(q->items, q->nitems);
		q->items = NULL;
		q->nitems = 0;
	}
	return;
}

gq_item_t
gq_pop_head(gq_ll_t dll)
{
	gq_item_t res;

	if ((res = dll->i1st) && (dll->i1st = dll->i1st->next) == NULL) {
		dll->ilst = NULL;
	} else if (res) {
		dll->i1st->prev = NULL;
	}
	return res;
}

void
gq_push_tail(gq_ll_t dll, gq_item_t i)
{
	if (dll->ilst) {
		dll->ilst->next = i;
		i->prev = dll->ilst;
		i->next = NULL;
		dll->ilst = i;
	} else {
		assert(dll->i1st == NULL);
		dll->i1st = dll->ilst = i;
		i->next = NULL;
		i->prev = NULL;
	}
	return;
}

void
gq_pop_item(gq_ll_t dll, gq_item_t ip)
{
	if (ip->prev) {
		ip->prev->next = ip->next;
	} else {
		/* must be head then */
		dll->i1st = ip->next;
	}
	if (ip->next) {
		ip->next->prev = ip->prev;
	} else {
		/* must be tail then */
		dll->ilst = ip->prev;
	}
	ip->next = ip->prev = NULL;
	return;
}

/* gq.c ends here */
