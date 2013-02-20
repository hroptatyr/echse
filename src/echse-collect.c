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

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */
#if !defined UNUSED
# define UNUSED(x)	__attribute__((unused)) x
#endif	/* UNUSED */

typedef gq_item_t item_t;
typedef gq_item_t collref_t;
typedef unsigned int coll_t;

typedef struct gq_ll_s collref_ll_t[1];
typedef struct gq_ll_s item_ll_t[1];

typedef enum {
	EVMDFR_START,
	EVMDFR_END,
	EVMDFR_BANG,
} echs_evmdfr_t;

struct item_s {
	struct gq_item_s i;
	const char *nick;
	collref_ll_t collrefs;
};

struct collref_s {
	struct gq_item_s i;
	coll_t ref;
};

struct coll_s {
	/* :as */
	const char *nick;
	item_ll_t items;
	enum {
		COLL_OVERLAP_UNK,
		COLL_OVERLAP_EXTEND,
		COLL_OVERLAP_SHRINK,
	} overlap;

	/* variable bits */
	echs_instant_t beg;
	echs_instant_t end;
	echs_instant_t lst;
	echs_evmdfr_t lstmdfr;
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


static inline __attribute__((pure)) void*
unconst(const void *x)
{
	union {
		const void *c;
		void *p;
	} y = {x};
	return y.p;
}

static gq_item_t
make_gq_item(gq_t x, size_t membz, size_t nbatch)
{
	if (x->free->i1st == GQ_NULL_ITEM) {
		/* resize */
		init_gq(x, membz, x->nitems + nbatch);
	}
	/* get us a new object and populate */
	return gq_pop_head(x, x->free);
}

static void
free_gq_item(gq_t x, gq_item_t i)
{
	/* back on our free list */
	gq_push_tail(x, x->free, i);
	return;
}


static struct {
	size_t z;
	struct coll_s *q;
} colls;

static struct {
	struct gq_s q[1];
} items;

static struct {
	struct gq_s q[1];
} collrefs;

static inline struct item_s*
item_ptr(item_t i)
{
	return gq_item_ptr(items.q, i);
}

static item_t
make_item(const char *nick)
{
	struct item_s *ip;
	gq_item_t res = make_gq_item(items.q, sizeof(*ip), 64U);

	ip = item_ptr(res);
	ip->nick = strdup(nick);
	return res;
}

static void
free_item(item_t i)
{
	/* nick has been strdup()'d */
	free(unconst(item_ptr(i)->nick));
	free_gq_item(items.q, i);
	return;
}

static void
item_add_collref(item_t i, collref_t ref)
{
	struct item_s *ip = item_ptr(i);

	gq_push_tail(collrefs.q, ip->collrefs, ref);
	return;
}

static collref_t
item_pop_collref(item_t i)
{
	struct item_s *ip = item_ptr(i);
	return gq_pop_head(collrefs.q, ip->collrefs);
}


static inline struct collref_s*
collref_ptr(collref_t cref)
{
	return gq_item_ptr(collrefs.q, cref);
}

static collref_t
make_collref(coll_t c)
{
	struct collref_s *ref;
	collref_t res = make_gq_item(collrefs.q, sizeof(*ref), 64U);

	ref = collref_ptr(res);
	ref->ref = c;
	return res;
}

static void
free_collref(collref_t i)
{
	struct collref_s *ref = collref_ptr(i);

	ref->ref = GQ_NULL_ITEM;
	free_gq_item(collrefs.q, i);
	return;
}


static inline struct coll_s*
coll_ptr(coll_t c)
{
	return c ? colls.q + (c - 1U) : NULL;
}

static coll_t
make_coll(const char *nick)
{
	coll_t res;

	if ((colls.z % 16U) == 0U) {
		colls.q = realloc(colls.q, (colls.z + 16U) * sizeof(*colls.q));
	}
	res = (coll_t)colls.z++;

	colls.q[res].nick = strdup(nick);
	return ++res;
}

static void
free_coll(coll_t c)
{
	static item_t coll_pop_item(coll_t);

	/* free all items first */
	for (item_t ip;
	     (ip = coll_pop_item(c)) != GQ_NULL_ITEM; free_item(ip)) {
		/* free the collrefs too */
		for (collref_t jp;
		     (jp = item_pop_collref(ip)) != GQ_NULL_ITEM;
		     free_collref(jp));
	}
	return;
}

static void
coll_add_item(coll_t c, item_t i)
{
	struct coll_s *cp = coll_ptr(c);

	gq_push_tail(items.q, cp->items, i);
	/* and push the collection on the ref list of i */
	item_add_collref(i, make_collref(c));
	return;
}

static item_t
coll_pop_item(coll_t c)
{
	struct coll_s *cp = coll_ptr(c);
	return gq_pop_head(items.q, cp->items);
}


static const struct item_s*
find_item(hattrie_t *ht, const char *token)
{
	size_t toklen = strlen(token);
	value_t *x;

	if ((x = hattrie_tryget(ht, token, toklen)) == NULL) {
		return NULL;
	}
	return item_ptr((intptr_t)*x);
}

static void
test(echs_stream_t s, echs_instant_t till)
{
	static int materialise(echs_event_t, echs_evmdfr_t);
	item_t xmas = make_item("XMAS");
	item_t boxd = make_item("BOXD");
	coll_t xmasc = make_coll("xmas");
	/* bla */
	hattrie_t *ht = hattrie_create();

	{
		value_t *x;

		x = hattrie_get(ht, "XMAS", 4);
		*x = (value_t)(intptr_t)xmas;
		x = hattrie_get(ht, "BOXD", 4);
		*x = (value_t)(intptr_t)boxd;
	}

	coll_add_item(xmasc, xmas);
	coll_add_item(xmasc, boxd);

	for (echs_event_t e;
	     (e = echs_stream_next(s),
	      !__event_0_p(e) && __event_le_p(e, till));) {
		const char *token = e.what;
		const struct item_s *x;
		echs_evmdfr_t st;

		if ((st = echs_event_modifier(e)) != EVMDFR_START) {
			token++;
		}

		if ((x = find_item(ht, token)) == NULL) {
			continue;
		}
		for (struct collref_s *crp = collref_ptr(x->collrefs->i1st);
		     crp; crp = collref_ptr(crp->i.next)) {
			struct coll_s *c = coll_ptr(crp->ref);

			/* print last value */
			if (!__inst_0_p(c->end) &&
			    !__inst_eq_p(e.when, c->lst) && c->lstmdfr) {
				echs_event_t prev = {c->end, c->nick};

				/* quick output lest we forget */
				materialise(prev, c->lstmdfr);
				/* mop the floor for the next shfit */
				c->beg = (echs_instant_t){0};
				c->end = (echs_instant_t){0};
				c->lst = (echs_instant_t){0};
			}
			if (__inst_0_p(c->beg) && st == EVMDFR_START) {
				c->beg = e.when;
				e.what = c->nick;
				materialise(e, EVMDFR_START);
			} else if (st != c->lstmdfr && __inst_eq_p(e.when, c->lst)) {
				/* wipe */
				c->end = (echs_instant_t){0};
			} else if (__inst_0_p(c->end) && st == EVMDFR_END) {
				c->end = e.when;
			}

			c->lst = e.when;
			c->lstmdfr = st;
		}
	}
	/* materialise all pending colls */
	for (size_t i = 0; i < colls.z; i++) {
		struct coll_s *c = colls.q + i;

		if (!__inst_0_p(c->end) && c->lstmdfr) {
			echs_event_t prev = {c->end, c->nick};
			materialise(prev, c->lstmdfr);
		}
	}

	hattrie_free(ht);
	free_coll(xmasc);
	return;
}

static int
materialise(echs_event_t e, echs_evmdfr_t m)
{
	static char buf[256];
	char *bp = buf;

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

	/* the iterator */
	test(s, till);

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
