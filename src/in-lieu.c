/*** in-lieu.c -- in-lieu filter
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

typedef struct echs_instdiff_s echs_instdiff_t;

typedef struct ev_s *ev_t;
typedef struct ev_anno_s *ev_anno_t;

typedef enum {
	EVMDFR_START,
	EVMDFR_END,
	EVMDFR_BANG,
} echs_evmdfr_t;

struct echs_instdiff_s {
	/* difference in days */
	int32_t d;
	/* difference in msecs */
	int32_t t;
};

struct evq_s {
	struct gq_s pool[1];
	struct gq_ll_s lieu[1];
	struct gq_ll_s q[1];
};

struct ev_anno_s {
	unsigned int movable:1;
	unsigned int immovable:1;

	/* variable bits */
	echs_event_t beg;
	echs_event_t end;
};

struct clo_s {
	hattrie_t *ht;

	size_t nannos;
	struct ev_anno_s *annos;

	/* stuff to keep track */
	struct evq_s evq[1];
	echs_instant_t last;
};

struct ev_s {
	struct gq_item_s i;

	echs_event_t e;
	ev_anno_t a;
};


static unsigned int
__get_yday(unsigned int y, unsigned int m)
{
	static const uint16_t mlen[] = {
		0U, 31U, 59U, 90U, 120U, 151U, 181U,
		212U, 243U, 273U, 304U, 334U, 365U,
	};

	unsigned int res = mlen[m - 1];

	if (UNLIKELY((y % 4U) == 0U && m > 2U)) {
		res++;
	}
	return res;
}

static unsigned int
__leap_years_between(unsigned int y1, unsigned int y2)
{
/* Return how many leap years are in [y1, y2) */
	unsigned int est = (y2 - y1) / 4U;

	if (LIKELY(y2 > y1) && (UNLIKELY(y1 % 4U) > ((y2 - 1U) % 4U))) {
		est++;
	}
	return est;
}

static echs_instdiff_t
__inst_diff(echs_instant_t i1, echs_instant_t i2)
{
/* compute i2 - i1 in days and msecs,
 * this is practically __ymd_diff() off dateutils */
	echs_instdiff_t diff;
	uint32_t i1u32;
	uint32_t i2u32;

	/* straight forward is the time portion */
	i1u32 = ((i1.H * 60U + i1.M) * 60U + i1.S) * 1000U + i1.ms;
	i2u32 = ((i2.H * 60U + i2.M) * 60U + i2.S) * 1000U + i2.ms;
	diff.t = i2u32 - i1u32;

	/* date portion */
	/* first compute the month difference */
	{
		unsigned int yd1 = __get_yday(i1.y, i1.m);
		unsigned int yd2 = __get_yday(i2.y, i2.m);

		if ((diff.d = (yd2 + i2.d) - (yd1 + i1.d)) < 0) {
			diff.d += 365U;
			if ((i1.y++ % 4U) == 0U) {
				diff.d++;
			}
		}
		if (i2.y > i1.y) {
			diff.d += 365U * (i2.y - i1.y);
			diff.d += __leap_years_between(i1.y, i2.y);
		}
	}

	/* split into date and time part */
	return diff;
}

static echs_instant_t
__inst_add(echs_instant_t i, echs_instdiff_t diff)
{
	i.ms += diff.t % 1000U;
	diff.t /= 1000U;
	i.S += diff.t % 60U;
	diff.t /= 60U;
	i.M += diff.t % 60U;
	diff.t /= 60U;
	i.H += diff.t % 24U;

	i.d += diff.d;
	return echs_instant_fixup(i);
}


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

	if (LIKELY((ev = (void*)gq_pop_head(evq->q)) != NULL)) {
		res = ev->e;

		/* and free him */
		free_gq_item(evq->pool, ev);
	}
	return res;
}

static void
push_ev(struct evq_s *evq, echs_event_t e)
{
	ev_t ev = make_gq_item(evq->pool, sizeof(*ev), 64U);

	ev->e = e;
	gq_push_tail(evq->q, (gq_item_t)ev);
	return;
}

static ev_anno_t
pop_lieu(struct evq_s *evq)
{
	ev_t ev;
	ev_anno_t res = NULL;

	if (LIKELY((ev = (void*)gq_pop_head(evq->lieu)) != NULL)) {
		res = ev->a;

		/* and free him */
		free_gq_item(evq->pool, ev);
	}
	return res;
}

static ev_anno_t
pop_lieu_after(struct evq_s *evq, ev_t after)
{
	ev_t ev;
	ev_anno_t res = NULL;

	if (UNLIKELY(after == NULL)) {
		return pop_lieu(evq);
	} else if (LIKELY((ev = (void*)after->i.next) != NULL)) {
		res = ev->a;

		after->i.next = (void*)ev->i.next;
		free_gq_item(evq->pool, ev);
	}
	return res;
}

