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
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "echse.h"




/* christmas stream */
static echs_event_t
xmas_next(echs_instant_t i)
{
	DEFSTATE(XMAS);
	static echs_state_t st[1];
	struct echs_event_s e;

	e.nwhat = 1U;
	e.what = st;

	if (i.m < 12U || i.d < 25U) {
		e.when = (echs_instant_t){i.y, 12U, 25U};
		st[0] = ON(XMAS);
	} else if (i.d < 26U) {
		e.when = (echs_instant_t){i.y, 12U, 26U};
		st[0] = OFF(XMAS);
	} else {
		e.when = (echs_instant_t){i.y + 1, 12U, 25U};
		st[0] = ON(XMAS);
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
	static echs_state_t st[1];
	struct echs_event_s e;

	e.nwhat = 1U;
	e.what = st;

	if (i.m >= 5U) {
	next_year:
		/* compute next years easter sunday right away */
		e.when = __easter(i.y + 1);
		st[0] = ON(EASTER);
	} else {
		echs_instant_t easter = __easter(i.y);

		if (i.m < easter.m || i.d < easter.d) {
			e.when = easter;
			st[0] = ON(EASTER);
		} else if (i.m > easter.m || i.d > easter.d) {
			goto next_year;
		} else {
			/* compute end of easter */
			if (++easter.d > 31U) {
				easter.d = 1U;
				easter.m = 4U;
			}
			e.when = easter;
			st[0] = OFF(EASTER);
		}
	}
	return e;
}


static void
pr_when(echs_instant_t i)
{
	fprintf(stdout, "%04u-%02u-%02u",
		(unsigned int)i.y,
		(unsigned int)i.m,
		(unsigned int)i.d);
	return;
}

int
main(void)
{
	echs_instant_t start = {2000, 1, 1};

	for (size_t j = 0; j < 8U; j++) {
		const echs_event_t nx = xmas_next(start);
		const echs_event_t ne = easter_next(start);

		if (nx.when.u == ne.when.u) {
			pr_when(nx.when);
			fputc('\t', stdout);
			for (size_t i = 0; i < nx.nwhat; i++) {
				fputs(nx.what[i], stdout);
				fputc(' ', stdout);
			}
			for (size_t i = 0; i < ne.nwhat; i++) {
				fputs(ne.what[i], stdout);
				fputc(' ', stdout);
			}
			start = nx.when;
		} else if (nx.when.u < ne.when.u) {
			pr_when(nx.when);
			fputc('\t', stdout);
			for (size_t i = 0; i < nx.nwhat; i++) {
				fputs(nx.what[i], stdout);
				fputc(' ', stdout);
			}
			start = nx.when;
		} else if (nx.when.u > ne.when.u) {
			pr_when(ne.when);
			fputc('\t', stdout);
			for (size_t i = 0; i < ne.nwhat; i++) {
				fputs(ne.what[i], stdout);
				fputc(' ', stdout);
			}
			start = ne.when;
		}
		fputc('\n', stdout);
	}
	return 0;
}

/* echse.c ends here */
