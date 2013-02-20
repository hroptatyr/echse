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
	return (n * mbsz);
}

ptrdiff_t
init_gq(gq_t q, size_t mbsz, size_t at_least)
{
	size_t nusz = gq_nmemb(mbsz, at_least);
	size_t olsz = gq_nmemb(mbsz, q->nitems);
	void *ol_items = q->items;
	void *nu_items;
	ptrdiff_t res = 0;

	if (olsz >= nusz) {
		return 0;
	}

#if defined MREMAP_MAYMOVE
	if (ol_items) {
		nu_items = mremap(ol_items, olsz, nusz, MREMAP_MAYMOVE);
	} else {
		nu_items = mmap(NULL, nusz, PROT_MEM, MAP_MEM, -1, 0);
	}
#else  /* !MREMAP_MAYMOVE */
	/* get some new items */
	nu_items = mmap(ol_items, nusz, PROT_MEM, MAP_MEM, -1, 0);

	if (ol_items) {
		/* aha, resize */
		memcpy(nu_items, ol_items, olsz);

		/* unmap the old guy */
		munmap(ol_items, olsz);
	}
#endif	/* MREMAP_MAYMOVE */
	/* reassign */
	q->items = nu_items;
	q->itemz = mbsz;
	q->nitems = nusz / mbsz;

	/* fixup sizes for the free list fiddling */
	olsz -= olsz % mbsz;
	nusz -= nusz % mbsz;
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
			if (sp > ep) {
				ip->prev = gq_item(q, sp - mbsz);
			}
		}
		/* attach new list to free list */
		eip->prev = q->free->ilst;
		if (q->free->ilst) {
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
	return res;
}

void
fini_gq(gq_t q)
{
	if (q->items) {
		munmap(q->items, gq_nmemb(q->itemz, q->nitems));
		q->items = NULL;
		q->nitems = 0U;
		q->itemz = 0U;
	}
	return;
}

static gq_item_t
gq_ll_next(gq_t x, gq_item_t i)
{
	struct gq_item_s *p = gq_item_ptr(x, i);
	return p->next;
}

static gq_item_t
gq_ll_prev(gq_t x, gq_item_t i)
{
	struct gq_item_s *p = gq_item_ptr(x, i);
	return p->prev;
}

static void
gq_ll_set_next(gq_t x, gq_item_t i, gq_item_t nx)
{
	struct gq_item_s *p = gq_item_ptr(x, i);
	p->next = nx;
	return;
}

static void
gq_ll_set_prev(gq_t x, gq_item_t i, gq_item_t pr)
{
	struct gq_item_s *p = gq_item_ptr(x, i);
	p->prev = pr;
	return;
}

gq_item_t
gq_pop_head(gq_t q, gq_ll_t dll)
{
	gq_item_t res;

	if ((res = dll->i1st) &&
	    (dll->i1st = gq_ll_next(q, dll->i1st)) == GQ_NULL_ITEM) {
		dll->ilst = GQ_NULL_ITEM;
	} else if (res) {
		gq_ll_set_prev(q, dll->i1st, GQ_NULL_ITEM);
	} else {
		return GQ_NULL_ITEM;
	}
	{
		struct gq_item_s *rp = gq_item_ptr(q, res);
		/* rinse */
		rp->prev = rp->next = GQ_NULL_ITEM;
	}
	return res;
}

void
gq_push_tail(gq_t q, gq_ll_t dll, gq_item_t i)
{
	struct gq_item_s *ip = gq_item_ptr(q, i);

	if (dll->ilst) {
		gq_ll_set_next(q, dll->ilst, i);
		ip->prev = dll->ilst;
		ip->next = GQ_NULL_ITEM;
		dll->ilst = i;
	} else {
		assert(dll->i1st == 0U);
		dll->i1st = dll->ilst = i;
		ip->next = GQ_NULL_ITEM;
		ip->prev = GQ_NULL_ITEM;
	}
	return;
}

void
gq_pop_item(gq_t q, gq_ll_t dll, gq_item_t i)
{
	gq_item_t nx = gq_ll_next(q, i);
	gq_item_t pr = gq_ll_prev(q, i);

	if (pr != GQ_NULL_ITEM) {
		/* i->prev->next = i->next */
		gq_ll_set_next(q, pr, nx);
	} else {
		/* must be head then */
		dll->i1st = nx;
	}
	if (nx != GQ_NULL_ITEM) {
		/* i->next->prev = i->prev */
		gq_ll_set_prev(q, nx, pr);
	} else {
		/* must be tail then */
		dll->ilst = pr;
	}
	{
		struct gq_item_s *ip = gq_item_ptr(q, i);
		ip->next = ip->prev = GQ_NULL_ITEM;
	}
	return;
}

/* gq.c ends here */
