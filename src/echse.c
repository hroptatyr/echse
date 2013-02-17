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
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "echse.h"
#include "instant.h"
#include "dt-strpf.h"
#include "module.h"

#define countof(x)		(sizeof(x) / sizeof(*x))

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */


/* christmas stream */
static echs_event_t
xmas_next(echs_instant_t i)
{
	DEFSTATE(XMAS);
	DEFSTATE(BOXD);
	struct echs_event_s e;

	if (i.m < 12U || i.d < 25U) {
		e.when = (echs_instant_t){i.y, 12U, 25U};
		e.what = ON(XMAS);
	} else if (i.d <= 26U) {
		static const echs_state_t s[] = {OFF(XMAS), ON(BOXD)};
		static const echs_state_t *sp = s;

		if (sp < s + countof(s)) {
			e.when = (echs_instant_t){i.y, 12U, 26U};
			e.what = *sp++;
		} else {
			/* reset state counter */
			sp = s;
			e.when = (echs_instant_t){i.y, 12U, 27U};
			e.what = OFF(BOXD);
		}
	} else {
		e.when = (echs_instant_t){i.y + 1, 12U, 25U};
		e.what = ON(XMAS);
	}
	return e;
}


/* easter stream */
static echs_instant_t
__easter(unsigned int y)
{
	/* compute gregorian easter date first */
	unsigned int a = y % 19U;
	unsigned int b = y / 4U;
	unsigned int c = b / 25U + 1;
	unsigned int d = 3U * c / 4U;
	unsigned int e;

	e = 19U * a + -((8U * c + 5) / 25U) + d + 15U;
	e %= 30U;
	e += (29578U - a - 32U * e) / 1024U;
	e = e - ((y % 7U) + b - d + e + 2) % 7U;
	return (echs_instant_t){y, e <= 31 ? 3U : 4U, e <= 31U ? e : e - 31U};
}

static echs_event_t
easter_next(echs_instant_t i)
{
	DEFSTATE(EASTER);
	struct echs_event_s e;

	if (i.m >= 5U) {
	next_year:
		/* compute next years easter sunday right away */
		e.when = __easter(i.y + 1);
		e.what = ON(EASTER);
	} else {
		echs_instant_t easter = __easter(i.y);

		if (i.m > easter.m || i.d > easter.d) {
			goto next_year;
		} else if (i.m < easter.m || i.d < easter.d) {
			e.when = easter;
			e.what = ON(EASTER);
		} else {
			/* compute end of easter */
			if (++easter.d > 31U) {
				easter.d = 1U;
				easter.m = 4U;
			}
			e.when = easter;
			e.what = OFF(EASTER);
		}
	}
	return e;
}


/* myself as stream */
static echs_stream_f *evf;
static echs_event_t *evs;
static size_t nevf;

int
fini_stream(void)
{
	if (LIKELY(evf != NULL)) {
		free(evf);
		free(evs);
	}
	nevf = 0UL;
	evf = NULL;
	evs = NULL;
	return 0;
}

int
init_stream(void)
{
	return 0;
}

static void
add_stream(echs_stream_f f)
{
	if (UNLIKELY((nevf % 64U) == 0U)) {
		evf = realloc(evf, (nevf + 64U) * sizeof(*evf));
		/* also realloc the event cache */
		evs = realloc(evs, (nevf + 64U) * sizeof(*evs));
		memset(evs + nevf, 0, 64 * sizeof(evs));
	}
	/* bang f */
	evf[nevf++] = f;
	return;
}

echs_event_t
echs_stream(echs_instant_t inst)
{
	size_t best = 0;
	echs_event_t e;

	if (UNLIKELY(evs == NULL)) {
		e.when = (echs_instant_t){0};
		e.what = NULL;
		return e;
	}

	/* try and find the very next event out of all instants */
	for (size_t i = 0; i < nevf; i++) {
		if (__inst_lt_p(evs[i].when, inst)) {
			/* refill */
			evs[i] = evf[i](inst);
		}
		if (__inst_lt_p(evs[i].when, evs[best].when)) {
			best = i;
		}
	}

	/* BEST has the guy, remember for return value */
	e = evs[best];

	/* refill that cache now that we still know who's best */
	evs[best] = evf[best](e.when);
	return e;
}


/* dso handling */
#define logger(what, how, args...)	fprintf(stderr, how "\n", args)

static int
close_aux(echs_mod_t m)
{
	echs_mod_f finf;
	int res = -1;

	if ((finf = echs_mod_sym(m, "fini_stream")) == NULL) {
		logger(LOG_ERR, "cannot fini dso %p", m);
	} else {
		res = ((int(*)(void))finf)();
	}
	return res;
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
	/* dso list */
	static echs_mod_t *ems;
	static size_t nems;
	/* command line options */
	struct echs_args_info argi[1];
	/* date range to scan through */
	echs_instant_t from;
	echs_instant_t till;
	echs_instant_t next;
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

	for (unsigned int i = 0; i < argi->inputs_num; i++) {
		const char *strm = argi->inputs[i];
		echs_mod_t m;
		echs_mod_f inif;
		echs_mod_f strf;

		if ((m = echs_mod_open(strm)) == NULL) {
			logger(LOG_ERR, "cannot open dso %s", strm);
		} else if ((inif = echs_mod_sym(m, "init_stream")) == NULL) {
			logger(LOG_ERR, "cannot init dso %s", strm);
		} else if ((strf = echs_mod_sym(m, "echs_stream")) == NULL) {
			logger(LOG_ERR, "cannot find stream in %s", strm);
		} else if (((int(*)(void))inif)() < 0) {
			logger(LOG_ERR, "init'ing dso %s failed", strm);
		} else {
			/* everything in order, call the initter */
			res = ((int(*)(void))inif)();

			if (nems % 64U) {
				ems = realloc(ems, (nems + 64) * sizeof(*ems));
			}
			ems[nems] = m;

			/* add the stream function */
			add_stream((echs_stream_f)strf);
		}
	}

	/* the iterator */
	next = from;
	for (echs_event_t e;
	     (e = echs_stream(next)).when.u && __inst_le_p(e.when, till);) {
		static char buf[256];
		char *bp = buf;

		/* BEST has the guy */
		next = e.when;
		bp += dt_strf(buf, sizeof(buf), next);
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
	fini_stream();

	for (size_t i = 0; i < nems; i++) {
		close_aux(ems[i]);
	}
	free(ems);

out:
	echs_parser_free(argi);
	return res;
}

/* echse.c ends here */
