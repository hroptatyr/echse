/*** builders.c -- stream building blocks
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
#include <string.h>
#include "echse.h"
#include "instant.h"
#include "builders.h"
#include "strctl.h"

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */
#if !defined UNUSED
# define UNUSED(x)	__attribute__((unused)) x
#endif	/* UNUSED */

static const unsigned int mdays[] = {
	0U, 31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
};

static echs_wday_t
__get_wday(echs_instant_t i)
{
/* sakamoto method, stolen from dateutils */
	static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
	unsigned int year = i.y;
	unsigned int res;

	year -= i.m < 3;
	res = year + year / 4U - year / 100U + year / 400U;
	res += t[i.m - 1U] + i.d;
	return (echs_wday_t)(((unsigned int)res % 7U) ?: SUN);
}

static char*
strdup_state(const char *st)
{
	size_t sz = strlen(st);
	char *res = malloc(1U + sz + 1U/*for \nul*/);

	res[0] = '~';
	memcpy(res + 1, st, sz + 1U);
	return res;
}


struct wday_clo_s {
	char *state;
	echs_wday_t wd;
	int in_lieu;
	echs_stream_t s;
};

static echs_event_t
__wday_after(void *clo)
{
	struct wday_clo_s *wdclo = clo;
	echs_event_t e = echs_stream_next(wdclo->s);
	echs_wday_t ref_wd = wdclo->wd;
	echs_wday_t when_wd = __get_wday(e.when);
	int add;

	/* now the magic bit, we want the number of days to add so that
	 * wd(e.when + X) == WD above, this is a simple modulo subtraction */
	add = ((unsigned int)ref_wd + 7U - (unsigned int)when_wd) % 7;
	/* however if the difference is naught add 7 days so we truly get
	 * the same weekday next week (after as in strictly-after) */
	e.when.d += add ?: wdclo->in_lieu;
	e.when = echs_instant_fixup(e.when);
	if (wdclo->state != NULL) {
		e.what = wdclo->state;
	}
	return e;
}

DEFUN echs_stream_t
echs_wday_after(echs_stream_t s, echs_wday_t wd)
{
	struct wday_clo_s *clo = calloc(1, sizeof(*clo));

	clo->wd = wd;
	clo->in_lieu = 7;
	clo->s = s;
	return (echs_stream_t){__wday_after, clo};
}

DEFUN echs_stream_t
echs_wday_after_or_on(echs_stream_t s, echs_wday_t wd)
{
	struct wday_clo_s *clo = calloc(1, sizeof(*clo));

	clo->wd = wd;
	clo->s = s;
	return (echs_stream_t){__wday_after, clo};
}

static echs_event_t
__wday_before(void *clo)
{
	struct wday_clo_s *wdclo = clo;
	echs_event_t e = echs_stream_next(wdclo->s);
	echs_wday_t ref_wd = wdclo->wd;
	echs_wday_t when_wd = __get_wday(e.when);
	int add;
	int tgtd;

	/* now the magic bit, we want the number of days to add so that
	 * wd(e.when + X) == WD above, this is a simple modulo subtraction */
	add = ((int)when_wd + 7 - (int)ref_wd) % 7;
	/* however if the difference is naught add 7 days so we truly get
	 * the same weekday next week (after as in strictly-after) */
	/* also, because echs_instant_t operates on unsigneds we have to
	 * do a mini fix-up here */
	if ((tgtd = e.when.d - (add ?: wdclo->in_lieu)) < 0) {
		tgtd += mdays[--e.when.m];
	}
	e.when.d = tgtd;
	if (wdclo->state != NULL) {
		e.what = wdclo->state;
	}
	return e;
}

DEFUN echs_stream_t
echs_wday_before(echs_stream_t s, echs_wday_t wd)
{
	struct wday_clo_s *clo = calloc(1, sizeof(*clo));

	clo->wd = wd;
	clo->in_lieu = 7;
	clo->s = s;
	return (echs_stream_t){__wday_before, clo};
}

