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


/* myself as stream */
struct echse_clo_s {
	size_t nstrms;
	struct {
		echs_strdef_t sd;
		echs_event_t ev;
	} *strms;
	/* index to refill */
	size_t rfll;
	/* last instant served */
	echs_instant_t last;
};

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
	bestindx = 0;
	bestinst = x->strms[bestindx].ev.when;

	/* try and find the very next event out of all instants */
	for (size_t i = 0; i < x->nstrms; i++) {
		echs_instant_t inst = x->strms[i].ev.when;

		if (x->strms[i].sd.s.f == NULL) {
			continue;
		} else if (i == x->rfll || __inst_lt_p(inst, x->last)) {
			echs_stream_t s = x->strms[i].sd.s;
			echs_event_t e;

			/* refill */
			do {
				e = echs_stream_next(s);
			} while (!__inst_0_p(e.when) &&
				 __inst_lt_p(e.when, x->last));

			if (__inst_0_p(e.when)) {
				echs_close(x->strms[i].sd);
				memset(x->strms + i, 0, sizeof(*x->strms));
				continue;
			}

			/* otherwise cache E */
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
	x->rfll = bestindx;
	x->last = bestinst;
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
	/* set refill slot */
	x.rfll = -1UL;
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
	echs_stream_t this;
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

	this = make_echs_stream(from, argi->inputs, argi->inputs_num);

	/* the iterator */
	for (echs_event_t e;
	     (e = echs_stream_next(this),
	      !__inst_0_p(e.when) && __inst_le_p(e.when, till));) {
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
			break;
		}
	}

	/* get all of them streams in here finished */
	free_echs_stream(this);

out:
	echs_parser_free(argi);
	return res;
}

/* echse.c ends here */
