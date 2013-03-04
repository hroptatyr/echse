/*** collect.c -- collection filter
 *
 * Copyright (C) 2013 Sebastian Freundt
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
#include <string.h>
#include <hat-trie/hat-trie.h>
#include "echse.h"
#include "instant.h"
#include "gq.h"

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */
#if !defined UNUSED
# define UNUSED(x)		__attribute__((unused)) x
#endif	/* UNUSED */

typedef struct ev_s *ev_t;
typedef struct ev_anno_s *ev_anno_t;

typedef enum {
	EVMDFR_START,
	EVMDFR_END,
	EVMDFR_BANG,
} echs_evmdfr_t;

struct evq_s {
	struct gq_s pool[1];
	struct gq_ll_s q[1];
};

struct ev_anno_s {
	/* variable bits */
	echs_instant_t beg;
	echs_instant_t end;
	echs_instant_t lst;
	echs_evmdfr_t lstmdfr;

	enum {
		IMMED,
		CANCEL,
		LATER,
	} flag;
};

struct clo_s {
	hattrie_t *ht;

	/* them alias */
	const char *as;

	size_t nannos;
	struct ev_anno_s *annos;

	struct evq_s evq[1];
};

struct ev_s {
	struct gq_item_s i;

	echs_event_t e;
	ev_anno_t a;
};


static echs_evmdfr_t
echs_event_modifier(echs_event_t e)
{
	switch (*e.what) {
	case '~':
		return EVMDFR_END;
	case '!':
		return EVMDFR_BANG;
	default:
		return EVMDFR_START;
	}
}

static ev_anno_t
find_item(hattrie_t *ht, const char *token)
{
	size_t toklen = strlen(token);
	value_t *x;

	if ((x = hattrie_tryget(ht, token, toklen)) == NULL) {
		return NULL;
	}
	return *x;
}

static void*
make_gq_item(gq_t x, size_t membz, size_t nbatch)
{
	if (x->free->i1st == GQ_NULL_ITEM) {
		/* resize */
		init_gq(x, nbatch, membz);
	}
	/* get us a new object and populate */
	return gq_pop_head(x->free);
}

static void
free_gq_item(gq_t x, void *i)
{
	/* back on our free list */
	gq_push_tail(x->free, i);
	return;
}

static echs_event_t
pop_ev(struct evq_s *evq)
{
	ev_t ev;
	echs_event_t res = {0};

again:
	if (LIKELY((ev = (void*)gq_pop_head(evq->q)) != NULL)) {
		ev_anno_t a;

		if ((a = ev->a) == NULL) {
			goto immed;
		}

		switch (a->flag) {
		case LATER:
			/* return a 0 event but keep this guy */
			gq_push_head(evq->q, (gq_item_t)ev);
			a->flag = IMMED;
			break;
		case CANCEL:
			/* mop the floor for the next shfit */
			a->end = (echs_instant_t){0};
			/* just flush this guy */
			free_gq_item(evq->pool, ev);
			/* and start over */
			goto again;

		case IMMED:
		default:
			/* mop the floor for the next shfit */
			a->end = (echs_instant_t){0};
		immed:
			/* just the ordinary event */
			res = ev->e;
			/* and pool him up again */
			free_gq_item(evq->pool, ev);
			break;
		}
	}
	return res;
}

static void
push_ev(struct evq_s *evq, echs_event_t e, ev_anno_t a)
{
	ev_t ev = make_gq_item(evq->pool, sizeof(*ev), 64U);
	ev->e = e;
	ev->a = a;
	gq_push_tail(evq->q, (gq_item_t)ev);
	return;
}


static echs_event_t
__collect(echs_event_t e, struct clo_s *clo)
{
	const char *token = e.what;
	ev_anno_t c;
	echs_evmdfr_t st;

	/* check if we're draining */
	if (UNLIKELY(__event_0_p(e))) {
		goto out;
	}

	if ((st = echs_event_modifier(e)) != EVMDFR_START) {
		token++;
	}
	if ((c = find_item(clo->ht, token)) == NULL) {
		push_ev(clo->evq, e, NULL);
		goto out;
	}

	/* otherwise, ... the hard work */
	{
		if (__inst_0_p(c->beg) && st == EVMDFR_START) {
			c->beg = e.when;
			e.what = clo->as + 1U;
			push_ev(clo->evq, e, NULL);
		} else if (__inst_0_p(c->end) && st == EVMDFR_END) {
			c->end = e.when;
			e.what = clo->as + 0U;
			/* just in case push */
			c->flag = LATER;
			push_ev(clo->evq, e, c);
		} else if (__inst_eq_p(e.when, c->lst)) {
			/* join! */
			c->flag = CANCEL;
		} else if (st == EVMDFR_START) {
			c->beg = e.when;
			c->flag = IMMED;
			e.what = clo->as + 1U;
			push_ev(clo->evq, e, NULL);
		} else {
			/* use the already queued LATER event */
			c->end = e.when;
			c->flag = IMMED;
		}

		c->lst = e.when;
		c->lstmdfr = st;
	}
out:
	return pop_ev(clo->evq);
}

echs_filter_t
make_echs_filter(echs_instant_t UNUSED(i), ...)
{
	struct clo_s *clo = calloc(1, sizeof(*clo));

	clo->ht = hattrie_create();
	clo->as = "~xmas";

	clo->annos = calloc(clo->nannos = 2U, sizeof(*clo->annos));

	{
		value_t *x = hattrie_get(clo->ht, "XMAS", 4);
		*x = clo->annos + 0U;
	}
	{
		value_t *x = hattrie_get(clo->ht, "BOXD", 4);
		*x = clo->annos + 1U;
	}
	return (echs_filter_t){(echs_event_t(*)())__collect, clo};
}

void
free_echs_filter(echs_filter_t f)
{
	struct clo_s *clo = f.clo;

	if (UNLIKELY(clo == NULL)) {
		/* impossible to be here */
		return;
	}
	if (LIKELY(clo->ht != NULL)) {
		hattrie_free(clo->ht);
		clo->ht = NULL;
	}
	if (LIKELY(clo->annos != NULL)) {
		free(clo->annos);
		clo->annos = NULL;
		clo->nannos = 0U;
	}
	fini_gq(clo->evq->pool);
	free(clo);
	return;
}

/* collect.c ends here */