DEFUN echs_stream_t
echs_wday_before_or_on(echs_stream_t s, echs_wday_t wd)
{
	struct wday_clo_s *clo = calloc(1, sizeof(*clo));

	clo->wd = wd;
	clo->s = s;
	return (echs_stream_t){__wday_before, clo};
}

DEFUN echs_stream_t
echs_free_wday(echs_stream_t s)
{
	struct wday_clo_s *clo = s.clo;
	echs_stream_t res = clo->s;

	if (clo->state != NULL) {
		/* prob strdup'd */
		free(clo->state);
		clo->state = NULL;
	}
	free(clo);
	return res;
}


struct every_clo_s {
	char *state;
	echs_instant_t next;
};

static echs_event_t
__every_year(void *clo)
{
	struct every_clo_s *eclo = clo;
	echs_instant_t next = eclo->next;

	eclo->next.y++;
	return (echs_event_t){echs_instant_fixup(next), eclo->state};
}

static echs_stream_t
_every_year(echs_instant_t i, echs_mon_t mon, unsigned int dom)
{
	struct every_clo_s *clo = calloc(1, sizeof(*clo));

	/* check the mode we're in */
	if (i.m < mon || i.m == mon && i.d <= dom) {
		i = (echs_instant_t){i.y, mon, dom, ECHS_ALL_DAY};
	} else {
		i = (echs_instant_t){i.y + 1, mon, dom, ECHS_ALL_DAY};
	}
	clo->next = i;
	return (echs_stream_t){__every_year, clo};
}

/* our cw muxer (using the .d slot) */
#define YMCW_MUX(c, w)	(((c) << 4U) | ((w) & 0x0fU))

static echs_instant_t
__ymcw_to_inst(echs_instant_t y)
{
/* convert instant with CW coded D slot in Y to Gregorian insants. */
	unsigned int c = y.d >> 4U;
	unsigned int w = y.d & 0x07U;
	echs_instant_t res = {y.y, y.m, 1U, y.H, y.M, y.S, y.ms};
	/* get month's first's weekday */
	echs_wday_t wd1 = __get_wday(res);
	unsigned int add;
	unsigned int tgtd;

	/* now just like in the __wday_after() code, we want the number of days
	 * to add so that wd(res + X) == W above,
	 * this is a simple modulo subtraction */
	add = ((unsigned int)w + 7U - (unsigned int)wd1) % 7;
	if ((tgtd = 1U + add + (c - 1) * 7U) > mdays[res.m]) {
		if (UNLIKELY(tgtd == 29U && !(res.y % 4U))) {
			/* ah, leap year innit
			 * no need to check for Feb because months are
			 * usually longer than 29 days and we wouldn't be
			 * here if it was a different month */
			;
		} else {
			tgtd -= 7U;
		}
	}
	res.d = tgtd;
	return res;
}

static echs_event_t
__every_year_ymcw(void *clo)
{
	struct every_clo_s *eclo = clo;
	echs_instant_t next = __ymcw_to_inst(eclo->next);

	eclo->next.y++;
	return (echs_event_t){next, eclo->state};
}

static echs_stream_t
_every_year_ymcw(echs_instant_t i, echs_mon_t m, unsigned int spec)
{
	struct every_clo_s *clo = calloc(1, sizeof(*clo));
	unsigned int c = spec >> 8U;
	unsigned int w = spec & 0x0fU;
	echs_instant_t prob = {i.y, m, YMCW_MUX(c, w), ECHS_ALL_DAY};

	clo->next = prob;
	return (echs_stream_t){__every_year_ymcw, clo};
}

DEFUN echs_stream_t
echs_every_year(echs_instant_t i, echs_mon_t mon, unsigned int when)
{
/* short for *-MON-DOM */
	/* check the mode we're in */
	if (LIKELY(when < 32U)) {
		return _every_year(i, mon, when);
	} else if (when > FIRST(0U) && when <= FIFTH(SUN)) {
		/* aaah */
		return _every_year_ymcw(i, mon, when);
	} else {
		/* um */
		;
	}
	return (echs_stream_t){NULL};
}

