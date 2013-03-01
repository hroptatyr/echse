/*** echse.c -- testing echse concept
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
#include "fltdef.h"

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
static int
materialise(echs_event_t e)
{
	static char buf[256];
	char *bp = buf;

	/* BEST has the guy */
	bp += dt_strf(buf, sizeof(buf), e.when);
	*bp++ = '\t';
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
			echs_close(x->strms[i].sd);
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

	/* BEST has the guy */
	if (UNLIKELY(__event_0_p(x->last = x->strms[bestindx].ev))) {
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

		if ((sd = echs_open(inst, strm)).m == NULL) {
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
			echs_close(x->strms[i].sd);
		}
		free(x->strms);
	}
	memset(x, 0, sizeof(*x));
	return;
}


/* myself as filter */
struct echsf_clo_s {
	echs_fltdef_t fd;
};

static echs_event_t
__filter(echs_event_t e, void *clo)
{
	struct echsf_clo_s *x = clo;

	return echs_filter_next(x->fd.f, e);
}

echs_filter_t
make_echs_filter(echs_instant_t from, ...)
{
	static struct echsf_clo_s x;
	va_list ap;
	const char *fn;

	va_start(ap, from);
	fn = va_arg(ap, const char *);
	va_end(ap);

	if ((x.fd = echs_open_fltdef(from, fn)).m == NULL) {
		logger(LOG_ERR, "cannot use stream DSO %s", fn);
	}
	return (echs_filter_t){__filter, &x};
}

void
free_echs_filter(echs_filter_t f)
{
	struct echsf_clo_s *clo = f.clo;

	if (LIKELY(clo != NULL)) {
		echs_close_fltdef(clo->fd);
	}
	return;
}


#if defined STANDALONE
#if defined __INTEL_COMPILER
# pragma warning (disable:593)
# pragma warning (disable:181)
#elif defined __GNUC__
# pragma GCC diagnostic ignored "-Wswitch"
# pragma GCC diagnostic ignored "-Wswitch-enum"
#endif /* __INTEL_COMPILER */
#include "echse-clo.h"
#include "echse-clo.c"
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
	/* date range to scan through */
	echs_instant_t from;
	echs_instant_t till;
	echs_filter_t thif;
	echs_stream_t this;
	echs_stream_t strm;
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

	/* generate the input stream to our filter */
	strm = make_echs_stream(from, argi->inputs, argi->inputs_num);

	if (argi->filter_given) {
		thif = make_echs_filter(from, argi->filter_arg);
		this = make_echs_filtstrm(thif, strm);
	} else {
		thif = (echs_filter_t){NULL};
		this = strm;
	}

	/* the iterator */
	for (echs_event_t e;
	     (e = echs_stream_next(this),
	      !__event_0_p(e) && __event_le_p(e, till));) {
		if (UNLIKELY(materialise(e) < 0)) {
			break;
		}
	}

	/* free filtstrm */
	if (argi->filter_given) {
		free_echs_filtstrm(this);
	}

	/* free filter */
	free_echs_filter(thif);

	/* get all of them streams in here finished */
	free_echs_stream(strm);

out:
	echs_parser_free(argi);
	return res;
}
#endif	/* STANDALONE */

/* echse.c ends here */
