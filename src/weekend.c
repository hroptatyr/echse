/*** weekend.c -- sat+sun stream
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
#include "echse.h"
#include "instant.h"

#if !defined countof
# define countof(x)		(sizeof(x) / sizeof(*x))
#endif	/* !countof */
#if !defined UNUSED
# define UNUSED(x)		__attribute__((unused)) x
#endif	/* UNUSED */

static unsigned int
__get_wday(echs_instant_t i)
{
/* sakamoto method, stolen from dateutils */
	static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
	int year = i.y;
	int res;

	year -= i.m < 3;
	res = year + year / 4U - year / 100U + year / 400U;
	res += t[i.m - 1U] + i.d;
	return (unsigned int)res % 7U;
}


/* new-year stream */
static enum {
	BEFORE_SAT,
	ON_SAT,
	BEFORE_SUN,
	ON_SUN,
} state;
static echs_instant_t next;

static echs_event_t
__stream(void *UNUSED(clo))
{
	DEFSTATE(SAT);
	DEFSTATE(SUN);
	struct echs_event_s e;

	switch (state) {
	case BEFORE_SAT:
		/* assume next points to the weekend's saturday */
		e.when = next;
		e.what = ON(SAT);
		state = ON_SAT;
		break;
	case ON_SAT:
		/* assume next points to the weekend's saturday */
		e.when = next = echs_instant_fixup((++next.d, next));
		e.what = OFF(SAT);
		state = BEFORE_SUN;
		break;
	case BEFORE_SUN:
		/* assume next points to the weekend's sunday */
		e.when = next;
		e.what = ON(SUN);
		state = ON_SUN;
		break;
	case ON_SUN:
		/* assume next points to the weekend's sunday */
		e.when = echs_instant_fixup((++next.d, next));
		e.what = OFF(SUN);
		state = BEFORE_SAT;
		/* make next point to next saturday */
		next = echs_instant_fixup((next.d += 5, next));
		break;
	default:
		abort();
	}
	return e;
}

echs_stream_t
make_echs_stream(echs_instant_t i, ...)
{
	unsigned int wd;

	switch ((wd = __get_wday(i))) {
	case 0/*S*/:
		next = i;
		state = BEFORE_SUN;
		break;
	case 1/*M*/:
	case 2/*T*/:
	case 3/*W*/:
	case 4/*R*/:
	case 5/*F*/:
	case 6/*A*/:
		/* point to next sat */
		i.d += (6U - wd);
		next = echs_instant_fixup(i);
		state = BEFORE_SAT;
		break;
	default:
		abort();
	}
	return (echs_stream_t){__stream, NULL};
}

/* weekend.c ends here */