static void
push_lieu(struct evq_s *evq, echs_event_t e, ev_anno_t a)
{
	ev_t ev = make_gq_item(evq->pool, sizeof(*ev), 64U);

	if (echs_event_modifier(e) == EVMDFR_START) {
		a->beg = e;
		ev->a = a;
		gq_push_tail(evq->lieu, (gq_item_t)ev);
	} else {
		a->end = e;
	}
	return;
}

static ev_anno_t
fixup_lieu(ev_anno_t a, echs_instant_t inst)
{
	/* compute eva.a->end - eva.e */
	echs_instdiff_t diff = __inst_diff(a->beg.when, a->end.when);

	a->beg.when = inst;
	a->end.when = echs_instant_fixup(__inst_add(inst, diff));
	return a;
}

static void
emit_lieu(struct evq_s *evq, ev_anno_t a)
{
	push_ev(evq, a->beg);
	push_ev(evq, a->end);
	return;
}

static void
emit_lieu_maybe(struct evq_s *evq, echs_instant_t cutoff, echs_instant_t last)
{
	/* go through the in-lieu queue */

	for (ev_t ev = (void*)evq->lieu->i1st, prev = NULL, next;
	     ev != NULL && (next = (void*)ev->i.next, 1);
	     ev = next) {
		ev_anno_t a;

		if (UNLIKELY((a = ev->a) == NULL)) {
			goto next;
		} else if (__event_0_p(a->beg)) {
			goto next;
		} else if (__event_0_p(a->end)) {
			goto next;
		} else if (!__event_le_p(a->end, cutoff)) {
			goto next;
		}
		/* yay, pop the lieu event and emit */
		a = pop_lieu_after(evq, prev);
		emit_lieu(evq, fixup_lieu(a, last));
		last = a->end.when;
		continue;
	next:
		prev = ev;
	}
	return;
}

static inline __attribute__((pure)) bool
movablep(const struct ev_anno_s *e)
{
	return e->movable == 1U;
}

static inline __attribute__((pure)) bool
immovablep(const struct ev_anno_s *e)
{
	return e->immovable == 1U;
}


static echs_event_t
__in_lieu(echs_event_t e, void *_clo)
{
	struct clo_s *clo = _clo;
	const char *token = e.what;
	ev_anno_t c;
	echs_evmdfr_t st;

	/* check if we're draining */
	if (UNLIKELY(__event_0_p(e))) {
		goto drain;
	}

	if ((st = echs_event_modifier(e)) != EVMDFR_START) {
		token++;
	}
	if ((c = find_item(clo->ht, token)) == NULL) {
		/* neither movable nor immovable */
		push_ev(clo->evq, e);
		goto out;
	}

	/* otherwise */
	if (immovablep(c) && st == EVMDFR_START) {
		/* check if there's finished in-lieu events before this one */
		emit_lieu_maybe(clo->evq, e.when, clo->last);
		/* oh don't forget to push the current guy */
		push_ev(clo->evq, e);

	} else if (immovablep(c)) {
		/* yay, we can insert in-lieus now */
		push_ev(clo->evq, e);

	} else if (movablep(c)) {
		/* ah movable, push to in-lieu list right away */
		push_lieu(clo->evq, e, c);

	} else {
		/* pass through */
		push_ev(clo->evq, e);
	}
out:
	if (!__event_0_p(e = pop_ev(clo->evq)) &&
	    echs_event_modifier(e) != EVMDFR_START &&
	    !__event_le_p(e, clo->last)) {
		clo->last = e.when;
	}
	return e;

drain:
	if (__event_0_p(e = pop_ev(clo->evq))) {
		/* get them off the lieu queue if any */
		ev_anno_t l;

		if ((l = pop_lieu(clo->evq)) != NULL) {
			emit_lieu(clo->evq, fixup_lieu(l, clo->last));
		}
		e = pop_ev(clo->evq);
		clo->last = e.when;
	}
	return e;
}

echs_filter_t
make_echs_filter(echs_instant_t UNUSED(i), ...)
{
	struct clo_s *clo = calloc(1, sizeof(*clo));

	clo->ht = hattrie_create();

	clo->annos = calloc(clo->nannos = 4U, sizeof(*clo->annos));
	{
		value_t *x = hattrie_get(clo->ht, "SAT", 3);
		*x = clo->annos + 0U;
		clo->annos[0U].immovable = 1;
	}
	{
		value_t *x = hattrie_get(clo->ht, "SUN", 3);
		*x = clo->annos + 1U;
		clo->annos[1U].immovable = 1;
	}
	{
		value_t *x = hattrie_get(clo->ht, "XMAS", 4);
		*x = clo->annos + 2U;
		clo->annos[2U].movable = 1;
	}
	{
		value_t *x = hattrie_get(clo->ht, "BOXD", 4);
		*x = clo->annos + 3U;
		clo->annos[3U].movable = 1;
	}

	return (echs_filter_t){__in_lieu, clo};
}

void
free_echs_filter(echs_filter_t UNUSED(f))
{
	struct clo_s *clo = f.clo;

	if (UNLIKELY(clo == NULL)) {
		/* we should never end up here */
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

/* in-lieu.c ends here */