static echs_event_t
__every_month(void *clo)
{
	struct every_clo_s *eclo = clo;
	echs_instant_t next = eclo->next;

	eclo->next.m++;
	return (echs_event_t){echs_instant_fixup(next), eclo->state};
}

DEFUN echs_stream_t
echs_every_month(echs_instant_t i, unsigned int dom)
{
/* short for *-*-DOM */
	struct every_clo_s *clo = calloc(1, sizeof(*clo));

	if (i.d <= dom) {
		i = (echs_instant_t){i.y, i.m, dom, ECHS_ALL_DAY};
	} else {
		i = (echs_instant_t){i.y, i.m + 1, dom, ECHS_ALL_DAY};
	}
	clo->next = i;
	return (echs_stream_t){__every_month, clo};
}

DEFUN void
echs_free_every(echs_stream_t s)
{
	struct every_clo_s *clo = s.clo;

	if (clo->state != NULL) {
		/* prob strdup'd */
		free(clo->state);
		clo->state = NULL;
	}
	free(clo);
	return;
}


struct any_clo_s {
	char *state;
};

static void
__any_set_state(echs_stream_t s, const char *state)
{
	struct any_clo_s *any = s.clo;
	size_t z = strlen(state);

	/* most builders are atomic so use the BANG */
	any->state = malloc(1U + z + 1U/*for \nul*/);
	any->state[0] = '!';
	memcpy(any->state + 1U, state, z + 1U);
	return;
}

DEFUN void
echs_every_set_state(echs_stream_t s, const char *state)
{
	return __any_set_state(s, state);
}

DEFUN void
echs_wday_set_state(echs_stream_t s, const char *state)
{
	return __any_set_state(s, state);
}


/* just testing */
struct echs_mux_clo_s {
	/* last event served */
	echs_event_t last;
	/* total number of streams and the stream themselves */
	size_t nstrms;
	struct {
		echs_stream_t st;
		echs_event_t ev;
	} strms[];
};

static inline bool
__events_eq_p(echs_event_t e1, echs_event_t e2)
{
	return __inst_eq_p(e1.when, e2.when) &&
		(e1.what == e2.what || strcmp(e1.what, e2.what) == 0);
}

static echs_event_t
__refill(echs_stream_t s, echs_instant_t last)
{
	echs_event_t e;

	do {
		if (__event_0_p(e = echs_stream_next(s))) {
			break;
		}
	} while (__event_lt_p(e, last));
	return e;
}

static echs_event_t
__mux_stream(void *clo)
{
	struct echs_mux_clo_s *x = clo;
	echs_instant_t bestinst;
	size_t bestindx;

	if (UNLIKELY(x->strms == NULL)) {
		return (echs_event_t){0};
	}
	/* start out with the best, non-0 index */
	bestindx = -1UL;
	bestinst = (echs_instant_t){.u = (uint64_t)-1};

	/* try and find the very next event out of all instants */
	for (size_t i = 0; i < x->nstrms; i++) {
		echs_instant_t inst = x->strms[i].ev.when;

		if (x->strms[i].st.f == NULL) {
			continue;
		} else if (__inst_0_p(inst)) {
		clos_0:
			memset(x->strms + i, 0, sizeof(*x->strms));
			continue;
		} else if (__inst_lt_p(inst, x->last.when) ||
			   __events_eq_p(x->strms[i].ev, x->last)) {
			echs_stream_t s = x->strms[i].st;
			echs_event_t e;

			if (__event_0_p(e = __refill(s, x->last.when))) {
				goto clos_0;
			}

			/* cache E */
			x->strms[i].ev = e;
			inst = e.when;
		}

		/* do the actual check */
		if (__inst_lt_p(inst, bestinst) || __inst_0_p(bestinst)) {
			bestindx = i;
			bestinst = x->strms[bestindx].ev.when;
		}
	}

	/* BEST has the guy, or has he nought? */
	if (UNLIKELY(bestindx == -1UL)) {
		/* great */
		return (echs_event_t){0};
	} else if (UNLIKELY(__event_0_p(x->last = x->strms[bestindx].ev))) {
		/* big fucking fuck */
		return (echs_event_t){0};
	}
	/* otherwise just use the cache */
	return x->strms[bestindx].ev;
}

