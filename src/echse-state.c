/*** echse-state.c -- read echse edge file and transform to state file
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
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "echse.h"
#include "instant.h"
#include "dt-strpf.h"
#include "strdef.h"

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */
#if !defined UNUSED
# define UNUSED(x)	__attribute__((unused)) x
#endif	/* UNUSED */

#define logger(what, how, args...)	fprintf(stderr, how "\n", args)


#if defined STANDALONE
struct evkey_s {
	const char *orig;
	uint32_t oriz;
	/* offset and size of actual key */
	uint16_t ko;
	uint16_t kz;
};

static struct evkey_s
dissect_key(const char *k)
{
	size_t z = strlen(k);
	char *renm;

	switch (k[0]) {
	case '~':
		return (struct evkey_s){k, z, 1U, (uint16_t)(z - 1U)};
	default:
		break;
	}
	/* check if it's a renaming state A->B which is short for ~A B */
	if (UNLIKELY((renm = strstr(k, "->")) != NULL)) {
		const char *nu = renm + 2U;

		/* we break the const promise here to allow for strstr()
		 * in wipe_st() below, otherwise we'd have to resort to
		 * GNU extension memmem() */
		renm[0] = '\0';
		return (struct evkey_s){k, renm - k,
				(uint16_t)(nu - k), (uint16_t)(z - (nu - k))};
	}
	return (struct evkey_s){k, z, 0U, (uint16_t)z};
}

static size_t
wipe_st(char *st, size_t si, const char *k, size_t z)
{
	char *kp;
	char *rest;

	for (kp = st;
	     (kp = strstr(kp, k)) != NULL && (kp[z] != ' ' || kp[-1] != ' ');
	     kp += z + 1U);

	if (kp == NULL) {
		/* nothing to wipe */
		return si;
	}

	rest = kp + z + 1U;
	memmove(kp, rest, si - (rest - st));
	return si - z - 1U;
}

static size_t
add_st(char *st, size_t si, const char *k, size_t z)
{
	char *kp;

	for (kp = st;
	     (kp = strstr(kp, k)) != NULL && (kp[z] != ' ' || kp[-1] != ' ');
	     kp += z + 1U);

	if (kp != NULL) {
		/* nothing to add, key already there */
		return si;
	}

	memcpy(st + si, k, z);
	return si + z + 1U;
}

static int
materialise(echs_event_t e)
{
#define INITIALZ	(256U)
	static char buf[INITIALZ];
	static char *st = buf + 24U;
	static size_t si = 0U;
	static size_t sz = sizeof(buf) - 24U;
	static echs_instant_t last;
	struct evkey_s k;

	/* let's aggregate several state changes at an instant into one */
	if (si > 0U && __inst_lt_p(last, e.when) || __inst_0_p(e.when)) {
		/* safely print whatever has been prepared */

		/* bang date and round off buffer with a nice \n for writing */
		dt_strf(st - 24U, 24U, last);

		st[-1] = '\t';
		st[si - 1U] = '\n';
		st[si] = '\0';
		if (write(STDOUT_FILENO, st - 24U, si + 24U) < 0) {
			return -1;
		}
		/* reset the stuff that we turned into \t's and \n's */
		st[-1] = ' ';
		st[si - 1U] = ' ';
	}
	if (__inst_0_p(e.when)) {
		/* that was the last ever call to materialise()
		 * clear up resources and shit*/
		if (st - 24U != buf) {
			free(st - 24U);
			st = buf + 24U;
			sz = sizeof(buf) - 24U;
		}
		/* indicate resource freedom */
		return -2;
	}
	/* record e.when */
	last = e.when;

	/* dissect the key */
	k = dissect_key(e.what);

	/* assume we need to insert k.kz many bytes, check for buffer size */
	if (si + k.kz + 1U >= sz) {
		size_t nz = ((si + k.kz + 1U + 24U) / INITIALZ + 1U) * INITIALZ;

		if (st - 24U != buf) {
			char *nu = realloc(st - 24U, nz);
			st = nu + 24U;
		} else {
			char *nu = malloc(nz);
			memcpy(nu + 24U, st, si);
			st = nu + 24U;
		}
	}

	if (LIKELY(k.ko == 0U)) {
		/* just add it */
		si = add_st(st, si, k.orig, k.oriz);
	} else if (k.ko == 1U) {
		/* wipe orig */
		si = wipe_st(st, si, k.orig + k.ko, k.kz);
	} else {
		si = wipe_st(st, si, k.orig, k.oriz);
		si = add_st(st, si, k.orig + k.ko, k.kz);
	}
	return 0;
}
#endif	/* STANDALONE */

