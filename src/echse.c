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
#include "stream.h"

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */
#if !defined UNUSED
# define UNUSED(x)	__attribute__((unused)) x
#endif	/* UNUSED */


/* myself as stream */
static echs_strdef_t *esds;
static echs_event_t *evs;
static size_t nesds;

int
fini_stream(void *UNUSED(clo))
{
	if (LIKELY(esds != NULL)) {
		for (size_t i = 0; i < nesds; i++) {
			echs_close(esds[i]);
		}
		free(esds);
		free(evs);
	}
	nesds = 0UL;
	esds = NULL;
	evs = NULL;
	return 0;
}

void*
init_stream(void)
{
	return NULL;
}

static void
add_strdef(echs_strdef_t sd)
{
	if (UNLIKELY((nesds % 64U) == 0U)) {
		esds = realloc(esds, (nesds + 64U) * sizeof(*esds));
		/* also realloc the event cache */
		evs = realloc(evs, (nesds + 64U) * sizeof(*evs));
		memset(evs + nesds, 0, 64 * sizeof(evs));
	}
	/* bang f */
	esds[nesds++] = sd;
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
	for (size_t i = 0; i < nesds; i++) {
		if (esds[i] == NULL) {
			continue;
		} else if (__inst_lt_p(evs[i].when, inst)) {
			/* refill */
			evs[i] = echs_stream_next(esds[i], inst);
			if (__inst_0_p(evs[i].when)) {
				echs_close(esds[i]);
				esds[i] = NULL;
				continue;
			}
		}
		if (__inst_lt_p(evs[i].when, evs[best].when) ||
		    __inst_0_p(evs[best].when)) {
			best = i;
		}
	}

	/* BEST has the guy, remember for return value */
	e = evs[best];

	/* refill that cache now that we still know who's best */
	evs[best] = echs_stream_next(esds[best], e.when);
	if (__inst_0_p(evs[best].when)) {
		echs_close(esds[best]);
		esds[best] = NULL;
	}
	return e;
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

#define logger(what, how, args...)	fprintf(stderr, how "\n", args)

int
main(int argc, char *argv[])
{
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
		echs_strdef_t m;

		if ((m = echs_open(strm)) == NULL) {
			logger(LOG_ERR, "cannot use stream DSO %s", strm);
			continue;
		}

		/* add the stream function */
		add_strdef(m);
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
	fini_stream(NULL);

out:
	echs_parser_free(argi);
	return res;
}

/* echse.c ends here */