static void*
__mux_ctl(echs_strctl_t ctl, void *clo, ...)
{
	struct echs_mux_clo_s *x = clo;

	switch (ctl) {
	case ECHS_STRCTL_CLONE: {
		size_t sz = x->nstrms * sizeof(*x->strms) + sizeof(*x);
		struct echs_mux_clo_s *clone = malloc(sz);

		*clone = *x;
		for (size_t i = 0; i < x->nstrms; i++) {
			/* bang strms slot */
			clone->strms[i] = x->strms[i];
		}
		return clone;
	}
	case ECHS_STRCTL_UNCLONE:
		memset(x, 0, sizeof(*x) + x->nstrms * sizeof(*x->strms));
		free(x);
		break;
	default:
		break;
	}
	return NULL;
}

DEFUN echs_stream_t
echs_mux(size_t nstrm, echs_stream_t strm[])
{
	struct echs_mux_clo_s *x;
	size_t st_sz;

	st_sz = nstrm * sizeof(*x->strms) + sizeof(*x);
	x = malloc(st_sz);
	x->nstrms = nstrm;

	for (size_t i = 0; i < nstrm; i++) {
		/* bang strdef */
		x->strms[i].st = strm[i];
		/* cache the next event */
		x->strms[i].ev = echs_stream_next(strm[i]);
	}
	/* set last slot */
	x->last = (echs_event_t){.when = 0, .what = ""};
	return (echs_stream_t){__mux_stream, x, __mux_ctl};
}

DEFUN void
echs_free_mux(echs_stream_t mux_strm)
{
	struct echs_mux_clo_s *x = mux_strm.clo;

	if (LIKELY(x != NULL)) {
		__mux_ctl(ECHS_STRCTL_UNCLONE, x);
	}
	return;
}


/* simple content filter */
struct echs_sel_clo_s {
	/* last event served */
	echs_event_t last;
	/* the stream to select from */
	echs_stream_t strm;
	/* total number of selectors */
	size_t nsels;
	char *sels[];
};

static echs_event_t
__sel_stream(void *clo)
{
	struct echs_sel_clo_s *x = clo;
	echs_event_t e;

	while ((e = echs_stream_next(x->strm), !__event_0_p(e))) {
		const char *what = e.what;

		if (UNLIKELY(what == NULL)) {
			continue;
		}

		switch (*what) {
		case '~':
		case '!':
			what++;
		default:
			break;
		}

		for (size_t i = 0; i < x->nsels; i++) {
			if (!strcmp(what, x->sels[i])) {
				goto out;
			}
		}
	}
out:
	return e;
}

static void*
__sel_ctl(echs_strctl_t ctl, void *clo, ...)
{
	struct echs_sel_clo_s *x = clo;

	switch (ctl) {
	case ECHS_STRCTL_CLONE: {
		size_t sz = x->nsels * sizeof(*x->sels) + sizeof(*x);
		struct echs_sel_clo_s *clone = malloc(sz);

		*clone = *x;
		for (size_t i = 0; i < x->nsels; i++) {
			clone->sels[i] = strdup(x->sels[i]);
		}
		clone->strm = clone_echs_stream(x->strm);
		return clone;
	}
	case ECHS_STRCTL_UNCLONE:
		unclone_echs_stream(x->strm);
		for (size_t i = 0; i < x->nsels; i++) {
			/* we strdup'd them guys */
			free(x->sels[i]);
		}
		memset(x, 0, sizeof(*x) + x->nsels * sizeof(*x->sels));
		free(x);
		break;
	default:
		break;
	}
	return NULL;
}

