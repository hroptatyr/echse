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
#include "boobs.h"

#define countof(x)		(sizeof(x) / sizeof(*x))


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


static uint64_t
__inst_u64(echs_instant_t x)
{
	return be64toh(x.u);
}

static echs_instant_t
__u64_inst(uint64_t x)
{
	return (echs_instant_t){.u = htobe64(x)};
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
	echs_stream_f src[] = {xmas_next, easter_next};
	uint64_t uinsts[countof(src)];
	struct {
		size_t nwhat;
		const echs_state_t *what;
	} states[countof(src)];

	/* fill caches */
	for (size_t i = 0; i < countof(src); i++) {
		echs_event_t e = src[i](start);

		uinsts[i] = __inst_u64(e.when);
		states[i].nwhat = e.nwhat;
		states[i].what = e.what;
	}
	for (size_t j = 0; j < 20U; j++) {
		size_t best = 0;
		echs_instant_t next;

		/* try and find the very next event out of all instants */
		for (size_t i = 1; i < countof(uinsts); i++) {
			if (uinsts[i] < uinsts[best]) {
				best = i;
			}
		}

		/* BEST has the guy */
		next = __u64_inst(uinsts[best]);
		pr_when(next);
		fputc('\t', stdout);
		for (size_t i = 0; i < states[best].nwhat; i++) {
			fputs(states[best].what[i], stdout);
			fputc(' ', stdout);
		}
		fputc('\n', stdout);

		/* refill that cache */
		{
			echs_event_t e = src[best](next);
			uinsts[best] = __inst_u64(e.when);
			states[best].nwhat = e.nwhat;
			states[best].what = e.what;
		}
	}
	return 0;
}

/* echse.c ends here */
