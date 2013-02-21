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
#if !defined UNUSED
# define UNUSED(x)	__attribute__((unused)) x
#endif	/* UNUSED */

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


static inline __attribute__((const, pure)) size_t
gq_nmemb(size_t mbsz, size_t n)
{
	return (n * mbsz);
}

static inline __attribute__((pure)) gq_item_t
gq_item(gq_t UNUSED(q), void *x)
{
	return x;
}
static inline __attribute__((pure)) struct gq_item_s*
gq_item_ptr(gq_t UNUSED(q), gq_item_t x)
{
	return x;
}

static void
gq_rinse(gq_item_t i)
{
	i->next = GQ_NULL_ITEM;
	return;
}

void
init_gq(gq_t q, size_t nnew_members, size_t mbsz)
{
	void *ol_items = q->items;
	void *nu_items;
	size_t olsz;
	size_t nusz;

	if (q->itemz == 0UL) {
		/* itemz singleton */
		if (UNLIKELY((q->itemz = mbsz) == 0UL)) {
			return;
		}
	}

	/* old size of the active map */
	mbsz = q->itemz;
	olsz = gq_nmemb(mbsz, q->nitems);
	nusz = gq_nmemb(mbsz, q->nitems + nnew_members);

#if defined MREMAP_MAYMOVE
	if (ol_items) {
		nu_items = mremap(ol_items, olsz, nusz, 0);
	} else
#endif	/* MREMAP_MAYMOVE */
	{
		/* get some new items */
		nu_items = mmap(ol_items, nusz, PROT_MEM, MAP_MEM, -1, 0);
	}

	/* reassign */
	q->items = nu_items;
	q->nitems = nusz / mbsz;

	if (nu_items != ol_items) {
		q->ntot += q->nitems;
		/* reset old size slot so the free list adder can work */
		olsz = 0UL;
	}

	/* fill up the free list */
	{
		char *const ep = (char*)nu_items + olsz;
		char *const eep = ep + (nusz - olsz);
		struct gq_item_s *eip = (void*)ep;

		for (char *sp = ep; sp < eep; sp += mbsz) {
			struct gq_item_s *ip = (void*)sp;

			if (sp + mbsz < eep) {
				ip->next = gq_item(q, sp + mbsz);
			}
		}
		/* attach new list to free list */
		if (q->free->ilst != GQ_NULL_ITEM) {
			struct gq_item_s *ilst = gq_item_ptr(q, q->free->ilst);
			ilst->next = gq_item(q, eip);
		} else {
			assert(q->free->i1st == GQ_NULL_ITEM);
		}
		if (q->free->i1st == GQ_NULL_ITEM) {
			q->free->i1st = gq_item(q, eip);
			q->free->ilst = gq_item(q, (eep - mbsz));
		}
	}
	return;
}

void
fini_gq(gq_t q)
{
/* redo me */
	if (q->items) {
		munmap(q->items, gq_nmemb(q->itemz, q->nitems));
		q->items = NULL;
		q->nitems = 0U;
		q->itemz = 0U;
	}
	return;
}

gq_item_t
gq_pop_head(gq_ll_t dll)
{
	gq_item_t res;

	if ((res = dll->i1st) == GQ_NULL_ITEM) {
		return GQ_NULL_ITEM;
	} else if ((dll->i1st = dll->i1st->next) == GQ_NULL_ITEM) {
		dll->ilst = GQ_NULL_ITEM;
	}
	gq_rinse(res);
	return res;
}

void
gq_push_tail(gq_ll_t dll, gq_item_t i)
{
	gq_rinse(i);
	if (dll->ilst != GQ_NULL_ITEM) {
		dll->ilst->next = i;
		dll->ilst = i;
	} else {
		assert(dll->i1st == GQ_NULL_ITEM);
		dll->i1st = dll->ilst = i;
	}
	return;
}

/* gq.c ends here */