DEFUN echs_stream_t
echs_select(echs_stream_t strm, size_t ns, const char *s[])
{
	struct echs_sel_clo_s *x;
	size_t st_sz;

	st_sz = ns * sizeof(*x->sels) + sizeof(*x);
	x = malloc(st_sz);
	for (size_t i = 0; i < (x->nsels = ns); i++) {
		x->sels[i] = strdup(s[i]);
	}

	/* set the stream, best to operate on a clone of the stream */
	x->strm = clone_echs_stream(strm);
	/* set last slot */
	x->last = (echs_event_t){.when = 0, .what = ""};
	return (echs_stream_t){__sel_stream, x, __sel_ctl};
}

DEFUN void
echs_free_select(echs_stream_t sel_strm)
{
	struct echs_sel_clo_s *x = sel_strm.clo;

	if (LIKELY(x != NULL)) {
		__sel_ctl(ECHS_STRCTL_UNCLONE, x);
	}
	return;
}


/* simple content modifier */
struct echs_ren_clo_s {
	/* the stream to select from */
	echs_stream_t strm;
	/* total number of selectors */
	size_t nrens;
	char *rens[];
};

static echs_event_t
__ren_stream(void *clo)
{
	struct echs_ren_clo_s *x = clo;
	echs_event_t e = echs_stream_next(x->strm);

	if (LIKELY(!__event_0_p(e) && e.what != NULL)) {
		const char *what = e.what;
		const char mod = *what;

		switch (mod) {
		case '~':
		case '!':
			what++;
		default:
			break;
		}

		for (size_t i = 0; i < 2 * x->nrens; i++) {
			if (!strcmp(what, x->rens[i++])) {
				/* rename */
				x->rens[i][0] = mod;
				e.what = x->rens[i];
				switch (mod) {
				case '~':
				case '!':
					break;
				default:
					e.what++;
					break;
				}
				break;
			}
		}
	}
	return e;
}

static void*
__ren_ctl(echs_strctl_t ctl, void *clo, ...)
{
	struct echs_ren_clo_s *x = clo;

	switch (ctl) {
	case ECHS_STRCTL_CLONE: {
		size_t sz = 2U * x->nrens * sizeof(*x->rens) + sizeof(*x);
		struct echs_ren_clo_s *clone = malloc(sz);

		*clone = *x;
		for (size_t i = 0; i < 2U * x->nrens; i++) {
			clone->rens[i] = strdup(x->rens[i]);
		}
		clone->strm = clone_echs_stream(x->strm);
		return clone;
	}
	case ECHS_STRCTL_UNCLONE:
		unclone_echs_stream(x->strm);
		for (size_t i = 0; i < 2 * x->nrens; i++) {
			/* we strdup'd them guys */
			free(x->rens[i]);
		}
		memset(x, 0, sizeof(*x) + x->nrens * sizeof(*x->rens));
		free(x);
		break;
	default:
		break;
	}
	return NULL;
}

DEFUN echs_stream_t
echs_rename(echs_stream_t strm, size_t nr, struct echs_rename_atom_s r[])
{
	struct echs_ren_clo_s *x;
	size_t st_sz;

	st_sz = 2U * nr * sizeof(*x->rens) + sizeof(*x);
	x = malloc(st_sz);
	for (size_t i = 0, j = 0; i < (x->nrens = nr); i++) {
		struct echs_rename_atom_s ra = r[i];

		x->rens[j++] = strdup(ra.from);
		x->rens[j++] = strdup_state(ra.to);
	}

	/* set the stream, best to operate on a clone of the stream */
	x->strm = clone_echs_stream(strm);
	return (echs_stream_t){__ren_stream, x, __ren_ctl};
}

DEFUN void
echs_free_rename(echs_stream_t ren_strm)
{
	struct echs_ren_clo_s *x = ren_strm.clo;

	if (LIKELY(x != NULL)) {
		__ren_ctl(ECHS_STRCTL_UNCLONE, x);
	}
	return;
}

/* builders.c ends here */