static inline bool
__events_eq_p(echs_event_t e1, echs_event_t e2)
{
	return __inst_eq_p(e1.when, e2.when) &&
		(e1.what == e2.what || strcmp(e1.what, e2.what) == 0);
}


/* myself as stream */
struct echse_clo_s {
	size_t nstrms;
	struct {
		echs_strdef_t sd;
		echs_event_t ev;
	} *strms;
	/* last event served */
	echs_event_t last;
};

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
__stream(void *clo)
{
	struct echse_clo_s *x = clo;
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

		if (x->strms[i].sd.s.f == NULL) {
			continue;
		} else if (__inst_0_p(inst)) {
		clos_0:
			echs_close_stream(x->strms[i].sd);
			memset(x->strms + i, 0, sizeof(*x->strms));
			continue;
		} else if (__inst_lt_p(inst, x->last.when) ||
			   __events_eq_p(x->strms[i].ev, x->last)) {
			echs_stream_t s = x->strms[i].sd.s;
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

echs_stream_t
make_echs_stream(echs_instant_t inst, ...)
{
	static struct echse_clo_s x;
	va_list ap;
	const char *const *fn;
	size_t nfn;

	va_start(ap, inst);
	fn = va_arg(ap, const char *const*);
	nfn = va_arg(ap, size_t);
	va_end(ap);

	for (size_t i = 0; i < nfn; i++) {
		const char *strm = fn[i];
		echs_strdef_t sd;

		if ((sd = echs_open_stream(inst, strm)).m == NULL) {
			logger(LOG_ERR, "cannot use stream DSO %s", strm);
			continue;
		}

		if (UNLIKELY((x.nstrms % 64U) == 0U)) {
			/* realloc the streams array */
			size_t ol_sz = (x.nstrms + 0U) * sizeof(*x.strms);
			size_t nu_sz = (x.nstrms + 64U) * sizeof(*x.strms);

			x.strms = realloc(x.strms, nu_sz);
			memset(x.strms + x.nstrms, 0, nu_sz - ol_sz);
		}
		/* bang strdef */
		x.strms[x.nstrms].sd = sd;
		/* cache the next event */
		x.strms[x.nstrms].ev = echs_stream_next(sd.s);
		/* inc */
		x.nstrms++;
	}
	/* set last slot */
	x.last = (echs_event_t){.when = 0, .what = ""};
	return (echs_stream_t){__stream, &x};
}

void
free_echs_stream(echs_stream_t s)
{
	struct echse_clo_s *x = s.clo;

	if (LIKELY(x->strms != NULL)) {
		for (size_t i = 0; i < x->nstrms; i++) {
			echs_close_stream(x->strms[i].sd);
		}
		free(x->strms);
	}
	memset(x, 0, sizeof(*x));
	return;
}


#if defined STANDALONE
#include "echse.yucc"

int
main(int argc, char *argv[])
{
	/* command line options */
	yuck_t argi[1U];
	/* date range to scan through */
	echs_instant_t from;
	echs_instant_t till;
	echs_stream_t strm;
	int rc = 0;

	if (yuck_parse(argi, argc, argv)) {
		rc = 1;
		goto out;
	}

	if (argi->from_arg) {
		from = dt_strp(argi->from_arg);
	} else {
		from = (echs_instant_t){2000, 1, 1};
	}

	if (argi->till_arg) {
		till = dt_strp(argi->till_arg);
	} else {
		till = (echs_instant_t){2037, 12, 31};
	}

	/* generate the input stream to our filter */
	strm = make_echs_stream(from, argi->args, argi->nargs);

	/* the iterator */
	for (echs_event_t e;
	     (e = echs_stream_next(strm),
	      !__event_0_p(e) && __event_le_p(e, till));) {
		if (UNLIKELY(materialise(e) < 0)) {
			break;
		}
	}

	/* materialise whatever's left */
	(void)materialise((echs_event_t){0});

	/* get all of them streams in here finished */
	free_echs_stream(strm);

out:
	yuck_free(argi);
	return rc;
}
#endif	/* STANDALONE */

/* echse-state.c ends here */
