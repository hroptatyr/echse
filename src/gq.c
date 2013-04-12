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

typedef struct bk_item_s *bk_item_t;

struct bk_item_s {
	bk_item_t next;
	size_t z;
};


static inline __attribute__((const, pure)) size_t
gq_nmemb(size_t mbsz, size_t n)
{
	return (n * mbsz);
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
	void *ol_items = q->book->i1st;
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

	{
		/* get some new items */
		nu_items = mmap(ol_items, nusz, PROT_MEM, MAP_MEM, -1, 0);
	}

	/* reassign */
	q->nitems = nusz / mbsz;

	if (nu_items != ol_items) {
		/* steal one item again for the book keeper */
		bk_item_t bk = nu_items;

		bk->z = nusz;
		gq_push_head(q->book, (gq_item_t)bk);

		/* reset nu_items and size */
		nu_items = (char*)nu_items + mbsz;
		nusz -= mbsz;
	} else {
		/* update the book keeper */
		bk_item_t bk = nu_items;

		bk->z = nusz;

		/* reset nu_items pointer */
		nu_items = (char*)nu_items + olsz;
		nusz -= olsz;
	}

	/* fill up the free list
	 * the first member of the list will not be pointed to by i1st
	 * it's a bookkeeper */
	for (char *sp = nu_items, *const ep = sp + nusz;
	     sp < ep; sp += mbsz) {
		gq_push_tail(q->free, (gq_item_t)sp);
	}
	return;
}

void
fini_gq(gq_t q)
{
	/* use the bookkeeper elements to determine size and addresses */
	for (bk_item_t bk; (bk = (void*)gq_pop_head(q->book)) != NULL;) {
		munmap(bk, bk->z);
	}
	q->nitems = 0U;
	q->itemz = 0U;
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

void
gq_push_head(gq_ll_t dll, gq_item_t i)
{
	gq_rinse(i);
	if (dll->i1st != GQ_NULL_ITEM) {
		i->next = dll->i1st;
		dll->i1st = i;
	} else {
		assert(dll->ilst == GQ_NULL_ITEM);
		dll->i1st = dll->ilst = i;
	}
	return;
}

/* gq.c ends here */
