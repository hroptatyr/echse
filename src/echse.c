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
#include <time.h>

#include "echse.h"
#include "boobs.h"
#include "dt-strpf.h"

#define countof(x)		(sizeof(x) / sizeof(*x))

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */


/* helpers */
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


/* christmas stream */
static echs_event_t
xmas_next(echs_instant_t i)
{
	DEFSTATE(XMAS);
	struct echs_event_s e;

	if (i.m < 12U || i.d < 25U) {
		e.when = (echs_instant_t){i.y, 12U, 25U};
		e.what = ON(XMAS);
	} else if (i.d < 26U) {
		e.when = (echs_instant_t){i.y, 12U, 26U};
		e.what = OFF(XMAS);
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
echs_event_t
echs_stream(echs_instant_t inst)
{
/* this is main() implemented as coroutine with echs_stream_f's signature */
	static echs_stream_f src[] = {xmas_next, easter_next};
	static uint64_t uinsts[countof(src)];
	static echs_state_t states[countof(src)];
	uint64_t uinst = __inst_u64(inst);
	size_t best = 0;
	echs_event_t e;

	/* try and find the very next event out of all instants */
	for (size_t i = 0; i < countof(uinsts); i++) {
		if (uinsts[i] < uinst) {
			/* refill */
			e = src[i](inst);
			uinsts[i] = __inst_u64(e.when);
			states[i] = e.what;
		}
		if (uinsts[i] < uinsts[best]) {
			best = i;
		}
	}

	/* BEST has the guy, remember for return value */
	e.when = __u64_inst(uinsts[best]);
	e.what = states[best];

	{
		/* refill that cache now that we still know who's best */
		echs_event_t ne = src[best](e.when);
		uinsts[best] = __inst_u64(ne.when);
		states[best] = ne.what;
	}
	return e;
}


int
main(void)
{
	echs_instant_t next = {2000, 1, 1};

	for (size_t j = 0; j < 40U; j++) {
		echs_event_t e = echs_stream(next);
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
	return 0;
}

/* echse.c ends here */
