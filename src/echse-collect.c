/*** echse-collect.c -- collecting events
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
#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "echse.h"
#include "instant.h"
#include "dt-strpf.h"
#include "module.h"
#include "gq.h"
#include <hat-trie/hat-trie.h>
#include "echs-lisp.h"

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */
#if !defined UNUSED
# define UNUSED(x)	__attribute__((unused)) x
#endif	/* UNUSED */

typedef struct collref_s *collref_t;
typedef struct ev_s *ev_t;

typedef struct gq_ll_s collref_ll_t[1];

typedef enum {
	EVMDFR_START,
	EVMDFR_END,
	EVMDFR_BANG,
} echs_evmdfr_t;

struct item_s {
	struct gq_item_s i;
	collref_ll_t collrefs;
};

struct collref_s {
	struct gq_item_s i;
	coll_t ref;
};

struct coll_s {
	struct gq_item_s i;

	struct collect_s c[1];

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

struct ev_s {
	struct gq_item_s i;

	echs_event_t e;
	coll_t c;
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

static int
materialise(echs_event_t e)
{
	static char buf[256];
	char *bp = buf;
	echs_evmdfr_t m = echs_event_modifier(e);

	/* BEST has the guy */
	bp += dt_strf(buf, sizeof(buf), e.when);
	*bp++ = '\t';
	switch (m) {
	case EVMDFR_END:
		if (*e.what != '~') {
			*bp++ = '~';
		}
		break;
	case EVMDFR_BANG:
		if (*e.what != '!') {
			*bp++ = '!';
		}
		break;
	default:
		break;
	}
	{
		size_t e_whaz = strlen(e.what);
		memcpy(bp, e.what, e_whaz);
		bp += e_whaz;
	}
	*bp++ = '\n';
	*bp = '\0';
	if (write(STDOUT_FILENO, buf, bp - buf) < 0) {
		return -1;
	}
	return 0;
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


static struct {
	struct gq_s q[1];
	struct gq_ll_s active[1];
} colls;

static struct {
	struct gq_s q[1];
	hattrie_t *ht;
} items;

static struct {
	struct gq_s q[1];
} collrefs;

static struct {
	struct gq_s pool[1];
	struct gq_ll_s q[1];
} evs;


item_t
make_item(tstrng_t nick)
{
	item_t res = make_gq_item(items.q, sizeof(*res), 64U);

	/* also bang into hat-trie */
	{
		value_t *x = hattrie_get(items.ht, nick.s, nick.z);
		*x = (value_t)(intptr_t)res;
	}
	return res;
}

void
free_item(item_t i)
{
	free_gq_item(items.q, i);
	return;
}

void
item_ll_add(item_ll_t l, item_t i)
{
	gq_push_tail(l, (gq_item_t)i);
	return;
}

item_t
item_ll_pop(item_ll_t l)
{
	item_t res = (void*)gq_pop_head(l);
	return res;
}

static void
item_add_collref(item_t i, collref_t ref)
{
	gq_push_tail(i->collrefs, (gq_item_t)ref);
	return;
}

static collref_t
item_pop_collref(item_t i)
{
	void *res = gq_pop_head(i->collrefs);
	return res;
}

static collref_t
item_collrefs(const struct item_s *i)
{
	return (void*)i->collrefs->i1st;
}


static collref_t
make_collref(coll_t c)
{
	collref_t res = make_gq_item(collrefs.q, sizeof(*res), 64U);

	res->ref = c;
	return res;
}

static void
free_collref(collref_t cr)
{
	cr->ref = NULL;
	free_gq_item(collrefs.q, cr);
	return;
}

static collref_t
next_collref(collref_t i)
{
	return (void*)i->i.next;
}


static coll_t
make_coll(struct collect_s c)
{
	coll_t res = make_gq_item(colls.q, sizeof(*res), 16U);

	*res->c = c;
	return res;
}

static void
free_coll(coll_t c)
{
	/* free all items first */
	for (item_t ip;
	     (ip = item_ll_pop(c->c->items)) != NULL; free_item(ip)) {
		/* free the collrefs too */
		for (collref_t jp;
		     (jp = item_pop_collref(ip)) != NULL; free_collref(jp));
	}
	/* free the alias */
	free(c->c->as);
	free_gq_item(colls.q, c);
	return;
}

static coll_t
active_colls(void)
{
	return (void*)colls.active->i1st;
}

static coll_t
next_coll(coll_t c)
{
	return (void*)c->i.next;
}

void
add_collect(struct collect_s c)
{
	coll_t cc = make_coll(c);

	/* add backrefs */
	for (item_t i = (void*)c.items->i1st; i != NULL; i = (void*)i->i.next) {
		item_add_collref(i, make_collref(cc));
	}
	/* activate */
	gq_push_tail(colls.active, (gq_item_t)cc);
	return;
}

static const struct item_s*
find_item(hattrie_t *ht, const char *token)
{
	size_t toklen = strlen(token);
	value_t *x;

	if ((x = hattrie_tryget(ht, token, toklen)) == NULL) {
		return NULL;
	}
	return *x;
}

static echs_event_t
pop_ev(void)
{
	ev_t ev;
	echs_event_t res = {0};

again:
	if (LIKELY((ev = (void*)gq_pop_head(evs.q)) != NULL)) {
		coll_t c;

		if ((c = ev->c) == NULL) {
			goto immed;
		}

		switch (c->flag) {
		case LATER:
			/* return a 0 event but keep this guy */
			gq_push_head(evs.q, (gq_item_t)ev);
			c->flag = IMMED;
			break;
		case CANCEL:
			/* mop the floor for the next shfit */
			c->end = (echs_instant_t){0};
			/* just flush this guy */
			free_gq_item(evs.pool, ev);
			/* and start over */
			goto again;

		case IMMED:
		default:
			/* mop the floor for the next shfit */
			c->end = (echs_instant_t){0};
		immed:
			/* just the ordinary event */
			res = ev->e;
			/* and pool him up again */
			free_gq_item(evs.pool, ev);
			break;
		}
	}
	return res;
}

static void
push_ev(echs_event_t e, coll_t c)
{
	ev_t ev = make_gq_item(evs.pool, sizeof(*ev), 64U);
	ev->e = e;
	ev->c = c;
	gq_push_tail(evs.q, (gq_item_t)ev);
	return;
}


static echs_event_t
echs_identity(echs_event_t e, void *UNUSED(clo))
{
	return e;
}

static echs_event_t
__filter(echs_event_t e, void *UNUSED(clo))
{
	const char *token = e.what;
	const struct item_s *x;
	echs_evmdfr_t st;

	/* check if we're draining */
	if (UNLIKELY(__event_0_p(e))) {
		goto out;
	}

	if ((st = echs_event_modifier(e)) != EVMDFR_START) {
		token++;
	}
	if ((x = find_item(items.ht, token)) == NULL) {
		push_ev(e, NULL);
		goto out;
	}

	/* otherwise, ... the hard work */
	for (collref_t cr = item_collrefs(x);
	     cr != NULL; cr = next_collref(cr)) {
		coll_t c = cr->ref;

		if (__inst_0_p(c->beg) && st == EVMDFR_START) {
			c->beg = e.when;
			e.what = c->c->as + 1U;
			push_ev(e, NULL);
		} else if (__inst_0_p(c->end) && st == EVMDFR_END) {
			c->end = e.when;
			e.what = c->c->as + 0U;
			/* just in case push */
			c->flag = LATER;
			push_ev(e, c);
		} else if (__inst_eq_p(e.when, c->lst)) {
			/* join! */
			c->flag = CANCEL;
		} else if (st == EVMDFR_START) {
			c->beg = e.when;
			c->flag = IMMED;
			e.what = c->c->as + 1U;
			push_ev(e, NULL);
		} else {
			/* use the already queued LATER event */
			c->end = e.when;
			c->flag = IMMED;
		}

		c->lst = e.when;
		c->lstmdfr = st;
	}
out:
	return pop_ev();
}

static void
init_collect(void)
{
	items.ht = hattrie_create();
	return;
}

static void
fini_collect(void)
{
	hattrie_free(items.ht);

	for (coll_t c; (c = (void*)gq_pop_head(colls.active)) != NULL;) {
		free_coll(c);
	}

	/* finalise the gq stuff */
	fini_gq(items.q);
	fini_gq(collrefs.q);
	fini_gq(colls.q);
	return;
}

echs_filter_t
make_echs_filter(echs_instant_t from, ...)
{
	va_list ap;
	const char *fn;

	va_start(ap, from);
	fn = va_arg(ap, const char *);
	va_end(ap);

	/* process collect file */
	init_collect();

	echs_lisp(fn);
	return (echs_filter_t){__filter, NULL};
}

void
free_echs_filter(echs_filter_t UNUSED(f))
{
	fini_collect();
	return;
}


#if defined __INTEL_COMPILER
# pragma warning (disable:593)
# pragma warning (disable:181)
#elif defined __GNUC__
# pragma GCC diagnostic ignored "-Wswitch"
# pragma GCC diagnostic ignored "-Wswitch-enum"
#endif /* __INTEL_COMPILER */
#include "echse-collect-clo.h"
#include "echse-collect-clo.c"
#if defined __INTEL_COMPILER
# pragma warning (default:593)
# pragma warning (default:181)
#elif defined __GNUC__
# pragma GCC diagnostic warning "-Wswitch"
# pragma GCC diagnostic warning "-Wswitch-enum"
#endif	/* __INTEL_COMPILER */

int
main(int argc, char *argv[])
{
	/* command line options */
	struct echs_args_info argi[1];
	echs_instant_t from;
	echs_instant_t till;
	echs_mod_t ex;
	echs_stream_t s;
	echs_filter_t this;
	int res = 0;

	if (echs_parser(argc, argv, argi)) {
		res = 1;
		goto out;
	}

	if (argi->from_given) {
		from = dt_strp(argi->from_arg);
	} else {
		from = (echs_instant_t){2000, 1, 1};
	}

	if (argi->till_given) {
		till = dt_strp(argi->till_arg);
	} else {
		till = (echs_instant_t){2037, 12, 31};
	}

	if (argi->filter_given) {
		this = make_echs_filter(from, argi->filter_arg);
	} else {
		this = (echs_filter_t){echs_identity, NULL};
	}

	{
		typedef echs_stream_t(*make_stream_f)(echs_instant_t, ...);
		echs_mod_f f;
		const char *const *in = argi->inputs;
		size_t nin = argi->inputs_num;

		if ((ex = echs_mod_open("echse")) == NULL) {
			printf("NOPE\n");
			goto out;
		} else if ((f = echs_mod_sym(ex, "make_echs_stream")) == NULL) {
			printf("no m_e_s()\n");
			goto clos_out;
		} else if ((s = ((make_stream_f)f)(from, in, nin)).f == NULL) {
			printf("no stream\n");
			goto clos_out;
		}
	}

	/* iterate! */
	for (echs_event_t e;
	     (e = echs_stream_next(s),
	      !__event_0_p(e) && __event_le_p(e, till));) {
		if (!__event_0_p(e = echs_filter_next(this, e))) {
			/* output */
			materialise(e);
		}
	}
	/* drain */
	for (echs_event_t e;
	     (e = echs_filter_drain(this),
	      !__event_0_p(e) && __event_le_p(e, till));) {
		materialise(e);
	}

	/* free filter */
	free_echs_filter(this);

	{
		typedef void(*free_stream_f)(echs_stream_t);
		echs_mod_f f;

		if ((f = echs_mod_sym(ex, "free_echs_stream")) == NULL) {
			printf("no f_e_s()\n");
			goto clos_out;
		}
		((free_stream_f)f)(s);
	}

clos_out:
	echs_mod_close(ex);

out:
	echs_parser_free(argi);
	return res;
}

/* echse-collect.c ends here */
